/*
mpsc队列实现
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <memory>

template<typename T>
class MPSCQueue{
private:
    struct Node{
        std::optional<T> data;
        std::atomic<Node*> next;

        Node(): data(std::nullopt), next(nullptr){}
        template<typename U>  
        explicit Node(U&& value): data(std::forward<U>(value)), next(nullptr) {}
    };

    std::atomic<Node*> head;
    std::atomic<Node*> tail;

public:
    MPSCQueue():head(nullptr), tail(nullptr){
        Node* dummy = new Node();
        head = dummy;
        tail.store(dummy);
    }
    ~MPSCQueue(){delete head.load();}

    template<typename U>
    void enqueue(U&& value);
    std::optional<T> dequeue();
};

template<typename T>
template<typename U>
void MPSCQueue<T>::enqueue(U&& value){      // 多个生产者
    Node* newNode = new Node(std::forward<U>(value));
    auto prev = tail.exchange(newNode, std::memory_order_relaxed);     // 很华丽的写法
    prev->next.store(newNode, std::memory_order_release);       // 确保写入数据对别的线程可见：在最后写共享指针时加 release
}

template<typename T>
std::optional<T> MPSCQueue<T>::dequeue() {
    Node* next = head.load()->next.load(std::memory_order_acquire);    // 确保看到的数据是构造完成的：在第一次读共享指针时加 acquire
    if (next == nullptr) {
        return std::nullopt; // 队列为空
    }
    // 获取数据，释放旧节点
    std::optional<T> result = std::move(next->data);
    delete head;
    head = next;

    return result;
}
