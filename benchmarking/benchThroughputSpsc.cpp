#include <iostream>
#include <fstream>   
#include <thread>
#include <vector>
#include <atomic>
#include <string>
#include <chrono>
#include <iomanip>
#include <queue>
#include <mutex>

#include "tsfqueue.hpp"
#include "lockfreeSpscUnbounded/queue.hpp"
#include "lockfreeSpscBounded/queue.hpp"

constexpr size_t TOTAL_OPS = 5000000;

template <size_t N>
struct Payload {
    uint8_t data[N] = {0};
};

// Baseline Mutex Queue for comparison
template <typename T>
class BlockingQueue {
private:
    std::queue<T> q;
    std::mutex mtx;

public:
    using value_type = T;
    
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(value);
    }

    bool tryPop(T& value) {
        std::lock_guard<std::mutex> lock(mtx);
        if (q.empty()) return false;
        value = q.front();
        q.pop();
        return true;
    }
};

template <typename QueueType, typename T>
void producerThread(QueueType* q, size_t itemsToPush, std::atomic_bool* start) {
    T dummy{};
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (size_t i = 0; i < itemsToPush; ++i) {
        if constexpr (requires { { q->tryPush(dummy) } -> std::same_as<bool>; }) {
            while (!q->tryPush(dummy)) { std::this_thread::yield(); }
        } else if constexpr (requires { q->push(dummy); }) {
            q->push(dummy);
        } else if constexpr (requires { q->waitAndPush(dummy); }) {
            q->waitAndPush(dummy);
        } else if constexpr (requires { q->emplace_back(dummy); }) {
            q->emplace_back(dummy);
        }
    }
}

template <typename QueueType, typename T>
void consumerThread(QueueType* q, size_t totalItemsToPop, std::atomic_bool* start) {
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    size_t poppedCount = 0;
    T val{};
    size_t fails = 0;
    while (poppedCount < totalItemsToPop) {
        if (q->tryPop(val)) {
            poppedCount++;
            fails = 0;
        } else {
            fails++;
            if (fails > 1000) {
                std::this_thread::yield();
            }
        }
    }
}

template <typename QueueType, typename T>
void runThroughputBenchmark(const std::string& queueName, std::ofstream& outfile) {
    auto q = std::make_unique<QueueType>();
    std::atomic_bool start{ false };

    std::thread consumer(consumerThread<QueueType, T>, q.get(), TOTAL_OPS, &start);
    std::thread producer(producerThread<QueueType, T>, q.get(), TOTAL_OPS, &start);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto startTime = std::chrono::high_resolution_clock::now();
    start.store(true, std::memory_order_release);

    if (consumer.joinable()) consumer.join();
    if (producer.joinable()) producer.join();

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = endTime - startTime;
    double opsPerSecond = TOTAL_OPS / diff.count();

    std::cout << std::left << std::setw(35) << queueName 
              << "| P=1, C=1" 
              << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
              << " | Throughput: " << std::scientific << std::setprecision(2) << opsPerSecond << " Ops/sec\n";

    outfile << std::left << std::setw(35) << queueName 
            << "| P=1, C=1" 
            << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
            << " | Throughput: " << std::scientific << std::setprecision(2) << opsPerSecond << " Ops/sec\n";
}

int main() {
    std::ofstream outfile("benchmarkResultsSpsc.txt");
    
    std::cout << "Starting Benchmarks (SPSC Lock-Free vs Mutex)... Please wait...\n\n";

    std::string header = "--- SPSC THROUGHPUT COMPARISON (TOTAL_OPS = " + std::to_string(TOTAL_OPS) + ") ---\n\n";
    std::cout << header << std::flush;
    outfile << header;
    
    std::string divider = "-------------------------------------------------------------------\n";

    runThroughputBenchmark<tsfqueue::impl::lockfreeSpscUnbounded<Payload<4>>, Payload<4>>("SPSC_LockFree_Unbounded (4B)", outfile);
    runThroughputBenchmark<tsfqueue::impl::lockfreeSpscBounded<Payload<4>, 65536>, Payload<4>>("SPSC_LockFree_Bounded (4B)", outfile);
    runThroughputBenchmark<BlockingQueue<Payload<4>>, Payload<4>>("SPSC_Mutex (4B)", outfile);
    
    std::cout << "\n";
    outfile << "\n";

    runThroughputBenchmark<tsfqueue::impl::lockfreeSpscUnbounded<Payload<1024>>, Payload<1024>>("SPSC_LockFree_Unbounded (1KB)", outfile);
    runThroughputBenchmark<tsfqueue::impl::lockfreeSpscBounded<Payload<1024>, 65536>, Payload<1024>>("SPSC_LockFree_Bounded (1KB)", outfile);
    runThroughputBenchmark<BlockingQueue<Payload<1024>>, Payload<1024>>("SPSC_Mutex (1KB)", outfile);
    
    std::cout << divider;
    outfile << divider;

    outfile.close();
    return 0;
}