// performance_test.cpp
// 性能测试，仅做 throughput 对比，并计算 speedup，不做功能断言。
// 编译示例：
//   g++ -std=c++20 -pthread -Iinclude -O3 -march=native tests/performance_test.cpp -o perf_test

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include "lock-free_spsc_queue.h"
#include "lock_spsc_queue.h"
#include "lock-free_mpsc_queue.h"
#include "lock_mpsc_queue.h"

using Clock = std::chrono::high_resolution_clock;

template<class F>
double bench(F f) {
    auto t0 = Clock::now();
    f();
    auto t1 = Clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

void perf_spsc() {
    constexpr int OPS = 1'000'000;
    // Lock-free
    double t_lf = bench([&](){
        LockFreeSPSCQueue<int, 1024> q;
        std::thread prod([&](){
            for (int i = 0; i < OPS; ++i) while (!q.enqueue(i));
        });
        std::thread cons([&](){
            int cnt = 0;
            while (cnt < OPS) {
                auto v = q.dequeue();
                if (v) ++cnt;
            }
        });
        prod.join(); cons.join();
    });
    // Lock-based
    double t_lk = bench([&](){
        LockSPSCQueue<int, 1024> q;
        std::thread prod([&](){
            for (int i = 0; i < OPS; ++i) while (!q.enqueue(i));
        });
        std::thread cons([&](){
            int cnt = 0;
            while (cnt < OPS) {
                auto v = q.dequeue();
                if (v) ++cnt;
            }
        });
        prod.join(); cons.join();
    });
    std::cout << "SPSC queue: lock-free = " << t_lf << " s, lock = " << t_lk
              << " s, speedup = " << (t_lk / t_lf) << "x\n";
}

void perf_mpsc() {
    constexpr int PRODUCERS = 4;
    constexpr int OPS = 250'000;
    double t_lf = bench([&](){
        LockFreeMPSCQueue<int> q;
        std::vector<std::thread> pros;
        for (int p = 0; p < PRODUCERS; ++p)
            pros.emplace_back([&](){ for (int i = 0; i < OPS; ++i) q.enqueue(i); });
        std::thread cons([&](){
            int cnt = 0;
            while (cnt < PRODUCERS * OPS) {
                auto v = q.dequeue();
                if (v) ++cnt;
            }
        });
        for (auto &t : pros) t.join();
        cons.join();
    });
    double t_lk = bench([&](){
        LockMPSCQueue<int, PRODUCERS * OPS> q;
        std::vector<std::thread> pros;
        for (int p = 0; p < PRODUCERS; ++p)
            pros.emplace_back([&](){ for (int i = 0; i < OPS; ++i) while(!q.enqueue(i)); });
        std::thread cons([&](){
            int cnt = 0;
            while (cnt < PRODUCERS * OPS) {
                auto v = q.dequeue();
                if (v) ++cnt;
            }
        });
        for (auto &t : pros) t.join();
        cons.join();
    });
    std::cout << "MPSC queue: lock-free = " << t_lf << " s, lock = " << t_lk
              << " s, speedup = " << (t_lk / t_lf) << "x\n";
}

int main() {
    std::cout << "=== 性能测试开始 ===\n";
    perf_spsc();
    perf_mpsc();
    std::cout << "=== 性能测试结束 ===\n";
    return 0;
}
