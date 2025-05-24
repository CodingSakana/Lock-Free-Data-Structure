/*
这个版本在 v1.0 的基础上添加了 内存池复用 和 内存对齐防止 false sharing。但是目前未添加 hazard point。可能会出现 freelist 的 ABA 问题。即将修复。
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <memory>
#include <utility>

template<typename T>
class MPSCQueue{
private:
    struct Node{
        std::optional<T> data;
        std::atomic<Node*> next;

        Node(): data(std::nullopt), next(nullptr){}
        template<typename U>
        explicit Node(U&& value) : data(std::forward<U>(value)), next(nullptr) {}
    };

    alignas(64) std::atomic<Node*> head;            // 内存对齐
    char pad1[64 - sizeof(std::atomic<Node*>)];     // 确保独占一行 cache line
    alignas(64) std::atomic<Node*> tail;
    char pad2[64 - sizeof(std::atomic<Node*>)];
    std::atomic<Node*> freelist_;                   // 内存池

public:
    MPSCQueue(): head(new Node()), tail(head.load()), freelist_(nullptr) {}
    ~MPSCQueue() {
        // 释放主队列链表（包括 dummy）
        Node* curr = head.load();
        while (curr) {
            Node* next = curr->next.load();
            delete curr;
            curr = next;
        }

        // 释放 freelist 中节点
        Node* f = freelist_.load();
        while (f) {
            Node* next = f->next;
            delete f;
            f = next;
        }
    }

    template<typename U>
    void enqueue(U&& value);

    std::optional<T> dequeue();
};

template<typename T>
template<typename U>
void MPSCQueue<T>::enqueue(U&& value){      // 多个生产者
    auto newNode = freelist_.load(std::memory_order_relaxed);
    while(newNode){
        if(freelist_.compare_exchange_weak(newNode, newNode->next,
                                                    std::memory_order_acquire,      // 成功时的内存序
                                                    std::memory_order_relaxed)){    // 失败时的内存序                                          
            newNode->data.emplace(std::forward<U>(value));
            newNode->next = nullptr;
            break;
        }
    }
    if(!newNode){
        newNode = new Node(std::forward<U>(value));
    }

    auto prev = tail.exchange(newNode, std::memory_order_relaxed);      // 很华丽的做法
    prev->next.store(newNode, std::memory_order_release);               // 确保写入数据对别的线程可见：在最后写共享指针时加 release
}

template<typename T>
std::optional<T> MPSCQueue<T>::dequeue(){
    Node* old_dummy = head.load(std::memory_order_relaxed);
    Node* dataNode  = old_dummy->next.load(std::memory_order_acquire);
    if(!dataNode)
        return std::nullopt;

    head.store(dataNode, std::memory_order_relaxed);

    std::optional<T> result = std::move(dataNode->data);
    dataNode->data.reset();

    Node* free_head = freelist_.load(std::memory_order_relaxed);
    do{         // 经典写法
        old_dummy->next = free_head;
    } while(!freelist_.compare_exchange_weak(free_head, old_dummy, std::memory_order_release, std::memory_order_relaxed));

    return result;
}
