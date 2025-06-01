/*
这个版本在 v1.0 的基础上添加了 内存池 和 内存对齐防止 false sharing。但是目前未添加 hazard point。目前实现会出现 freelist 的 ABA 问题。即将修复。
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <memory>
#include <utility>

template<typename T>
class MPSCQueue {
private:
    struct Node {
        std::optional<T> data;      // 队列存的数据
        std::atomic<Node*> next;    // 下一个节点的指针

        Node(): data(std::nullopt), next(nullptr) {}    // 构造函数
        template<typename U>
        explicit Node(U&& value) : data(std::forward<U>(value)), next(nullptr) {}   // 为了完美转发引入 typename U，使得 U&& 为万能引用
    };

    alignas(64) std::atomic<Node*> head;            // 内存对齐
    char pad1[64 - sizeof(std::atomic<Node*>)];     // 确保独占一行 cache line。避免 false sharing
    alignas(64) std::atomic<Node*> tail;            // 同上
    char pad2[64 - sizeof(std::atomic<Node*>)];
    std::atomic<Node*> freeStack_;                  // 内存池

public:
    MPSCQueue(): head(new Node()), tail(head.load()), freeStack_(nullptr) {}
    ~MPSCQueue() {
        // 释放主队列链表（包括 dummy）
        Node* curr = head.load();
        while (curr) {  // 普普通通的链表删除
            Node* next = curr->next.load();
            delete curr;
            curr = next;
        }

        // 释放 freelist 中节点
        Node* f = freeStack_.load();
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
    auto newNode = freeStack_.load(std::memory_order_relaxed);   // 加载内存池
    while(newNode){
        if (freeStack_.compare_exchange_weak(newNode, newNode->next,                // CAS 操作，但其实 newNode->next 这一步是分开的，先做的，所以这一行代码不是原子的
                                                    std::memory_order_acquire,      // 成功时的内存序
                                                    std::memory_order_relaxed)){    // 失败时的内存序
            newNode->data.emplace(std::forward<U>(value));                          // 移动数据
            newNode->next = nullptr;
            break;
        }
    }
    if(!newNode){
        newNode = new Node(std::forward<U>(value));     // 内存池空就 new 一个 Node
    }

    auto prev = tail.exchange(newNode, std::memory_order_relaxed);      // 很华丽的做法，可以想想多线程是如何正确运行的
    prev->next.store(newNode, std::memory_order_release);               // 确保写入数据对别的线程可见：在最后写共享指针时加 release
}

template<typename T>
std::optional<T> MPSCQueue<T>::dequeue(){   // 一个消费者
    Node* old_dummy = head.load(std::memory_order_relaxed);     // 读取 dummy 节点
    Node* dataNode  = old_dummy->next.load(std::memory_order_acquire);  // dummy 节点的 next 才是数据节点
    if(!dataNode)
        return std::nullopt;    // 无数据返回 nullopt

    head.store(dataNode, std::memory_order_relaxed);    // 一个消费者，不用管理线程安全

    std::optional<T> result = std::move(dataNode->data);
    dataNode->data.reset();

    // 经典写法
    Node* free_head = freeStack_.load(std::memory_order_relaxed);
    do{
        old_dummy->next = free_head;
    } while(freeStack_.compare_exchange_weak(free_head, old_dummy, std::memory_order_release, std::memory_order_relaxed));

    return result;
}

/*
风险复现：

开始时：
Stack -> A -> B -> C
Queue -> dummy

------P1------
        nNode next
         │    │
Stack -> A -> B -> C
Queue -> dummy

(打断)

------P2------
        nNode next
         │    │
Stack -> A -> B -> C
Queue -> dummy

CAS 成功

Stack -> B -> C
Queue -> dummy -> A

------P3------
        nNode next
         │    │
Stack -> B -> C
Queue -> dummy -> A

CAS 成功

Stack -> C
Queue -> dummy -> A -> B

------C1------
         head
         │
Stack -> C
              dataNode
                  │
Queue -> dummy -> A -> B

CAS 成功

Stack -> dummy -> C
Queue -> A -> B

------C2------
         head
         │
Stack -> dummy -> C
          dataNode
              │
Queue -> A -> B

CAS 成功

Stack -> A -> dummy -> C
Queue -> B(dummy)

------P1------
        nNode
         │
Stack -> A -> D -> dummy -> C
         next
         │
Queue -> B(dummy)

CAS 成功

         A -> D -> dummy -> C
Stack ───┐
Queue -> B(dummy)

完全错误
*/