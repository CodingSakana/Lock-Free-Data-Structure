/*
Threads   : 4 (2P/2C)
Pushed    : 2000000
Popped    : 2000000
Deq empty : 13849
Elapsed   : 1.76818 s
Throughput: 1.1311 M ops/s
*/

// lock-free_mpmc_stack.hpp

#pragma once

#include <atomic>
#include <memory>
#include <optional>

template<typename T>
class LockFreeMPMCStack{
private:
    struct Node{
        std::shared_ptr<T> data;
        std::shared_ptr<Node> next;

        Node(T const& value) : data(std::make_shared<T>(value)){}
    };

    std::atomic<std::shared_ptr<Node>> head;    // 需要 CAS 操作
    
public:
    void push(T const& value);
    std::optional<std::shared_ptr<T>> pop();
        // std::optional<std::shared_ptr<T>> 表示一个“可能有值”的 std::shared_ptr<T> 类型变量。
        // 用于表示 pop() 等函数在失败时（如栈为空）不抛异常，而是返回一个空值（std::nullopt）。
    bool empty() const;
};

template<typename T>
void LockFreeMPMCStack<T>::push(T const& value){
    auto newNode = std::make_shared<Node>(value);
    newNode->next = head.load();
    while(!head.compare_exchange_weak(newNode->next, newNode));
        // 如果 compare_exchange_weak 失败，它会自动更新 newNode->next 为最新的 head
        // 所以不需要你手动再次 load
}

template<typename T>
std::optional<std::shared_ptr<T>> LockFreeMPMCStack<T>::pop(){
    auto oldHead = head.load();
    while(oldHead){
        if(head.compare_exchange_weak(oldHead, oldHead->next)){ // 注意！std::atomic<std::shared_ptr<Node>> 比较的是控制块指针，并不会出现ABA的问题。
            return oldHead->data;
        }
    }
    return std::nullopt;
}

template<typename T>
bool LockFreeMPMCStack<T>::empty() const{
    return !head.load();
        // 注意：仅表示当前时刻 head 是否为 nullptr，不保证之后不变
}

using TestMPMCStack = LockFreeMPMCStack<int>;
