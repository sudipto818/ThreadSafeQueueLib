#include <iostream>
#include <fstream>   
#include <thread>
#include <vector>
#include <atomic>
#include <string>
#include <chrono>
#include <iomanip>

#include "tsfqueue.hpp"
#include "lockfree_mpsc_unbounded/queue.hpp"
#include "blocking_mpmc_unbounded/queue.hpp"
#include "lockfree_spsc_bounded/queue.hpp"
#include "lockfree_spsc_unbounded/queue.hpp"

constexpr size_t TOTAL_OPS = 10000000;

template <typename QueueType>
void producer_thread(QueueType* q, size_t items_to_push, std::atomic_bool* start) {
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (size_t i = 0; i < items_to_push; ++i) {
        if constexpr (requires { q->push(1); }) {
            q->push(1);
        } else if constexpr (requires { q->wait_and_push(1); }) {
            q->wait_and_push(1);
        } else if constexpr (requires { q->emplace_back(1); }) {
            q->emplace_back(1);
        }
    }
}

template <typename QueueType>
void consumer_thread(QueueType* q, size_t total_items_to_pop, std::atomic_bool* start) {
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    size_t popped_count = 0;
    int val = 0;
    while (popped_count < total_items_to_pop) {
        if constexpr (requires { q->try_pop(val); }) {
            if (q->try_pop(val)) {
                popped_count++;
            }
        } else if constexpr (requires { q->wait_and_pop(val); }) {
            q->wait_and_pop(val);
            popped_count++;
        }
    }
}

// Multi-Consumer Thread Logic

template <typename QueueType>
void mpmc_consumer_thread(QueueType* q, size_t items_to_pop, std::atomic_bool* start) {
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    size_t popped_count = 0;
    int val = 0;
    while (popped_count < items_to_pop) {
        if constexpr (requires { q->try_pop(val); }) {
            if (q->try_pop(val)) {
                popped_count++;
            }
        } else if constexpr (requires { q->wait_and_pop(val); }) {
            q->wait_and_pop(val);
            popped_count++;
        }
    }
}

// SINGLE Consumer Benchmark (For SPSC and MPSC)
template <typename QueueType>
void run_throughput_benchmark(int num_producers, const std::string& queue_name, std::ofstream& outfile) {
    QueueType q;
    const size_t items_per_producer = TOTAL_OPS / num_producers;
    std::vector<std::thread> producers;
    producers.reserve(num_producers);
    std::atomic_bool start{ false };

    std::thread consumer(consumer_thread<QueueType>, &q, TOTAL_OPS, &start);
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back(producer_thread<QueueType>, &q, items_per_producer, &start);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto start_time = std::chrono::high_resolution_clock::now();
    start.store(true, std::memory_order_release);

    if (consumer.joinable()) consumer.join();
    for (auto& p : producers) p.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    double ops_per_second = TOTAL_OPS / diff.count();

    std::cout << std::left << std::setw(30) << queue_name 
              << "| P=" << std::left << std::setw(8) << num_producers 
              << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
              << " | Throughput: " << std::scientific << std::setprecision(2) << ops_per_second << " Ops/sec\n";

    outfile << std::left << std::setw(30) << queue_name 
            << "| P=" << std::left << std::setw(8) << num_producers 
            << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
            << " | Throughput: " << std::scientific << std::setprecision(2) << ops_per_second << " Ops/sec\n";
}

// MULTI Consumer Benchmark (For MPMC)
template <typename QueueType>
void run_mpmc_benchmark(int num_producers, int num_consumers, const std::string& queue_name, std::ofstream& outfile) {
    QueueType q;
    const size_t items_per_producer = TOTAL_OPS / num_producers;
    const size_t items_per_consumer = TOTAL_OPS / num_consumers;
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic_bool start{ false };

    // Spin up Multiple Consumers
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back(mpmc_consumer_thread<QueueType>, &q, items_per_consumer, &start);
    }
    
    // Spin up Multiple Producers
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back(producer_thread<QueueType>, &q, items_per_producer, &start);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto start_time = std::chrono::high_resolution_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& c : consumers) c.join();
    for (auto& p : producers) p.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    double ops_per_second = TOTAL_OPS / diff.count();

    std::string label = "P=" + std::to_string(num_producers) + ", C=" + std::to_string(num_consumers);

    std::cout << std::left << std::setw(30) << queue_name 
              << "| " << std::left << std::setw(10) << label 
              << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
              << " | Throughput: " << std::scientific << std::setprecision(2) << ops_per_second << " Ops/sec\n";

    outfile << std::left << std::setw(30) << queue_name 
            << "| " << std::left << std::setw(10) << label 
            << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
            << " | Throughput: " << std::scientific << std::setprecision(2) << ops_per_second << " Ops/sec\n";
}

int main() {
    std::ofstream outfile("benchmark_results.txt");
    
    std::cout << "Starting Benchmarks... Please wait (This might take a minute)...\n";
    std::cout << "Results will be saved to 'benchmark_results.txt'\n\n";

    std::string header = "--- THROUGHPUT BENCHMARK (TOTAL_OPS = " + std::to_string(TOTAL_OPS) + ") ---\n\n";
    std::cout << header;
    outfile << header;

    // 1. SPSC Queues
    run_throughput_benchmark<tsfqueue::SPSCUnbounded<int>>(1, "SPSC_Unbounded", outfile);
    run_throughput_benchmark<tsfqueue::SPSCBounded<int, 65536>>(1, "SPSC_Bounded_64k", outfile);
    
    std::string divider = "-------------------------------------------------------------------\n";
    std::cout << divider;
    outfile << divider;

    // 2. MPSC Unbounded Queue
    for (int p : {1, 2, 4, 8, 16}) {
        run_throughput_benchmark<tsfqueue::MPSCUnbounded<int>>(p, "MPSC_Unbounded", outfile);
    }
    std::cout << divider;
    outfile << divider;

    // 3. MPMC Blocking Queue (Symmetric Producer & Consumer Scaling)
    for (int num_threads : {1, 2, 4, 8, 16}) {
        run_mpmc_benchmark<tsfqueue::BlockingMPMCUnbounded<int>>(num_threads, num_threads, "MPMC_Blocking", outfile);
    }

    outfile.close();
    
    std::cout << "\nBenchmarks complete! Press Enter to exit...";
    std::cin.get(); 

    return 0;
}