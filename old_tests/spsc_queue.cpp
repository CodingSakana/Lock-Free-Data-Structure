#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cassert>
#include "lock_spsc_queue.hpp"

constexpr size_t N = 1024;
SPSCQueue<int, N> queue;

std::atomic<bool> stop_flag{false};
std::atomic<size_t> produced{0};
std::atomic<size_t> consumed{0};

void producer_thread() {
    int value = 0;
    while (!stop_flag.load()) {
        if (queue.enqueue(value)) {
            ++produced;
            ++value;
        }
        // 否则队列满了，忙等
    }
}

void consumer_thread() {
    int last = -1;
    while (!stop_flag.load()) {
        auto result = queue.dequeue();
        if (result.has_value()) {
            int val = result.value();
            // 验证数据连续性
            if (val != last + 1) {
                std::cerr << "Out-of-order: expected " << (last + 1) << ", got " << val << std::endl;
                std::exit(1);
            }
            last = val;
            ++consumed;
        }
        // 否则队列空了，忙等
    }
}

int main() {
    std::cout << "Starting SPSC test (5 seconds)..." << std::endl;

    auto start = std::chrono::steady_clock::now();
    std::thread prod(producer_thread);
    std::thread cons(consumer_thread);

    std::this_thread::sleep_for(std::chrono::seconds(5));
    stop_flag.store(true);

    prod.join();
    cons.join();

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout << "Finished.\n";
    std::cout << "Produced: " << produced.load() << "\n";
    std::cout << "Consumed: " << consumed.load() << "\n";
    std::cout << "Throughput: " << (consumed.load() / duration.count() / 1e6) << " million ops/sec\n";

    return 0;
}
