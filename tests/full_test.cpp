// full_test.cpp
// 功能测试，仅验证正确性，不做性能测试。
// 编译示例：
//   g++ -std=c++20 -pthread -Iinclude -fsanitize=address,undefined tests/full_test.cpp -o full_test_asan -latomic
//   g++ -std=c++20 -pthread -Iinclude -fsanitize=thread,undefined tests/full_test.cpp -o full_test_tsan -latomic

#include <atomic>
#include <cassert>
#include <iostream>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include "lock-free_mpsc_queue.h"
#include "lock-free_spsc_queue.h"
#include "lock_mpsc_queue.h"
#include "lock_spsc_queue.h"

void test_spsc_functional() {
    constexpr int CAP = 16;
    // 无锁 SPSC 环形缓冲可用元素 = CAP-1
    {
        LockFreeSPSCQueue<int, CAP> q;
        for (int i = 0; i < CAP - 1; ++i) {
            assert(q.enqueue(i));
        }
        assert(!q.enqueue(12345)); // 满
        for (int i = 0; i < CAP - 1; ++i) {
            auto v = q.dequeue();
            assert(v.has_value() && v.value() == i);
        }
        assert(!q.dequeue().has_value());
    }
    // 有锁 SPSC 队列可用元素 = CAP
    {
        LockSPSCQueue<int, CAP> q;
        for (int i = 0; i < CAP; ++i) {
            assert(q.enqueue(i));
        }
        assert(!q.enqueue(54321)); // 满
        for (int i = 0; i < CAP; ++i) {
            auto v = q.dequeue();
            assert(v.has_value() && v.value() == i);
        }
        assert(!q.dequeue().has_value());
    }
    std::cout << "[PASS] SPSC 功能测试\n";
}

void test_mpsc_functional() {
    constexpr int PRODUCERS = 4;
    constexpr int ITEMS_PER_P = 1000;
    // Lock-free MPSC
    {
        LockFreeMPSCQueue<int> q;
        std::vector<std::thread> pros;
        for (int p = 0; p < PRODUCERS; ++p) {
            pros.emplace_back([&, p]() {
                for (int i = 0; i < ITEMS_PER_P; ++i) {
                    q.enqueue(p * ITEMS_PER_P + i);
                }
            });
        }
        std::thread cons([&]() {
            std::set<int> seen;
            for (int i = 0; i < PRODUCERS * ITEMS_PER_P; ++i) {
                std::optional<int> v;
                while (!(v = q.dequeue()))
                    std::this_thread::yield();
                seen.insert(*v);
            }
            assert((int)seen.size() == PRODUCERS * ITEMS_PER_P);
        });
        for (auto& t : pros)
            t.join();
        cons.join();
    }
    // Lock-based MPSC
    {
        LockMPSCQueue<int, PRODUCERS * ITEMS_PER_P> q;
        std::vector<std::thread> pros;
        for (int p = 0; p < PRODUCERS; ++p) {
            pros.emplace_back([&, p]() {
                for (int i = 0; i < ITEMS_PER_P; ++i) {
                    while (!q.enqueue(p * ITEMS_PER_P + i))
                        std::this_thread::yield();
                }
            });
        }
        std::thread cons([&]() {
            std::set<int> seen;
            for (int i = 0; i < PRODUCERS * ITEMS_PER_P; ++i) {
                std::optional<int> v;
                while (!(v = q.dequeue()))
                    std::this_thread::yield();
                seen.insert(*v);
            }
            assert((int)seen.size() == PRODUCERS * ITEMS_PER_P);
        });
        for (auto& t : pros)
            t.join();
        cons.join();
    }
    std::cout << "[PASS] MPSC 功能测试\n";
}

int main() {
    test_spsc_functional();
    test_mpsc_functional();
    std::cout << "全部功能测试通过！\n";
    return 0;
}
