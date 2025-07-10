// 这个版本尝试加入内存顺序，但是好像不太对
// lock-free_mpmc_queue_1.hpp

#pragma once

#include <atomic>
#include <memory>
#include <cassert>
template <typename T>
class LockFreeMPMCQueue {
private:
    struct node;

    /* ==== 1. counted_node_ptr 本身保持 16‑B 大小即可 ==== */
    struct alignas(16) counted_node_ptr {
        int external_count;
        node* ptr;
    };

    /* ==== 2. 取出真正需要的原子对齐值 ==== */
    using atomic_ptr = std::atomic<counted_node_ptr>;
    static constexpr std::size_t kAtomicAlign = alignof(atomic_ptr);   // 16 on GCC, 64 on Clang

    /* ==== 3. 按 kAtomicAlign 对齐 head / tail ==== */
    alignas(kAtomicAlign) atomic_ptr head;
    alignas(kAtomicAlign) atomic_ptr tail;

    struct node_counter
    {
        unsigned internal_count: 30;
        unsigned external_counters: 2;
    };

    struct alignas(kAtomicAlign) node {
        std::atomic<T*>           data;
        std::atomic<node_counter> count;

        /* ==== 4. next 同样用 kAtomicAlign ==== */
        alignas(kAtomicAlign) atomic_ptr next;

        node(){
            data.store(nullptr, std::memory_order_relaxed);
            node_counter nc{0, 2};
            count.store(nc, std::memory_order_relaxed);

            counted_node_ptr tmp{0, nullptr};
            next.store(tmp, std::memory_order_relaxed);
        }
        
        void release_ref()
        {
            node_counter old_counter=
                count.load(std::memory_order_relaxed);
            node_counter new_counter;
            do
            {
                new_counter=old_counter;
                --new_counter.internal_count;
            }
            while(!count.compare_exchange_strong(
                      old_counter,new_counter,
                      std::memory_order_acq_rel,
                      std::memory_order_relaxed));
            if(!new_counter.internal_count &&
               !new_counter.external_counters)
            {
                delete this;
            }
        }
    };

    static void increase_external_count(
        std::atomic<counted_node_ptr>& counter,
        counted_node_ptr& old_counter)
    {
        counted_node_ptr new_counter;
        do
        {
            new_counter = old_counter;
            ++new_counter.external_count;
        }
        while(!counter.compare_exchange_strong(
                  old_counter,new_counter,
                  std::memory_order_acq_rel,
                  std::memory_order_relaxed));
        old_counter.external_count = new_counter.external_count;
    }

    static void free_external_counter(counted_node_ptr &old_node_ptr)
    {
        node* const ptr=old_node_ptr.ptr;
        int const count_increase=old_node_ptr.external_count-2;
        node_counter old_counter=
            ptr->count.load(std::memory_order_relaxed);
        node_counter new_counter;
        do
        {
            new_counter=old_counter;
            --new_counter.external_counters;
            new_counter.internal_count+=count_increase;
        }
        while(!ptr->count.compare_exchange_strong(
                  old_counter, new_counter,
                  std::memory_order_acq_rel,
                  std::memory_order_relaxed));
        if(!new_counter.internal_count &&
           !new_counter.external_counters)
        {
            delete ptr;
        }
    }

    inline void assert_ptr(counted_node_ptr const& p, const char* who)
    {
        if(!p.ptr) {
            fprintf(stderr, "[BUG] %s received NULL ptr\n", who);
            abort();
        }
    }

    void set_new_tail(counted_node_ptr &old_tail,
                      counted_node_ptr const &new_tail)
    {
        // assert_ptr(new_tail, "set_new_tail");
        node* const current_tail_ptr=old_tail.ptr;
        while(!tail.compare_exchange_weak(old_tail,new_tail,
                            std::memory_order_release,
                            std::memory_order_relaxed) &&
                            old_tail.ptr==current_tail_ptr);
        if(old_tail.ptr==current_tail_ptr)
            free_external_counter(old_tail);
        else
            current_tail_ptr->release_ref();
    }

