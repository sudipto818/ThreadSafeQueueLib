<div align="center">

# ThreadSafeQueueLib
**Exploring Concurrency and Lock-Free Data Structures in Modern C++**

[![C++23](https://img.shields.io/badge/C++-23-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B23)
[![CMake](https://img.shields.io/badge/CMake-Build-success.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

*A group learning project by **[Badri Bishal Das](https://github.com/badri41)** , **[Sujal Patnaik](https://github.com/Sujal-Patnaik)** , **[Sudipto Ghosh](https://github.com/sudipto818)** , **[Biswabhusan Samal](https://github.com/biswa2006)** at IIT Guwahati*

[Read Blog Here](https://www.badribishaldas.in/blog/threadSafeQueue/)

</div>

---

## Hello!

**ThreadSafeQueueLib** is a group project created to dive deep into C++ and concurrency concepts. Instead of just reading about threads, locks, and atomics, we decided to build a family of thread-safe queues from scratch. 

Our goal wasn't to write the next big production library, but to really understand what makes concurrent programming so challenging and rewarding. A standard `std::queue` isn't safe to use when multiple threads are pushing and popping at the same time. To solve this, we implemented both blocking (mutex-based) and lock-free queues.

---

## What We Built

We explored different queue designs depending on how many threads are involved:
- **spsc** (Single-Producer Single-Consumer) - Lock-free versions (both bounded and unbounded).
- **mpsc** (Multi-Producer Single-Consumer) - Unbounded lock-free.
- **mpmc** (Multi-Producer Multi-Consumer) - Both traditional locking and lock-free implementations.

We used C++ templates to write this as a header-only library, allowing the compiler to optimize the queues at build time depending on the types of data being stored.

## How to use this? 

prerequisites : C++23, CMake

### Integration
You can easily pull this into your own CMake projects, or clone the Git repo directly:

```cmake
include(FetchContent)
FetchContent_Declare(
    tsqlib
    GIT_REPOSITORY https://github.com/badri41/ThreadSafeQueueLib.git
    GIT_TAG main
)
FetchContent_MakeAvailable(tsqlib)

target_link_libraries(your_target PRIVATE ThreadSafeQueueLib)
```

### A Quick Example

```cpp
#include <iostream>
#include <thread>
#include <tsfqueue.hpp> // Main header

int main() {
    // A queue where one thread pushes and one thread pops
    tsfqueue::spscUnbounded<int> queue;

    std::thread producer([&]() {
        for (int i = 0; i < 1000; ++i) {
            queue.push(i);
        }
    });

    std::thread consumer([&]() {
        int value;
        for (int i = 0; i < 1000; ++i) {
            // Keep trying to pop until we get a value
            while (!queue.tryPop(value)) {
                std::this_thread::yield(); 
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "All done without data races!\n";
    return 0;
}
```

---

## Building and Testing

We used **GoogleTest** and Clang's **ThreadSanitizer** to make sure our lock-free logic actually works and doesn't contain hidden data races.

```bash
mkdir build && cd build
cmake ..
cmake --build . -j4
ctest --output-on-failure -j4
```

---
## Benchmarking

**benchThroughput.cpp**
To measure the raw throughput (operations per second) of the different queue architectures, we included a standalone, dependency-free benchmarking tool. It evaluates how each queue scales under different levels of thread contention by varying producers and consumers from 1 up to 16 threads.

To run the benchmarks with maximum performance, compile the source using the -O3 optimization flag and the C++23 standard:

```bash
# Compile the benchmark
g++ -O3 -std=c++23 -pthread -I./include benchmarking/benchThroughput.cpp -o benchThroughput.exe

# Run the executable
.\benchThroughput.exe  //In powershell
```

**benchLatency.cpp**
While throughput measures *how many* items are processed, latency measures *how fast* a single item travels from a producer to a consumer. To measure this, our benchmark pushes ultra-precise nanosecond timestamps through the queues and calculates the transit time.

Because averages can be heavily skewed by OS-level background noise or thread context-switching, the benchmark sorts millions of operations to provide industry-standard **Percentile Metrics** (measured in microseconds, $\mu s$):
* **p50 (Median):** The typical, everyday performance of the queue.
* **p99 & p99.9 (Tail Latency):** The absolute worst-case scenarios. In blocking queues, this number spikes massively due to "Lock Convoys" (the OS pausing threads to wait for a `std::mutex`). In lock-free queues, this number stays incredibly low.

```bash
# Compile the benchmark
g++ -O3 -std=c++23 -pthread -I./include benchmarking/benchLatency.cpp -o benchLatency.exe

# Run the executable
.\benchLatency.exe  //In powershell
```

*The commands will print live throughput results to the terminal and save a formatted table to benchmarkResults.txt and latencyResults.txt in the current directory.*

### Benchmark Results Summary

Here are the benchmarking results generated on our test system.

#### 1. spsc (Single-Producer Single-Consumer)
Because there is no contention between multiple producers or multiple consumers, SPSC architectures achieve incredibly high throughput. However, to truly prove the value of lock-free programming, we benchmarked our lock-free implementations against a traditional `std::mutex` + `std::queue`.

<div align="center">
  <img src="https://quickchart.io/chart?c=%7B%22type%22%3A%22bar%22%2C%22data%22%3A%7B%22labels%22%3A%5B%22Unbounded%20Lock-Free%22%2C%22Bounded%20Lock-Free%22%2C%22Mutex%20Baseline%22%5D%2C%22datasets%22%3A%5B%7B%22label%22%3A%22Throughput%20-%201KB%20Payload%20(Millions%20Ops%2Fsec)%22%2C%22data%22%3A%5B5.8%2C11.2%2C6.8%5D%2C%22backgroundColor%22%3A%22rgba(75%2C%20192%2C%20192%2C%200.8)%22%7D%5D%7D%2C%22options%22%3A%7B%22title%22%3A%7B%22display%22%3Atrue%2C%22text%22%3A%22SPSC%20Throughput%20Comparison%20(1KB%20Payload)%22%7D%2C%22scales%22%3A%7B%22yAxes%22%3A%5B%7B%22ticks%22%3A%7B%22beginAtZero%22%3Atrue%7D%7D%5D%7D%7D%7D" alt="SPSC Throughput vs Mutex" width="45%" />
  <img src="https://quickchart.io/chart?c=%7B%22type%22%3A%22bar%22%2C%22data%22%3A%7B%22labels%22%3A%5B%22p50%20(Median)%22%2C%22p99%22%2C%22p99.9%20(Worst%20Case)%22%5D%2C%22datasets%22%3A%5B%7B%22label%22%3A%22Transit%20Latency%20(%CE%BCs)%22%2C%22data%22%3A%5B0.7%2C99.2%2C196.4%5D%2C%22backgroundColor%22%3A%22rgba(153%2C%20102%2C%20255%2C%200.8)%22%7D%5D%7D%2C%22options%22%3A%7B%22title%22%3A%7B%22display%22%3Atrue%2C%22text%22%3A%22SPSC%20Transit%20Latency%20Distribution%22%7D%2C%22scales%22%3A%7B%22yAxes%22%3A%5B%7B%22ticks%22%3A%7B%22beginAtZero%22%3Atrue%7D%7D%5D%7D%7D%7D" alt="SPSC Latency Distribution Chart" width="45%" />
</div>

**The Lock-Free Advantage (Payload Scalability):**
- For very small payloads (4 Bytes), a traditional `std::mutex` queue performs extremely well (up to 20M Ops/sec) due to modern OS futex optimizations and `std::deque` block allocation.
- However, as payload size increases to **1KB**, the critical section inside the mutex grows, causing the producer and consumer to lock each other out during data copies.
- In contrast, our **Bounded Lock-Free SPSC** queue shines under heavy payloads, sustaining **~11.2M Ops/sec**. By eliminating locks, the producer and consumer can copy massive 1KB payloads into the ring buffer completely independently, resulting in nearly a **2x performance increase** over the mutex baseline (6.8M Ops/sec). 
- *(Note: The Unbounded Lock-Free queue drops to ~5.8M Ops/sec at 1KB payloads due to the overhead of the OS dynamically allocating `new`/`delete` nodes).*

#### 2. mpsc Unbounded (Multi-Producer Single-Consumer)
As the number of concurrent producers increases, contention on the queue's tail pointer increases, leading to more CAS (Compare-And-Swap) retries. This naturally decreases overall throughput and increases tail latency.

<div align="center">
  <img src="https://quickchart.io/chart?c=%7B%22type%22%3A%22bar%22%2C%22data%22%3A%7B%22labels%22%3A%5B%221%20Producer%22%2C%222%20Producers%22%2C%224%20Producers%22%2C%228%20Producers%22%2C%2216%20Producers%22%5D%2C%22datasets%22%3A%5B%7B%22label%22%3A%22Throughput%20(Millions%20Ops%2Fsec)%22%2C%22data%22%3A%5B10.9%2C6.62%2C5.91%2C4.63%2C3.71%5D%2C%22backgroundColor%22%3A%22rgba(54%2C%20162%2C%20235%2C%200.8)%22%7D%5D%7D%2C%22options%22%3A%7B%22title%22%3A%7B%22display%22%3Atrue%2C%22text%22%3A%22MPSC%20Unbounded%20Throughput%20vs%20Contention%22%7D%2C%22scales%22%3A%7B%22yAxes%22%3A%5B%7B%22ticks%22%3A%7B%22beginAtZero%22%3Atrue%7D%7D%5D%7D%7D%7D" alt="MPSC Throughput Chart" width="45%" />
  <img src="https://quickchart.io/chart?c=%7B%22type%22%3A%22line%22%2C%22data%22%3A%7B%22labels%22%3A%5B%221%20Producer%22%2C%222%20Producers%22%2C%224%20Producers%22%2C%228%20Producers%22%5D%2C%22datasets%22%3A%5B%7B%22label%22%3A%22p99%20Tail%20Latency%20(%CE%BCs)%22%2C%22data%22%3A%5B123.5%2C21634.4%2C65887.7%2C107720.3%5D%2C%22borderColor%22%3A%22rgba(255%2C%2099%2C%20132%2C%201)%22%2C%22backgroundColor%22%3A%22rgba(255%2C%2099%2C%20132%2C%200.2)%22%2C%22fill%22%3Atrue%7D%5D%7D%2C%22options%22%3A%7B%22title%22%3A%7B%22display%22%3Atrue%2C%22text%22%3A%22MPSC%20Unbounded%20p99%20Latency%20vs%20Contention%22%7D%2C%22scales%22%3A%7B%22yAxes%22%3A%5B%7B%22ticks%22%3A%7B%22beginAtZero%22%3Atrue%7D%7D%5D%7D%7D%7D" alt="MPSC Latency Chart" width="45%" />
</div>

**Throughput (1,000,000 Operations):**
- **1 Producer:** `1.09e+07 Ops/sec`
- **2 Producers:** `6.62e+06 Ops/sec`
- **4 Producers:** `5.91e+06 Ops/sec`
- **8 Producers:** `4.63e+06 Ops/sec`
- **16 Producers:** `3.71e+06 Ops/sec`

**Latency (Transit Time - 1,000,000 Operations):**
- **1 Producer:** p50: `0.70 us` | p99: `123.50 us`
- **2 Producers:** p50: `7207.30 us` | p99: `21634.40 us`
- **4 Producers:** p50: `39352.60 us` | p99: `65887.70 us`
- **8 Producers:** p50: `68444.10 us` | p99: `107720.30 us`

*(Notice how tail latency spikes significantly as threads compete for the lock-free enqueue).*

---

## Acknowledgements

This group project was built under the awesome mentorship and guidance of **Toshit Jain Bhaiya** as part of the **Coding Club, IIT Guwahati**.

- **Contributors**: Badri Bishal Das, Sujal Patnaik, Sudipto Ghosh & Biswabhusan Samal
- **Blog Write-up**: [Read Here](https://www.badribishaldas.in/blog/threadSafeQueue/)
