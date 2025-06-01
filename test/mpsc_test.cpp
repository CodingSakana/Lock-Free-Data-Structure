// test.cpp
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "mpsc_queue_v3.0.hpp"      // 把你的头文件路径填进来

// -------- 可调参数 --------
constexpr int  kNumProducers   = 8;          // 生产者数量
constexpr long kEnqueuePerProd = 5'000'000;  // 每个生产者 push 次数
// --------------------------

int main() {
    using namespace std::chrono;
    using namespace util;
    MPSCQueue<long> q;

    std::atomic<bool> go{false};
    std::atomic<long> produced{0};
    std::atomic<long> consumed{0};

    // 启动消费者（单线程）
    std::thread consumer([&] {
        while (!go.load(std::memory_order_acquire)) ;        // 等待开始

        for (;;) {
            auto val = q.dequeue();
            if (val) {
                ++consumed;
            } else if (produced.load(std::memory_order_acquire) ==
                       kNumProducers * kEnqueuePerProd) {
                // 所有生产者结束后，队列空 => 正常退出
                break;
            }
            // 让出 CPU，制造更多线程交叉
            std::this_thread::yield();
        }
    });

    // 启动多个生产者
    std::vector<std::thread> producers;
    for (int tid = 0; tid < kNumProducers; ++tid) {
        producers.emplace_back([&, tid] {
            while (!go.load(std::memory_order_acquire)) ;    // 同步起跑线

            for (long n = 0; n < kEnqueuePerProd; ++n) {
                // 唯一标识：高位 = 线程 id，低位 = 计数
                long value = (static_cast<long>(tid) << 48) | n;
                q.enqueue(value);
                ++produced;
                // 减少编译器优化，制造更多重用机会
                if ((n & 1023) == 0) std::this_thread::yield();
            }
        });
    }

    auto t0 = high_resolution_clock::now();
    go.store(true, std::memory_order_release);   // 发令

    for (auto &th : producers) th.join();
    consumer.join();
    auto t1 = high_resolution_clock::now();

    std::cout << "Produced = " << produced.load()
              << ", Consumed = " << consumed.load() << '\n';
    std::cout << "Elapsed  = "
              << duration_cast<milliseconds>(t1 - t0).count()
              << " ms\n";

    if (produced.load() != consumed.load()) {
        std::cerr << "\n❌  数据丢失/重复 —— 典型 ABA 造成结构损坏\n";
        return 1;
    }
    std::cout << "没出错；若你开启了 HazardPointer/taggedPtr 再跑一次对比\n";
    return 0;
}
