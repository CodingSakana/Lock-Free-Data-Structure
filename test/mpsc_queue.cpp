#include "lock-free_mpmc_queue.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <assert.h>
#include <unistd.h>
#include <csignal>

/*  g++ -std=c++20 -O3 -march=native -pthread test/mpmc_Queue.cpp -o mpmc_Queue_test -Iinclude -latomic
 *  ≈  
 *  g++ -std=c++20 -O3 -fsanitize=undefined,address -g test/mpmc_Queue.cpp -o mpmc_Queue_test -Iinclude -latomic
 *  (使用 TSAN / ASAN 运行可捕获数据竞争或内存泄漏)
 * 
 *  g++ -std=c++20 -O3 test/mpmc_Queue.cpp -o mpmc_Queue_test -Iinclude -latomic
 */

// constexpr std::size_t OPS_PER_THREAD = 1'000;
// constexpr std::size_t SLEEP_TIME = 5;

std::atomic<bool> stop{false};            // ★ 10 秒后置 true

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
    std::size_t SLEEP_TIME = std::atoi(argv[2]);
    using namespace std::chrono;
    TestMPMCQueue Queue;
    const unsigned hw_threads = std::thread::hardware_concurrency();
    if (hw_threads < 2)
    {
        std::cerr << "Need ≥2 hardware threads\n";
        return 1;
    }

    const unsigned prod_cnt = hw_threads / 2;
    const unsigned cons_cnt = hw_threads - prod_cnt;

    /*------------------ 启动生产者 -------------------*/
    std::atomic<std::size_t> enqueued{0};
    std::vector<std::thread> producers;
    for (unsigned p = 0; p < prod_cnt; ++p)
    {
        producers.emplace_back([&, base = p * OPS_PER_THREAD] {
            for (std::size_t i = 0; i < OPS_PER_THREAD && !stop; ++i){
                Queue.enqueue(static_cast<int>(base + i));
                enqueued++;
            }
        });
    }

    // sleep(3);
    // for (auto& th : producers)  th.join();
    // std::cout << "length: " << Queue.length() << "\n";

    /*------------------ 启动消费者 -------------------*/
    std::atomic<std::size_t> popped{0};
    std::atomic<std::size_t> emptytimes{0};
    std::vector<std::thread> consumers;
    for (unsigned c = 0; c < cons_cnt; ++c)
    {
        consumers.emplace_back([&] {
            while (popped < OPS_PER_THREAD * cons_cnt && !stop)
            {
                auto item = Queue.pop();
                if (item)
                    ++popped;
                /* 若队列暂空，稍作让出可减少忙等 */
                else{
                    emptytimes++;
                    std::this_thread::yield();
                }
            }
        });
    }

    const auto t0 = steady_clock::now();
    if(SLEEP_TIME){
        std::this_thread::sleep_for(seconds(SLEEP_TIME));
        stop.store(true, std::memory_order_relaxed);     // ★ 通知全部线程收尾
    }

    for (auto& th : producers)  th.join();
    for (auto& th : consumers)  th.join();

    const auto t1 = steady_clock::now();
    const double seconds = duration<double>(t1 - t0).count();

    std::cout << "Threads   : " << hw_threads << " ("
              << prod_cnt << "P/" << cons_cnt << "C)\n";
    std::cout << "enqueued    : " << enqueued << '\n';
    std::cout << "Popped    : " << popped << '\n';
    std::cout << "Pop failed: " << emptytimes << '\n';
    std::cout << "Elapsed   : " << seconds << " s\n";
    std::cout << "Throughput: "
              << static_cast<double>(popped) / seconds / 1'000'000
              << " M ops/s\n";

#ifdef _MSC_VER
    /* 若在 MSVC，启用 CRT leak-check：
       在 main 末尾加 _CrtDumpMemoryLeaks(); 并
       #include <crtdbg.h> + _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|
                                            _CRTDBG_LEAK_CHECK_DF); */
#endif
    return 0;
}
