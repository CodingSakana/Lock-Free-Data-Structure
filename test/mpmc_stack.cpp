#include "lock-free_mpmc_stack_CCIA.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <assert.h>
#include <unistd.h>
#include <csignal>

/*  g++ -std=c++20 -O3 -march=native -pthread test/mpmc_Stack.cpp -o mpmc_Stack_test -Iinclude -latomic
 *  ≈  
 *  g++ -std=c++20 -O3 -fsanitize=undefined,address -g test/mpmc_Stack.cpp -o mpmc_Stack_test -Iinclude -latomic
 *  (使用 TSAN / ASAN 运行可捕获数据竞争或内存泄漏)
 * 
 *  g++ -std=c++20 -O3 test/mpmc_stack.cpp -o mpmc_stack_test -Iinclude -latomic
 */

std::atomic<bool> stop{false};  // ★ 收到信号后置 true

void signalHandler(int signal)
{
    if (signal == SIGINT) {
        // 第一次按下 Ctrl+C，设置退出标志，后续循环会检测到并退出
        stop.store(true);
        std::cout << "\n收到 SIGINT, 准备退出……\n";
        exit(1);
    }
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    std::signal(SIGINT, signalHandler);
    std::size_t OPS_PER_THREAD = std::atoi(argv[1]);
    std::size_t SLEEP_TIME     = std::atoi(argv[2]);
    using namespace std::chrono;
    TestMPMCStack Stack;
    const unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads < 2)
    {
        std::cerr << "Need ≥2 hardware threads\n";
        return 1;
    }

    const unsigned prod_cnt = hw_threads / 2;
    const unsigned cons_cnt = hw_threads - prod_cnt;

    /*------------------ 启动生产者 -------------------*/
    std::atomic<std::size_t> pushed{0};
    std::vector<std::thread> producers;
    for (unsigned p = 0; p < prod_cnt; ++p)
    {
        producers.emplace_back([&, base = p * OPS_PER_THREAD] {
            for (std::size_t i = 0; i < OPS_PER_THREAD && !stop.load(); ++i) {
                Stack.push(static_cast<int>(base + i));
                pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    /*------------------ 启动消费者 -------------------*/
    std::atomic<std::size_t> popped{0};
    std::atomic<std::size_t> emptytimes{0};
    std::vector<std::thread> consumers;
    for (unsigned c = 0; c < cons_cnt; ++c)
    {
        consumers.emplace_back([&] {
            while (popped.load(std::memory_order_relaxed) < OPS_PER_THREAD * prod_cnt && !stop.load())
            {
                auto opt = Stack.pop();
                if (opt) {
                    popped.fetch_add(1, std::memory_order_acq_rel);
                }
                else {
                    emptytimes.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                }
            }
        });
    }

    const auto t0 = steady_clock::now();
    if (SLEEP_TIME > 0) {
        std::this_thread::sleep_for(seconds(SLEEP_TIME));
        stop.store(true, std::memory_order_relaxed);  // ★ 通知全部线程收尾
    }

    for (auto& th : producers) th.join();
    for (auto& th : consumers) th.join();

    const auto t1      = steady_clock::now();
    const double elapsed = duration<double>(t1 - t0).count();

    std::cout << "Threads   : " << hw_threads << " ("
              << prod_cnt << "P/" << cons_cnt << "C)\n";
    std::cout << "Pushed    : " << pushed.load() << '\n';
    std::cout << "Popped    : " << popped.load() << '\n';
    std::cout << "Deq empty : " << emptytimes.load() << '\n';
    std::cout << "Elapsed   : " << elapsed << " s\n";
    std::cout << "Throughput: "
              << static_cast<double>(popped.load()) / elapsed / 1'000'000
              << " M ops/s\n";

    return 0;
}
