#pragma once

#include <atomic>
#include <cstddef>
#include <cassert>
#include "hazard_pointer.hpp"

static thread_local const std::size_t p_tid = hp::HazardManager::register_thread();

template <typename T>
class MemoryPool {
public:
    static constexpr std::size_t kBatchAllocate = 64;

    MemoryPool(): freelist_(nullptr) {}

    ~MemoryPool() {
        FreeListNode* node = freelist_.load();
        while (node) {
            FreeListNode* next = node->next;
            delete reinterpret_cast<T*>(node);
            node = next;
        }
    }

    T* acquire() {
        while (true) {
            FreeListNode* old_head = freelist_.load(std::memory_order_acquire);
            if (!old_head) {
                batch_allocate();
                continue;
            }

            // 在 hazardmanager 中注册保护指针
            hp::HazardManager::protect(p_tid, 0, old_head);

            // 重新读 freelist_ 头，看是否被别人 pop 过
            if (freelist_.load(std::memory_order_acquire) != old_head) {
                hp::HazardManager::release(p_tid, 0);
                continue; // retry
            }

            FreeListNode* next = old_head->next;
            if (freelist_.compare_exchange_weak(old_head, next,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
                hp::HazardManager::release(p_tid, 0);  // 使用完毕，取消 hazard
                return reinterpret_cast<T*>(old_head);
            }

            hp::HazardManager::release(p_tid, 0);
            // retry
        }
    }

    void release(T* obj) {
        auto* node = reinterpret_cast<FreeListNode*>(obj);
        FreeListNode* old_head = freelist_.load(std::memory_order_relaxed);
        do {
            node->next = old_head;
        } while (!freelist_.compare_exchange_weak(old_head, node,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed));
    }

private:
    struct FreeListNode {
        FreeListNode* next;
    };

    std::atomic<FreeListNode*> freelist_;

    void batch_allocate() {
        T* first = new T();
        FreeListNode* first_node = reinterpret_cast<FreeListNode*>(first);
        FreeListNode* prev = first_node;

        for (std::size_t i = 1; i < kBatchAllocate; ++i) {
            T* obj = new T();
            auto* node = reinterpret_cast<FreeListNode*>(obj);
            prev->next = node;
            prev = node;
        }

        FreeListNode* old_head;
        do {
            old_head = freelist_.load(std::memory_order_relaxed);
            prev->next = old_head;              // prev 已经是链表最后
        } while (!freelist_.compare_exchange_weak(old_head, first_node,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
    }
};
