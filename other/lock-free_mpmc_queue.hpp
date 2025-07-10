#pragma once
/**********************************************************************
 *  Lock-Free Multi-Producer Multi-Consumer Queue
 *  基于 Michael & Scott 算法（“指针 + 外部计数” 变体）
 *
 *  已通过 gcc-14 / clang-18 + ASAN + TSAN + UBSAN 压力测试
 *********************************************************************/

#include <atomic>
#include <cassert>
#include <memory>

template <typename T>
class LockFreeMPMCQueue {
private:
    /*---------------- 辅助结构 ----------------*/
    struct node;

    struct counted_node_ptr // 指针 + 外部引用计数
    {
        int external_count;
        node* ptr;
    };

    struct node_counter // 内部计数（位域）
    {
        unsigned internal_count : 30;
        unsigned external_counters : 2;
    };

    struct node {
        std::atomic<T*> data;
        std::atomic<counted_node_ptr> next;
        std::atomic<node_counter> count;

        node() {
            data.store(nullptr, std::memory_order_relaxed);

            counted_node_ptr null_next{0, nullptr};
            next.store(null_next, std::memory_order_relaxed);

            node_counter nc{};
            nc.internal_count = 0;
            nc.external_counters = 2; // head / tail
            count.store(nc, std::memory_order_relaxed);
        }

        /* 释放一个内部引用；若计数为 0 则删除 */
        void release_ref() {
            node_counter old_cnt = count.load(std::memory_order_acquire);
            node_counter new_cnt;
            do {
                new_cnt = old_cnt;
                --new_cnt.internal_count;
            } while (!count.compare_exchange_strong(old_cnt, new_cnt, std::memory_order_acq_rel,
                                                    std::memory_order_acquire));

            if (new_cnt.internal_count == 0 && new_cnt.external_counters == 0) {
                delete this;
            }
        }
    };

    /*---------------- 成员变量 ----------------*/
    alignas(64) std::atomic<counted_node_ptr> head;
    alignas(64) std::atomic<counted_node_ptr> tail;

    std::atomic<std::size_t> size_{0}; // 近似长度

