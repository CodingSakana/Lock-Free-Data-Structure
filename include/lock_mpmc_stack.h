/*
Threads   : 4 (2P/2C)
Pushed    : 2000000
Popped    : 2000000
Deq empty : 126666
Elapsed   : 0.66962 s
Throughput: 2.98677 M ops/s
*/

#pragma once

#include <memory>
#include <mutex>

// 多生产者多消费者（MPMC）的线程安全栈
// 接口：
//   void push(const T& data);
//   std::shared_ptr<T> pop();
// 当栈空时，pop() 返回 nullptr (即空的 shared_ptr)，否则返回栈顶元素的 shared_ptr<T>。

template<typename T>
class LockMPMCStack {
private:
    // 链表节点，每个节点持有一个 shared_ptr<T> data 以及指向下一个节点的指针
    struct Node {
        std::shared_ptr<T> data;
        Node* next;

        explicit Node(const T& value)
            : data(std::make_shared<T>(value)), next(nullptr) {}
    };

    Node* head;               // 指向栈顶的链表头
    std::mutex mtx;           // 保护 head 的互斥锁

public:
    LockMPMCStack()
        : head(nullptr)
    {}

    ~LockMPMCStack() {
        // 析构时将所有剩余节点 pop 掉，释放内存
        while (pop()) {
            // 反复 pop 直到返回 nullptr
        }
    }

    // 将 value 压入栈顶
    void push(const T& value) {
        // 先创建一个新节点（包含 value）
        Node* new_node = new Node(value);

        // 上锁后插入到头部
        std::lock_guard<std::mutex> lock(mtx);
        new_node->next = head;
        head = new_node;
        // lock_guard 离开作用域后自动解锁
    }

    // 弹出栈顶元素并返回其 shared_ptr<T>
    // 如果栈空，则返回空的 shared_ptr<T>()
    std::shared_ptr<T> pop() {
        std::lock_guard<std::mutex> lock(mtx);
        if (!head) {
            // 栈空
            return std::shared_ptr<T>();
        }
        // head 不为空：摘掉头节点
        Node* old_head = head;
        head = old_head->next;

        // 将要返回的数据
        std::shared_ptr<T> res = old_head->data;
        // 释放节点内存
        delete old_head;
        return res;
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return head == nullptr;
    }
};
