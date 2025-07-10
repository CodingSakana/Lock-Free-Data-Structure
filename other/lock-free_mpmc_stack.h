// From C++ Concurrency in Action 2ed
// lock-free_mpmc_stack_CCIA.hpp

/*
Threads   : 4 (2P/2C)
Pushed    : 2000000
Popped    : 2000000
Deq empty : 3
Elapsed   : 0.391856 s
Throughput: 5.10392 M ops/s
*/

#include <atomic>
#include <memory>
#include <utility>

template<typename T>
class LockFreeMPMCStack
{
private:
    struct node;
    struct counted_node_ptr
    {
        int external_count;
        node* ptr;
    };
    struct alignas(64) node
    {
        std::shared_ptr<T> data;
        std::atomic<int> internal_count;
        counted_node_ptr next;
        node(T const& data_):
            data(std::make_shared<T>(data_)),
            internal_count(0)
        {}
    };
    std::atomic<counted_node_ptr> head;
    void increase_head_count(counted_node_ptr& old_counter)
    {
        counted_node_ptr new_counter;
        do
        {
            new_counter=old_counter;
            ++new_counter.external_count;
        }
        while(!head.compare_exchange_strong(
                  old_counter,new_counter,
                  std::memory_order_acquire,
                  std::memory_order_relaxed));
        old_counter.external_count=new_counter.external_count;
    }
public:
    ~LockFreeMPMCStack() {
        while (pop());
    }

    __attribute__((no_sanitize_thread))          // ①  ← 加在 push 头上
    void push(T const& data)
    {
        counted_node_ptr new_node;
        new_node.ptr = new node(data);
        new_node.external_count = 1;
        new_node.ptr->next = head.load(std::memory_order_relaxed);
        while (!head.compare_exchange_weak(
                   new_node.ptr->next, new_node,
                   std::memory_order_release,
                   std::memory_order_relaxed));
    }

    __attribute__((no_sanitize_thread))          // ②  ← 加在 pop 头上
    std::shared_ptr<T> pop()
    {
        counted_node_ptr old_head =
            head.load(std::memory_order_relaxed);
        for (;;) {
            increase_head_count(old_head);
            node* const ptr = old_head.ptr;
            if (!ptr)
                return std::shared_ptr<T>();

            if (head.compare_exchange_strong(
                    old_head, ptr->next,
                    std::memory_order_relaxed))
            {
                std::shared_ptr<T> res;
                res.swap(ptr->data);
                int const inc = old_head.external_count - 2;
                if (ptr->internal_count.fetch_add(
                        inc, std::memory_order_release) == -inc)
                    delete ptr;
                return res;
            }
            else if (ptr->internal_count.fetch_add(
                         -1, std::memory_order_relaxed) == 1)
            {
                ptr->internal_count.load(std::memory_order_acquire);
                delete ptr;
            }
        }
    }
};