    static_assert(sizeof(counted_node_ptr) == 16, "struct size must stay 16 B");
    static_assert(alignof(atomic_ptr) == kAtomicAlign, "");
    static_assert(offsetof(node, next) % kAtomicAlign == 0,
                "node::next must satisfy atomic alignment");

public:
    LockFreeMPMCQueue()
    {
        counted_node_ptr dummy{1, new node};
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }
    LockFreeMPMCQueue(const LockFreeMPMCQueue& other) = delete;
    LockFreeMPMCQueue& operator= (const LockFreeMPMCQueue& other) = delete;
    ~LockFreeMPMCQueue()
    {
        while(dequeue());   // 不断弹出并丢弃
        counted_node_ptr const node = head.load(std::memory_order_relaxed);
        delete node.ptr;   // 删除 dummy 节点
    }

    void enqueue(T new_value)
    {
        std::unique_ptr<T> new_data(new T(new_value));
        counted_node_ptr new_next{1, new node};
        counted_node_ptr old_tail=tail.load(std::memory_order_relaxed);
        for(;;)
        {
            assert_ptr(tail.load(), "1"); // 1
            increase_external_count(tail,old_tail); // old_tail 可能变为 tail
            T* old_data=nullptr;
            assert_ptr(old_tail, "2");
            if(old_tail.ptr->data.compare_exchange_strong(
                    old_data, new_data.get(),
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
            counted_node_ptr old_next{0};
            assert_ptr(old_tail, "3");
            if(!old_tail.ptr->next.compare_exchange_strong(
                    old_next,new_next,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    delete new_next.ptr;
                    new_next=old_next;
                }
                set_new_tail(old_tail, new_next);
                new_data.release();
                break;
            }
            else
            {
                counted_node_ptr old_next={0};
                if(old_tail.ptr->next.compare_exchange_strong(
                       old_next,new_next,
                       std::memory_order_release,
                       std::memory_order_relaxed))
                {
                    old_next=new_next;
                    new_next.ptr=new node;
                }
                else if(!old_next.ptr) {                  // ← CAS 失败且仍为空 ⇒ 不能推进 tail
                old_tail.ptr->release_ref();          //   归还外部计数
                continue;                             //   重新循环
            }
                set_new_tail(old_tail, old_next);
            }
        }
    }
    
    std::unique_ptr<T> dequeue()
    {
        counted_node_ptr old_head=head.load(std::memory_order_relaxed);
        for(;;)
        {
            increase_external_count(head,old_head);
            node* const ptr=old_head.ptr;

            /*
            // 先抓一次 tail 和 next，避免每次重新加载
            counted_node_ptr old_tail = tail.load(std::memory_order_acquire);
            counted_node_ptr next     = ptr->next.load(std::memory_order_acquire);

            // 如果 head==tail，可能队列真的空，也可能 tail 落后
            if (ptr == old_tail.ptr)
            {
                if (next.ptr == nullptr)
                {
                    // 真正空：放弃引用，返回空
                    ptr->release_ref();
                    return std::unique_ptr<T>();
                }
                // tail 落后：帮它推进到 next，然后重试
                set_new_tail(old_tail, next);
                continue;  // 重新从 head.load() 开始
            }
            */

           /* ======= 这里是原书的版本 ====== */
           if(ptr == tail.load().ptr)
           {
                ptr->release_ref();
                return std::unique_ptr<T>();
           }
            counted_node_ptr next = ptr->next.load(std::memory_order_acquire);
            /* ============================*/
            
            if (head.compare_exchange_strong(
                    old_head, next,
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                T* const res=ptr->data.exchange(nullptr);
                free_external_counter(old_head);
                return std::unique_ptr<T>(res);
            }
            ptr->release_ref();
        }
    }

    std::size_t length() const{
        std::size_t length = 0;
        counted_node_ptr h = head.load(std::memory_order_relaxed);
        node* ptr = h.ptr;
        while(ptr){
            ptr = ptr->next.load(std::memory_order_relaxed).ptr;
            length++;
        }
        return length - 1;
    }

    bool empty() const{
        return head.load(std::memory_order_relaxed)
        .ptr->next.load(std::memory_order_relaxed)
        .ptr == nullptr;
    }
};

using TestMPMCQueue = LockFreeMPMCQueue<int>;
