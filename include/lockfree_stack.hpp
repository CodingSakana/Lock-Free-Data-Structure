#pragma once

#include <atomic>
#include <memory>
#include <optional>

template<typename T>
class LockFreeStack{
private:
    struct Node{
        std::shared_ptr<T> data;
        std::shared_ptr<Node> next;

        Node(T const& value) : data(std::make_shared<T>(value)){}
    };

    std::atomic<std::shared_ptr<Node>> head;
    
public:
    void push(T const& value);
    std::optional<std::shared_ptr<T>> pop();
        // std::optional<std::shared_ptr<T>> 表示一个“可能有值”的 std::shared_ptr<T> 类型变量。
        // 用于表示 pop() 等函数在失败时（如栈为空）不抛异常，而是返回一个空值（std::nullopt）。
    bool empty() const;
};

template<typename T>
void LockFreeStack<T>::push(T const& value){
    auto newNode = std::make_shared<Node>(value);
    newNode->next = head.load();
    while(!head.compare_exchange_weak(newNode->next, newNode));
        // 如果 compare_exchange_weak 失败，它会自动更新 newNode->next 为最新的 head
        // 所以不需要你手动再次 load
}

template<typename T>
std::optional<std::shared_ptr<T>> LockFreeStack<T>::pop(){
    auto oldHead = head.load();
    while(oldHead){
        if(head.compare_exchange_weak(oldHead, oldHead->next)){
            return oldHead->data;
        }
            // 否则 CAS 失败, oldHead 被自动更新为当前的 head
    }
    return std::nullopt;
}

template<typename T>
bool LockFreeStack<T>::empty() const{
    return !head.load();
        // 注意：仅表示当前时刻 head 是否为 nullptr，不保证之后不变
}