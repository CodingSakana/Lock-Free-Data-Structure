#include <hazard_pointer.hpp>
#include <memory_pool.hpp>

inline int HazardPointerManager::acquire_thread_index(){
    if (g_thread_index != -1) return g_thread_index;

    const std::thread::id my_id = std::this_thread::get_id();
    for (int i = 0; i < kMaxThreads; ++i) {
        std::thread::id zero; // 默认空 id
        if (g_hazard_slots[i][0].id.compare_exchange_strong(zero, my_id)) {
            g_thread_index = i;
            return i;
        }
    }
    throw std::runtime_error("No available hazard pointer slot for thread.");
}

void HazardPointerManager::protect(int thread_index, int slot, void* ptr){
    g_hazard_slots[thread_index][slot].pointer.store(ptr, std::memory_order_release);
}

void HazardPointerManager::release(int thread_index, int slot){
    g_hazard_slots[thread_index][slot].pointer.store(nullptr, std::memory_order_release);
}

void HazardPointerManager::retire(void* ptr, std::function<void(void*)> deleter) {
    auto* node = new RetiredNode{ptr, std::move(deleter), retired_list};
    retired_list = node;
    ++retired_count;
    if(retired_count >= kScanThreshold){
        scan();
    }
}

void HazardPointerManager::scan() {
    // 收集所有被保护的指针
    std::unordered_set<void*> protected_ptrs;

    for (int i = 0; i < kMaxThreads; ++i) {
        for (int j = 0; j < kSlotsPerThread; ++j) {
            void* ptr = g_hazard_slots[i][j].pointer.load(std::memory_order_acquire);
            if (ptr != nullptr){
                protected_ptrs.insert(ptr);
            }
        }
    }

    // Step 2: 扫描 retired_list，尝试安全删除
    RetiredNode* prev = nullptr;
    RetiredNode* curr = retired_list;
    retired_list = nullptr;  // 准备新链表
    retired_count = 0;

    while (curr != nullptr) {
        RetiredNode* next = curr->next;

        if (protected_ptrs.count(curr->ptr) == 0) {
            curr->deleter(curr->ptr);
            delete curr;
        }else{
            curr->next = retired_list;
            retired_list = curr;
            ++retired_count;
        }

        curr = next;
    }
}