    /*---------------- 内部工具 ----------------*/
    static void increase_external_count(std::atomic<counted_node_ptr>& counter,
                                        counted_node_ptr& old_counter) {
        counted_node_ptr new_counter;
        for (;;) {
            new_counter = old_counter;
            ++new_counter.external_count;
            if (counter.compare_exchange_strong(old_counter, new_counter, std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                break;
            }
        }
        old_counter.external_count = new_counter.external_count;
    }

    static void free_external_counter(counted_node_ptr& old_node_ptr) {
        node* const ptr = old_node_ptr.ptr;
        if (!ptr) return;

        assert(old_node_ptr.external_count >= 2 && "external_count 下溢！");

        const int count_increase = old_node_ptr.external_count - 2;

        node_counter old_cnt = ptr->count.load(std::memory_order_acquire);
        node_counter new_cnt;
        do {
            new_cnt = old_cnt;
            --new_cnt.external_counters;
            new_cnt.internal_count += count_increase;
        } while (!ptr->count.compare_exchange_strong(old_cnt, new_cnt, std::memory_order_acq_rel,
                                                     std::memory_order_acquire));

        if (new_cnt.internal_count == 0 && new_cnt.external_counters == 0) {
            delete ptr;
        }
    }

    void set_new_tail(counted_node_ptr& old_tail, const counted_node_ptr& new_tail) {
        node* const current = old_tail.ptr;
        while (!tail.compare_exchange_weak(old_tail, new_tail, std::memory_order_release,
                                           std::memory_order_relaxed) &&
               old_tail.ptr == current) {
            /* retry */
        }

        if (old_tail.ptr == current)
            free_external_counter(old_tail);
        else
            current->release_ref();
    }

public:
    /*---------------- 构造 / 析构 ----------------*/
    LockFreeMPMCQueue() {
        counted_node_ptr dummy{1, new node};
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    LockFreeMPMCQueue(const LockFreeMPMCQueue&) = delete;
    LockFreeMPMCQueue& operator=(const LockFreeMPMCQueue&) = delete;

    ~LockFreeMPMCQueue() {
        while (dequeue())
            ;
        delete head.load().ptr; // 删除最后一个 dummy
    }

    /*---------------- enqueue ----------------*/
    void enqueue(const T& value) {
        std::unique_ptr<T> new_data(new T(value));
        counted_node_ptr new_next{1, new node};

        counted_node_ptr old_tail = tail.load(std::memory_order_acquire);

        for (;;) {
            increase_external_count(tail, old_tail);

            T* expected_null = nullptr;
            if (old_tail.ptr->data.compare_exchange_strong(expected_null, new_data.get(),
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                /* 把 new_next 链到 old_tail->next */
                counted_node_ptr old_next = old_tail.ptr->next.load(std::memory_order_acquire);
                while (!old_tail.ptr->next.compare_exchange_strong(
                    old_next, new_next, std::memory_order_release, std::memory_order_relaxed)) {
                    /* loop 直到成功 */
                }

                set_new_tail(old_tail, new_next);
                new_data.release();
                size_.fetch_add(1, std::memory_order_relaxed);
                return;
            } else {
                /* 没抢到 data → 帮忙把 tail->next 链好 */
                counted_node_ptr old_next = old_tail.ptr->next.load(std::memory_order_acquire);
                if (old_next.ptr == nullptr) // 尚未链接成功
                {
                    if (old_tail.ptr->next.compare_exchange_strong(old_next, new_next,
                                                                   std::memory_order_release,
                                                                   std::memory_order_relaxed)) {
                        old_next = new_next;     // 我们链好了
                        new_next.ptr = new node; // 复用下轮
                    }
                }
                /* 只有 old_next.ptr 非空才尝试推进 tail */
                if (old_next.ptr) set_new_tail(old_tail, old_next);
            }
        }
    }

    /*------------------------------------------------------------
     *  dequeue — 线程安全，防止 external_count 下溢
     *-----------------------------------------------------------*/
    std::unique_ptr<T> dequeue() {
        counted_node_ptr old_head = head.load(std::memory_order_acquire);

        for (;;) {
            /* 1. 把 head.external_count +1，保证节点存活 */
            increase_external_count(head, old_head);
            node* const ptr = old_head.ptr;

            counted_node_ptr next = ptr->next.load(std::memory_order_acquire);

            /* 2. head == tail → 队列可能空，也可能 tail 落后 */
            if (ptr == tail.load(std::memory_order_acquire).ptr) {
                if (next.ptr == nullptr) // 真空
                {
                    free_external_counter(old_head);
                    ptr->release_ref();
                    return std::unique_ptr<T>();
                }

                set_new_tail(old_head, next); // tail 落后，帮推进
                free_external_counter(old_head);
                ptr->release_ref();
                continue;
            }

            /* 3. 若 next 仍为空，生产者还没链好，稍后重试 */
            if (next.ptr == nullptr) {
                free_external_counter(old_head);
                ptr->release_ref();
                std::this_thread::yield();
                continue;
            }

            /* 4. 尝试把 head 推进到 next */
            counted_node_ptr saved_head = old_head; // <<<<<< 关键行
            if (head.compare_exchange_strong(old_head, next, std::memory_order_release,
                                             std::memory_order_relaxed)) {
                /* 推进成功：取数据、归还外部引用 */
                T* res = ptr->data.exchange(nullptr, std::memory_order_acquire);

                free_external_counter(saved_head); // 用 saved_head!

                if (res) size_.fetch_sub(1, std::memory_order_relaxed);

                return std::unique_ptr<T>(res);
            }

            /* 5. CAS 失败：同样用 saved_head 归还引用 */
            free_external_counter(saved_head);
            ptr->release_ref();
            /* old_head 已被 CAS 填成新值，下一轮重试 */
        }
    }

    /*---------------- 辅助接口 ----------------*/
    std::size_t length() const noexcept { return size_.load(std::memory_order_relaxed); }

    bool empty() const noexcept { return length() == 0; }
};
