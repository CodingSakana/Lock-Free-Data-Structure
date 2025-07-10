/*
这是一个环形的 spsc 的无锁队列
*/

#pragma once
#include <atomic>
#include <cstddef>
#include <optional>

template<typename T, size_t Capacity>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");     // 性能优化，避免用 % 。

public:
    LockFreeSPSCQueue() : head_(0), tail_(0) {}

    bool enqueue(const T& item);
    std::optional<T> dequeue();

private:
    T buffer_[Capacity];
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

template<typename T, size_t Capacity>
bool LockFreeSPSCQueue<T, Capacity>::enqueue(const T& item){
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_acquire);

    if((head & (Capacity - 1)) == ((tail + 1) & (Capacity - 1))) return false;

    buffer_[tail & (Capacity - 1)] = item;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

template<typename T, size_t Capacity>
std::optional<T> LockFreeSPSCQueue<T, Capacity>::dequeue(){
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t head = head_.load(std::memory_order_acquire);

    if((head & (Capacity - 1)) == (tail & (Capacity - 1))) return std::nullopt;

    T result = buffer_[head & (Capacity - 1)];
    head_.store(head + 1, std::memory_order_release);

    return result;
}
