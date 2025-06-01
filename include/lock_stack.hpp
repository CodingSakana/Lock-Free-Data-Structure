// lock_mpmc_stack.hpp
#ifndef LOCK_MPMC_STACK_HPP
#define LOCK_MPMC_STACK_HPP

#include <vector>
#include <mutex>
#include <optional>
#include <cstddef>

template<typename T>
class MPMCStack {
public:
    MPMCStack() = default;
    ~MPMCStack() = default;

    // 禁止拷贝和赋值
    MPMCStack(const MPMCStack&) = delete;
    MPMCStack& operator=(const MPMCStack&) = delete;

    // 将 value push 到栈顶
    void push(T value) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_.push_back(std::move(value));
    }

    // 尝试 pop 栈顶元素；如果栈空返回 std::nullopt，否则返回该元素
    std::optional<T> pop() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (data_.empty()) {
            return std::nullopt;
        }
        T value = std::move(data_.back());
        data_.pop_back();
        return value;
    }

    // 返回当前栈内的元素个数
    std::size_t length() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_.size();
    }

private:
    mutable std::mutex     mtx_;
    std::vector<T>         data_;  // 底层用 vector 存储
};

#endif // LOCK_MPMC_STACK_HPP
