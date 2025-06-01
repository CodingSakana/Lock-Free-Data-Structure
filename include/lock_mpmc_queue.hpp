// mpmc_queue_1.hpp
#ifndef MPMC_QUEUE_1_HPP
#define MPMC_QUEUE_1_HPP

#include <queue>
#include <mutex>
#include <optional>

template<typename T>
class lock_free_queue
{
public:
    lock_free_queue() = default;
    ~lock_free_queue() = default;

    // 将一个元素推入队列
    void push(T new_value) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push(std::move(new_value));
    }

    // 弹出队首元素，如果队列为空则返回 empty optional
    std::optional<T> pop() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // 返回队列当前大小
    std::size_t length() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

private:
    mutable std::mutex mtx_;
    std::queue<T>    queue_;
};

#endif // MPMC_QUEUE_1_HPP
