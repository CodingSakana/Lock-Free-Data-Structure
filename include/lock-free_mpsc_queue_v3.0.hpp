#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <memory>
#include <utility>
#include <cassert>

// 工业级 MPSC 队列 + 内存池（带双宽度 CAS 防 ABA）

using TestMPSCQueue = LockFreeMPSCQueue<int>;

// 包含指针与版本号
template <typename T>
struct TaggedPtr {
    T* ptr;
    std::size_t tag;
};

// 重载原子 TaggedPtr
template <typename T>
class AtomicTaggedPtr {
    static_assert(std::is_trivially_copyable< TaggedPtr<T> >::value,
                  "TaggedPtr must be trivially copyable");
private:
    // 将 ptr|tag 打包成单个指针大小整数
    static uintptr_t encode(TaggedPtr<T> v) noexcept {
        return reinterpret_cast<uintptr_t>(v.ptr) | (v.tag << 48);  // 用户态的指针实际只会使用低 48 位
    }
    static TaggedPtr<T> decode(uintptr_t x) noexcept {
        T* p = reinterpret_cast<T*>(x & ((uintptr_t(1) << 48) - 1));
        std::size_t t = x >> 48;
        return {p, t};
    }

    std::atomic<uintptr_t> raw;
public:
    AtomicTaggedPtr(TaggedPtr<T> init = {nullptr, 0}) noexcept
        : raw(encode(init)) {}

    TaggedPtr<T> load(std::memory_order mo = std::memory_order_acquire) const noexcept {
        return decode(raw.load(mo));
    }

    bool compare_exchange_strong(TaggedPtr<T>& expected,
                                 TaggedPtr<T> desired,
                                 std::memory_order success = std::memory_order_acq_rel,
                                 std::memory_order failure = std::memory_order_acquire) noexcept {
        auto exp_raw = encode(expected);
        bool ok = raw.compare_exchange_strong(exp_raw, encode(desired), success, failure);
        if (!ok) expected = decode(exp_raw);
        return ok;
    }
};

// MPSC 队列实现
template <typename T>
class LockFreeMPSCQueue {
private:
    struct Node {
        std::optional<T> data;
        std::atomic<Node*> next;
        Node() noexcept : data(std::nullopt), next(nullptr) {}
        template <typename U>
        explicit Node(U&& v) : data(std::forward<U>(v)), next(nullptr) {}
    };

    // Dummy head
    alignas(64) std::atomic<Node*> head_;  char pad0[64 - sizeof(std::atomic<Node*>)];
    alignas(64) std::atomic<Node*> tail_;  char pad1[64 - sizeof(std::atomic<Node*>)];
    // 内存池: 双宽度 CAS 防 ABA
    alignas(64) AtomicTaggedPtr<Node> freeStack_;  char pad2[64 - sizeof(AtomicTaggedPtr<Node>)];

public:
    LockFreeMPSCQueue()
      : head_(nullptr), tail_(nullptr), freeStack_(TaggedPtr<Node>{nullptr,0}) {
        Node* dummy = new Node();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
    }

    ~LockFreeMPSCQueue() {
        // 清空队列
        Node* p = head_.load(std::memory_order_relaxed);
        while (p) {
            Node* next = p->next.load(std::memory_order_relaxed);
            delete p;
            p = next;
        }
        // 清空内存池
        auto ft = freeStack_.load(std::memory_order_relaxed);
        Node* q = ft.ptr;
        while (q) {
            Node* next = q->next.load(std::memory_order_relaxed);
            delete q;
            q = next;
        }
    }

    // 多生产者入队
    template <typename U>
    void enqueue(U&& value) {
        // 从内存池取节点
        TaggedPtr<Node> old = freeStack_.load(std::memory_order_acquire);
        Node* node = nullptr;
        while (old.ptr) {
            TaggedPtr<Node> desired{old.ptr->next.load(std::memory_order_relaxed), old.tag + 1};
            if (freeStack_.compare_exchange_strong(old, desired)) {
                node = old.ptr;
                node->data.emplace(std::forward<U>(value));
                node->next.store(nullptr, std::memory_order_relaxed);
                break;
            }
        }
        if (!node) {
            node = new Node(std::forward<U>(value));
        }

        // 插入环节
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }

    // 单消费者出队
    std::optional<T> dequeue() {
        Node* old = head_.load(std::memory_order_relaxed);
        Node* next = old->next.load(std::memory_order_acquire);
        if (!next) return std::nullopt;

        // 移动 head
        head_.store(next, std::memory_order_release);
        std::optional<T> res = std::move(next->data);
        next->data.reset();

        // 回收旧节点到内存池
        TaggedPtr<Node> oldFS = freeStack_.load(std::memory_order_acquire);
        TaggedPtr<Node> desired{old, oldFS.tag + 1};
        do {
            old->next.store(oldFS.ptr, std::memory_order_relaxed);
        } while (!freeStack_.compare_exchange_strong(oldFS, desired));

        return res;
    }
};
