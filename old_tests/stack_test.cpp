#include <iostream>
#include <cassert>
#include <vector>
#include <future>
#include <unordered_set>
#include "stack_v1.0.hpp"

void singleThreadTest() {
    MPMCStack<int> stack;

    assert(stack.empty());

    stack.push(42);
    assert(!stack.empty());

    auto val = stack.pop(); // optional<shared_ptr<int>>
    assert(val.has_value());
    assert(*val); // shared_ptr 不为空
    assert(**val == 42); // 解两层拿到值

    assert(stack.empty());

    auto emptyVal = stack.pop();
    assert(!emptyVal.has_value());

    std::cout << "Single-thread test passed.\n";
}

void multiThreadTest() {
    LockFreeStack<int> stack;
    const int numThreads = 4;
    const int numPerThread = 1000;

    // 多线程 push
    std::vector<std::future<void>> pushTasks;
    for (int t = 0; t < numThreads; ++t) {
        pushTasks.emplace_back(std::async(std::launch::async, [&stack, t, numPerThread]() {
            for (int i = 0; i < numPerThread; ++i) {
                stack.push(t * numPerThread + i); // 保证唯一值
            }
        }));
    }

    for (auto& f : pushTasks) f.get(); // 等待 push 完成

    // 多线程 pop
    std::vector<std::future<std::vector<int>>> popTasks;
    for (int t = 0; t < numThreads; ++t) {
        popTasks.emplace_back(std::async(std::launch::async, [&stack, numPerThread]() {
            std::vector<int> popped;
            for (int i = 0; i < numPerThread; ++i) {
                auto val = stack.pop(); // optional<shared_ptr<int>>
                if (val && *val) {
                    popped.push_back(**val); // 解两层
                }
            }
            return popped;
        }));
    }

    std::unordered_set<int> allPopped;
    for (auto& f : popTasks) {
        for (int v : f.get()) {
            allPopped.insert(v);
        }
    }

    assert(allPopped.size() == numThreads * numPerThread);
    std::cout << "Multi-thread test passed. Total popped: " << allPopped.size() << "\n";
}

int main() {
    std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency() << " threads\n";
    singleThreadTest();
    multiThreadTest();
}