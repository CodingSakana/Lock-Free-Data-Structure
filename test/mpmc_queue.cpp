#include "lock-free_mpmc_queue_1.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <assert.h>
#include <unistd.h>
#include <csignal>

/*  lock-free:
 *  clang++ -std=c++20 -O3 -fsanitize=undefined,address -g test/mpmc_queue.cpp -o mpmc_queue_test -Iinclude -latomic
 *  clang++ -std=c++20 -O3 test/mpmc_queue.cpp -o mpmc_queue_test -Iinclude -latomic
 *  
 *  lock:
 *  clang++ -std=c++20 -O3 -fsanitize=undefined,address -g test/mpmc_queue.cpp -o mpmc_queue_test -Iinclude
 *  clang++ -std=c++20 -O3 test/mpmc_queue.cpp -o mpmc_queue_test -Iinclude
 */

std::atomic<bool> stop{false};  // ★ 收到信号后置 true

void signalHandler(int signal)
{
    if (signal == SIGINT) {
        // 第一次按下 Ctrl+C，设置退出标志，后续循环会检测到并退出
        stop.store(true);
        std::cout << "\n收到 SIGINT, 准备退出……\n";
        sleep(3);
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
    TestMPMCQueue Queue;
    
    const unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads < 2)
    {
        std::cerr << "Need ≥2 hardware threads\n";
        return 1;
    }

    // 生产者线程数 = 硬件线程数 / 2；消费者线程数 = 剩余
    const unsigned prod_cnt = hw_threads / 2;
    const unsigned cons_cnt = hw_threads - prod_cnt;

    /*------------------ 启动生产者 -------------------*/
    std::atomic<std::size_t> enqueued{0};
    std::vector<std::thread> producers;
    producers.reserve(prod_cnt);

    for (unsigned p = 0; p < prod_cnt; ++p)
    {
        // 每个生产者负责 base + [0 .. OPS_PER_THREAD)
        producers.emplace_back([&, base = p * OPS_PER_THREAD] {
            for (std::size_t i = 0; i < OPS_PER_THREAD && !stop.load(); ++i) {
                // enqueue 接口：把整数值放入队列
                Queue.enqueue(static_cast<int>(base + i));
                enqueued.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    /*------------------ 启动消费者 -------------------*/
    std::atomic<std::size_t> dequeued{0};
    std::atomic<std::size_t> emptytimes{0};
    std::vector<std::thread> consumers;
    consumers.reserve(cons_cnt);

    for (unsigned c = 0; c < cons_cnt; ++c)
    {
        consumers.emplace_back([&] {
            // 每个消费者循环，直到所有项都被弹出或收到 stop 信号
            while (dequeued.load(std::memory_order_relaxed) < OPS_PER_THREAD * prod_cnt && !stop.load())
            {
                // dequeue 接口：从队列取出一个元素，返回 std::optional<int> 或 空指针
                auto opt = Queue.dequeue();
                if (opt) {
                    // 成功弹出
                    dequeued.fetch_add(1, std::memory_order_acq_rel);
                }
                else {
                    // 队列暂时为空，忙等时稍作让出
                    emptytimes.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                }
            }
        });
    }

    // 计时开始
    const auto t0 = steady_clock::now();

    if (SLEEP_TIME > 0) {
        std::this_thread::sleep_for(seconds(SLEEP_TIME));
        stop.store(true, std::memory_order_relaxed);  // ★ 通知所有线程收尾
    }

    // 等待所有线程退出
    for (auto& th : producers) th.join();
    for (auto& th : consumers) th.join();

    const auto t1 = steady_clock::now();
    const double elapsed = duration<double>(t1 - t0).count();

    // 输出测试结果
    std::cout << "Threads   : " << hw_threads << " ("
              << prod_cnt << "P/" << cons_cnt << "C)\n";
    std::cout << "Enqueued  : " << enqueued.load() << '\n';
    std::cout << "Dequeued  : " << dequeued.load() << '\n';
    std::cout << "Deq empty : " << emptytimes.load() << '\n';
    std::cout << "Elapsed   : " << elapsed << " s\n";
    std::cout << "Throughput: "
              << static_cast<double>(dequeued.load()) / elapsed / 1'000'000
              << " M ops/s\n";

    return 0;
}
