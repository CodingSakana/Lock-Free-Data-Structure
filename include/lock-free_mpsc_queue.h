/*
mpsc队列实现
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>

template <typename T>
class LockFreeMPSCQueue {
private:
    struct Node {
        std::optional<T> data;
        std::atomic<Node*> next{nullptr};
        explicit Node(T&& v) : data(std::move(v)) {}
    };

    std::atomic<Node*> tail;
    std::atomic<Node*> head; // dummy 头

public:
    LockFreeMPSCQueue() {
        Node* dummy = new Node(T{}); // data 不使用
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    template <typename U>
    void enqueue(U&& v) {
        Node* n = new Node(std::forward<U>(v));
        Node* prev = tail.exchange(n, std::memory_order_acq_rel);
        prev->next.store(n, std::memory_order_release);
    }

    std::optional<T> dequeue() {
        Node* first = head.load(std::memory_order_relaxed);
        Node* next = first->next.load(std::memory_order_acquire);
        if (!next) return std::nullopt;

        std::optional<T> ret = std::move(next->data);
        head.store(next, std::memory_order_relaxed);
        delete first;
        return ret;
    }
    ~LockFreeMPSCQueue() {
        Node* n = head.load(std::memory_order_relaxed);
        while (n) {
            Node* next = n->next.load(std::memory_order_relaxed);
            delete n;
            n = next;
        }
    }
};