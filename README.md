# Lock-Free Stack (C++)

This project implements a **lock-free stack** in C++ using modern atomic operations and `std::shared_ptr`. It serves as both a high-performance concurrent data structure and a learning tool for understanding lock-free programming fundamentals.

---

## ✨ Features

- [x] Fully thread-safe `push()` and `pop()` without using `std::mutex`  
- [x] Built using `std::atomic<std::shared_ptr<Node>>` for safe, concurrent updates  
- [x] Returns values with `std::optional<std::shared_ptr<T>>`  
- [x] No memory leaks (verified by Valgrind)  
- [x] Tested with single-threaded and multi-threaded workloads

---

## 📦 API Overview

```cpp
template<typename T>
class LockFreeStack {
public:
    void push(const T& value);
    std::optional<std::shared_ptr<T>> pop();
    bool empty() const;
};
```

---

## 🧠 Design Notes

- The stack uses a **single atomic head pointer** of type `std::atomic<std::shared_ptr<Node>>`.
- Each node contains a `std::shared_ptr<T>` as its data to:
  - Avoid data loss due to exceptions in `T`'s copy constructor
  - Support safe concurrent access and delayed destruction
- The core operations use **CAS (Compare-And-Swap)** via `compare_exchange_weak` to ensure atomic updates.

---

## 🚀 Build & Run Tests

### Compile (requires C++20):

```bash
g++ -std=c++20 -O2 -Iinclude test/test_stack_v1.0.cpp -o test_stack -Wall
```

### Run:

```bash
./test_stack
```

You should see:

```
Hardware concurrency: 4 threads
Single-thread test passed.
Multi-thread test passed. Total popped: 4000
```

---

## 🧼 Memory Safety

Memory safety and proper deallocation are ensured using `std::shared_ptr`.

Leak checking with **Valgrind**:

```bash
valgrind --leak-check=full ./test_stack
```

Example result:

```
All heap blocks were freed -- no leaks are possible
```

---

## 📁 Directory Structure

```
Lock-Free-Data-Structure/
├── include/
│   └── lockfree_stack.hpp
├── test/
│   └── test_stack_v1.0.cpp
├── README.md
```

---

## 📌 Future Work

- [ ] Lock-Free Queue
- [ ] Hazard Pointer support (for ABA mitigation)
- [ ] Wait-free variants
- [ ] Performance benchmarks

---

## 👨‍💻 Author

Created by [@sssakana](https://github.com/CodingSakana), inspired by Anthony Williams’ *C++ Concurrency in Action*.  
Edited on May 20 2025.

---