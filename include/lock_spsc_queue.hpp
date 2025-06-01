// lock_spsc_queue.hpp
#ifndef LOCK_SPSC_QUEUE_HPP
#define LOCK_SPSC_QUEUE_HPP

#include <array>
#include <mutex>
#include <optional>
#include <cstddef>
#include <stdexcept>

template<typename T, std::size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue()
        : head_(0), tail_(0), count_(0)
    {
        if (Capacity == 0) {
            throw std::invalid_argument("Capacity must be > 0");
        }
    }

    ~SPSCQueue() = default;

    // 禁止拷贝和赋值
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // 尝试将 value 推入队尾；如果队满返回 false，否则返回 true
    bool enqueue(const T& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (count_ == Capacity) {
            // 缓冲区已满
            return false;
        }
        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        return true;
    }

    // 尝试弹出队首元素；如果队空返回 std::nullopt，否则返回该元素
    std::optional<T> dequeue() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (count_ == 0) {
            // 队列空
            return std::nullopt;
        }
        T value = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return value;
    }

    // 返回当前队列中的元素个数
    std::size_t length() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return count_;
    }

private:
    mutable std::mutex mtx_;
    std::array<T, Capacity> buffer_;
    std::size_t head_;   // 下一个可 pop 元素的索引
    std::size_t tail_;   // 下一个可写入元素的位置索引
    std::size_t count_;  // 当前缓冲区中元素的数量
};

#endif // LOCK_SPSC_QUEUE_HPP
