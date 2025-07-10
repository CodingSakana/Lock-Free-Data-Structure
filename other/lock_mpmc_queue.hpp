/*
Threads   : 4 (2P/2C)
Enqueued  : 2000000
Dequeued  : 2000000
Deq empty : 179825
Elapsed   : 0.68851 s
Throughput: 2.90482 M ops/s
*/

#pragma once

#include <memory>
#include <mutex>

// 多生产者多消费者（MPMC）的线程安全队列
// 接口：
//   void enqueue(const T& data);
//   std::shared_ptr<T> dequeue();
// 当队列空时，dequeue() 返回空的 shared_ptr<T>()。

template<typename T>
class LockMPMCQueue {
private:
    // 链表节点，每个节点持有一个 shared_ptr<T> data 以及指向下一个节点的指针
    struct Node {
        std::shared_ptr<T> data;  // 存放用户数据
        Node* next;               // 指向下一个节点

        // 用于创建“真实”节点，将 value 封装到 shared_ptr 中
        explicit Node(const T& value)
            : data(std::make_shared<T>(value)), next(nullptr) {}

        // 用于创建哑节点（Dummy）：不存放有效 data
        Node() : data(nullptr), next(nullptr) {}
    };

    Node* head;        // 指向链表头（哑节点/Dummy）
    Node* tail;        // 指向链表尾（最新插入的节点，也可能是哑节点）
    std::mutex mtx;    // 保护 head、tail 及链表操作的互斥锁

public:
    LockMPMCQueue()
    {
        // 初始时，head 和 tail 都指向同一个 Dummy 节点
        head = tail = new Node();
    }

    ~LockMPMCQueue() {
        // 析构时不断调用 dequeue()，直到队列清空
        while (dequeue()) {
            // 只要 dequeue() 返回了非空 shared_ptr，就说明还有节点待删除
        }
        // 最后还剩一个 Dummy 节点，dequeue() 返回空后，此 head 也需手动删掉
        delete head;
    }

    // 将 value 入队
    void enqueue(const T& value) {
        // 首先在堆上创建一个“真实节点”（包含 value）
        Node* new_node = new Node(value);

        // 上锁后，插入到链表尾部
        std::lock_guard<std::mutex> lock(mtx);
        tail->next = new_node;  // 当前尾节点的 next 指向新节点
        tail = new_node;        // 然后把 tail 指向新节点
        // lock_guard 离开作用域后自动解锁
    }

    // 出队，如果队列空则返回空的 shared_ptr<T>()
    std::shared_ptr<T> dequeue() {
        std::lock_guard<std::mutex> lock(mtx);

        // head 永远指向 Dummy；真正要弹出的节点是 head->next
        Node* first_real = head->next;
        if (!first_real) {
            // 如果 head->next == nullptr，队列为空
            return std::shared_ptr<T>();
        }
        // 记录旧的 Dummy 节点，以便 delete
        Node* old_head = head;
        // 新的 Dummy 变为第一个真实节点
        head = first_real;

        // 取出要返回的数据 shared_ptr<T>
        std::shared_ptr<T> res = first_real->data;
        // 释放旧的 Dummy 节点（它不含有效 data）
        delete old_head;

        return res;
    }

    // 可选：检查队列是否为空（多线程时，仅作瞬时判断）
    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return (head->next == nullptr);
    }
};

using TestMPMCQueue = LockMPMCQueue<int>;
