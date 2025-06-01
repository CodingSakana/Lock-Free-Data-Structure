#pragma once

#include <atomic>
#include <memory>
template<typename T>
class lock_free_queue
{
private:
    struct node;
    struct counted_node_ptr
    {
        int external_count;
        node* ptr;
    };

    std::atomic<counted_node_ptr> head;
    std::atomic<counted_node_ptr> tail;

    struct node_counter
    {
        unsigned internal_count: 30;
        unsigned external_counters: 2;
    };

    struct node
    {
        std::atomic<T*> data;
        std::atomic<node_counter> count;
        std::atomic<counted_node_ptr> next;
        
        node()
        {
            data.store(nullptr, std::memory_order_relaxed);
            node_counter new_count;
            new_count.internal_count=0;
            new_count.external_counters=2;
            count.store(new_count, std::memory_order_relaxed);
            counted_node_ptr temp;
            temp.external_count = 0;
            temp.ptr = nullptr;
            next.store(temp, std::memory_order_relaxed);
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

    void set_new_tail(counted_node_ptr &old_tail,
                      counted_node_ptr const &new_tail)
    {
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

public:
    lock_free_queue():
        head({1, new node}), tail(head.load(std::memory_order_relaxed))
    {}
    lock_free_queue(const lock_free_queue& other) = delete;
    lock_free_queue& operator= (const lock_free_queue& other) = delete;
    ~lock_free_queue()
    {
        while(pop());   // 不断弹出并丢弃
        counted_node_ptr const node = head.load(std::memory_order_relaxed);
        delete node.ptr;   // 删除 dummy 节点
    }

    void push(T new_value)
    {
        std::unique_ptr<T> new_data(new T(new_value));
        counted_node_ptr new_next;
        new_next.ptr=new node;
        new_next.external_count=1;
        counted_node_ptr old_tail=tail.load(std::memory_order_acquire);
        for(;;)
        {
            increase_external_count(tail,old_tail);
            T* old_data=nullptr;
            if(old_tail.ptr->data.compare_exchange_strong(
                    old_data, new_data.get(),
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                counted_node_ptr old_next={0};
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
                set_new_tail(old_tail, old_next);
            }
        }
    }
    
    std::unique_ptr<T> pop()
    {
        counted_node_ptr old_head=head.load(std::memory_order_relaxed);
        for(;;)
        {
            increase_external_count(head,old_head);
            node* const ptr=old_head.ptr;

            /**/
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
            /**/

           /* ======= 这里是原书的版本 ====== 
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
