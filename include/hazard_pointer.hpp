// 风险指针的实现

#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <functional>
#include <array>
#include <cassert>
#include <stdexcept>

// ==================== ThreadLocalPool ====================
template <typename T>
class ThreadLocalPool{
public:
    static constexpr std::size_t kBatchAllocate = 64;
    T* acquire(){
        if (!g_freelist_){
            for(int i = 0; i < kBatchAllocate; ++i){
                T* new_node = new T();
                new_node->next = g_freelist_;
                g_freelist_ = new_node;
            }
        }
        T* node = g_freelist_;
        g_freelist_ = g_freelist_->next;
        return node;
    }

    void release(T* node){
        node->next = g_freelist_;
        g_freelist_ = node;
    }

private:
    // 每线程一个 freelist，避免锁
    thread_local static T* g_freelist_;
};

template <typename T>
thread_local T* ThreadLocalPool<T>::g_freelist_ = nullptr;

// ===================== configuration =====================
inline constexpr std::size_t kMaxThreads        = 256;   // 最大线程数
inline constexpr std::size_t kHazardsPerThread  = 6;     // 每个线程的最大风险指针数
inline constexpr std::size_t kScanThreshold     = 32;

// ===================== hazard slot & owner =====================
struct alignas(64) HazardSlot {                // 64 位对齐
    std::atomic<void*> ptr{nullptr};
};

// MOD: per‑owner struct aligned to 64 B to eliminate false sharing between thread registrations
struct alignas(64) ThreadOwner {
    std::atomic<std::thread::id> id{};
};

// global arrays -------------------------------------------------
inline ThreadOwner g_owner[kMaxThreads];

// MOD: make each row (one thread) aligned to 64 so its 4 slots share a line but not with others
struct alignas(64) HazardRow {
    HazardSlot slots[kHazardsPerThread];
};
inline HazardRow g_slots[kMaxThreads];

namespace hp {
// ===================== hazard manager =====================
class HazardManager {
using Deleter = void(*)(void*);

private:
    struct RetiredNode {
        void* ptr{nullptr};
        Deleter deleter;
        RetiredNode* next{nullptr};
    };

    struct ThreadExitCleaner {
        ~ThreadExitCleaner() {
            if (tl_tid != -1) {
                g_owner[tl_tid].id.store({}, std::memory_order_relaxed);
            }
        }
    };

    // thread‑local pool instance for RetiredNode
    static inline ThreadLocalPool<RetiredNode> g_retired_pool;
    static inline thread_local ThreadExitCleaner cleaner;

    // thread‑local bookkeeping ------------------------------
    static inline thread_local RetiredNode*  tl_retired_head  = nullptr;
    static inline thread_local std::size_t   tl_retired_count = 0;
    static inline thread_local std::size_t   tl_tid = static_cast<std::size_t>(-1);

    // helpers -----------------------------------------------
    static void scan();

public:
    static std::size_t register_thread();
    static void protect (std::size_t tid, std::size_t slot, void* p) noexcept;
    static void release (std::size_t tid, std::size_t slot)          noexcept;
    static void retire  (void* p, Deleter deleter);
    static void bulk_release(std::size_t tid) noexcept;              // MOD: new API – clear all slots of a thread
};

// ===================== implementation =====================

inline std::size_t HazardManager::register_thread() {
    if (tl_tid != static_cast<std::size_t>(-1))
        return tl_tid;                                   // already registered

    const std::thread::id me = std::this_thread::get_id();
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        std::thread::id zero{};
        if (g_owner[i].id.compare_exchange_strong(zero, me, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            tl_tid = i;
            // MOD: clear any stale hazard pointers left by a previous thread using this slot
            for (std::size_t j = 0; j < kHazardsPerThread; ++j)
                g_slots[i].slots[j].ptr.store(nullptr, std::memory_order_relaxed);
            return i;
        }
    }
    throw std::runtime_error("HazardManager: exhausted thread slots — increase kMaxThreads");
}

inline void HazardManager::protect(std::size_t tid, std::size_t slot, void* p) noexcept {
    assert(tid < kMaxThreads && slot < kHazardsPerThread);
    g_slots[tid].slots[slot].ptr.store(p, std::memory_order_release);
}

inline void HazardManager::release(std::size_t tid, std::size_t slot) noexcept {
    assert(tid < kMaxThreads && slot < kHazardsPerThread);
    g_slots[tid].slots[slot].ptr.store(nullptr, std::memory_order_relaxed);
}

// MOD: new helper – clear all slots quickly without building hazard array
inline void HazardManager::bulk_release(std::size_t tid) noexcept {
    for (std::size_t j = 0; j < kHazardsPerThread; ++j)
        g_slots[tid].slots[j].ptr.store(nullptr, std::memory_order_relaxed);
}

inline void HazardManager::retire(void* p, Deleter deleter) {
    auto* node    = g_retired_pool.acquire();
    node->ptr     = p;
    node->deleter = deleter;
    node->next    = tl_retired_head;
    tl_retired_head = node;

    if (++tl_retired_count >= kScanThreshold)
        scan();
}

inline void HazardManager::scan() {
    // 1) collect all protected pointers into a flat array
    std::array<void*, kMaxThreads * kHazardsPerThread> hazards{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        for (std::size_t j = 0; j < kHazardsPerThread; ++j) {
            void* p = g_slots[i].slots[j].ptr.load(std::memory_order_acquire);
            if (p) hazards[count++] = p;
        }
    }

    // 2) traverse retired list – delete or keep
    RetiredNode* curr = tl_retired_head;
    tl_retired_head   = nullptr;
    tl_retired_count  = 0;

    while (curr) {
        RetiredNode* next = curr->next;
        bool hazard = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (hazards[i] == curr->ptr) { hazard = true; break; }
        }

        if (!hazard) {
            curr->deleter(curr->ptr);                // delete user object
            g_retired_pool.release(curr);            // MOD: return RetiredNode to pool (no new/delete)
        } else {
            curr->next = tl_retired_head;            // remain in list for next scan
            tl_retired_head = curr;
            ++tl_retired_count;
        }
        curr = next;
    }
}

} // namespace hp
