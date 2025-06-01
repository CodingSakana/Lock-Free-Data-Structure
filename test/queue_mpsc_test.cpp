#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include "mpsc_queue_v2.0.hpp"

struct Item {
    int producer_id;
    int sequence;
};

MPSCQueue<Item> queue;

constexpr int PRODUCER_COUNT = 4;
constexpr int TEST_SECONDS = 5;

std::atomic<bool> stop_flag{false};
std::atomic<size_t> total_produced{0};
std::atomic<size_t> total_consumed{0};

// 每个生产者使用自己的序列号递增
void producer_thread(int id) {
    int seq = 0;
    while (!stop_flag.load(std::memory_order_relaxed)) {
        queue.enqueue(Item{ id, seq++ });
        total_produced.fetch_add(1, std::memory_order_relaxed);
    }
}

// 消费者记录每个 producer 的上一个值，检查递增性
void consumer_thread() {
    std::unordered_map<int, int> last_seq;
    while (!stop_flag.load(std::memory_order_relaxed)) {
        auto res = queue.dequeue();
        if (res.has_value()) {
            const auto& item = *res;
            ++total_consumed;

            // 检查 per-producer 序列是否递增
            if (last_seq.count(item.producer_id)) {
                if (item.sequence <= last_seq[item.producer_id]) {
                    std::cerr << "Out-of-order from producer "
                              << item.producer_id << ": got " << item.sequence
                              << ", last was " << last_seq[item.producer_id]
                              << std::endl;
                    std::exit(1);
                }
            }
            last_seq[item.producer_id] = item.sequence;
        }
    }
}

int main() {
    std::cout << "Running MPSC correctness test (" << PRODUCER_COUNT << " producers)...\n";

    std::vector<std::thread> producers;
    for (int i = 0; i < PRODUCER_COUNT; ++i) {
        producers.emplace_back(producer_thread, i);
    }
    std::thread consumer(consumer_thread);

    std::this_thread::sleep_for(std::chrono::seconds(TEST_SECONDS));
    stop_flag.store(true);

    for (auto& t : producers) t.join();
    consumer.join();

    std::cout << "Test complete.\n";
    std::cout << "Produced: " << total_produced.load() << "\n";
    std::cout << "Consumed: " << total_consumed.load() << "\n";
    std::cout << "Throughput: "
              << (total_consumed.load() / (double)TEST_SECONDS) / 1e6
              << " million ops/sec\n";

    return 0;
}
