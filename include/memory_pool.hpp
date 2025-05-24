#pragma once

#include <cstddef>

namespace memory{

constexpr std::size_t new_allocted_count = 64;

template <typename T>
class ThreadLocalPool{
public:
    T* acquire(){
        if (!freelist_){
            for(int i = 0; i < new_allocted_count; ++i){
                T* new_node = new T();
                new_node->next = freelist_;
                freelist_ = new_node;
            }
        }
        T* node = freelist_;
        freelist_ = freelist_->next;
        node->next = nullptr;
        return node;
    }

    void release(T* node){
        node->next = freelist_;
        freelist_ = node;
    }

private:
    // 每线程一个 freelist，避免锁
    thread_local static T* freelist_;
};

template <typename T>
thread_local T* ThreadLocalPool<T>::freelist_ = nullptr;

}  // namespace memory
