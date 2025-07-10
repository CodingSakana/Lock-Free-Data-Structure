// lock_mpsc_queue.h
// 多生产者单消费者（MPSC）的线程安全环形队列
//
// 该实现基于一个固定容量的环形缓冲区，使用互斥锁保证并发安全：
//   · 多个生产者线程可同时调用 enqueue()；内部使用互斥锁串行化写入。
//   · 单个消费者线程调用 dequeue() 获取数据。
//   · 当队列已满时，enqueue() 返回 false；当队列为空时，dequeue() 返回 std::nullopt。
//   · length() 返回当前有效元素数量，出于一致性也加锁读取。
// 适用场景：日志收集、任务派发等典型 MPSC 场景，对性能要求不及无锁实现但希望简洁可靠。

#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

template<typename T, std::size_t Capacity>
class LockMPSCQueue {
    static_assert(Capacity > 0, "Capacity must be greater than 0");

public:
    LockMPSCQueue()
        : head_(0), tail_(0), count_(0)
    {
        if constexpr (Capacity == 0) {
            throw std::invalid_argument("Capacity must be > 0");
        }
    }

    ~LockMPSCQueue() = default;

    // 禁止拷贝和赋值，避免误用
    LockMPSCQueue(const LockMPSCQueue&) = delete;
    LockMPSCQueue& operator=(const LockMPSCQueue&) = delete;

    // ----------- API -----------

    // 将 value 推入队尾；成功返回 true，若队满返回 false
    template<typename U>
    bool enqueue(U&& value) {
        static_assert(std::is_constructible_v<T, U&&>,
                      "Value type must be convertible to T");

        std::lock_guard<std::mutex> lock(mtx_);
        if (count_ == Capacity) {
            return false;              // 缓冲区已满
        }

        buffer_[tail_] = std::forward<U>(value);
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    // 弹出队首元素；若队空返回 std::nullopt
    std::optional<T> dequeue() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (count_ == 0) {
            return std::nullopt;       // 队列空
        }

        T value = std::move(buffer_[head_]);
        head_ = (head_ + 1) % Capacity;
        --count_;
        return value;
    }

    // 返回当前队列中元素数量
    std::size_t length() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return count_;
    }

private:
    mutable std::mutex mtx_;
    std::array<T, Capacity> buffer_;
    std::size_t head_;   // 下一个可 pop 元素的索引
    std::size_t tail_;   // 下一个可写入元素的位置索引
    std::size_t count_;  // 当前缓冲区元素数量
};
