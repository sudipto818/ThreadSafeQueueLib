#include <iostream>
#include <fstream>   
#include <thread>
#include <vector>
#include <atomic>
#include <string>
#include <chrono>
#include <iomanip>

#include "tsfqueue.hpp"
#include "lockfree_spsc_unbounded/queue.hpp"

constexpr size_t TOTAL_OPS = 5000000; // Increased slightly for throughput testing

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
    size_t fails = 0;
    while (popped_count < total_items_to_pop) {
        if (q->try_pop(val)) {
            popped_count++;
            fails = 0;
        } else {
            fails++;
            if (fails > 1000) {
                std::this_thread::yield();
            }
        }
    }
}

template <typename QueueType>
void run_throughput_benchmark(const std::string& queue_name, std::ofstream& outfile) {
    QueueType q;
    std::atomic_bool start{ false };

    // SPSC implies exactly 1 Consumer and 1 Producer
    std::thread consumer(consumer_thread<QueueType>, &q, TOTAL_OPS, &start);
    std::thread producer(producer_thread<QueueType>, &q, TOTAL_OPS, &start);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto start_time = std::chrono::high_resolution_clock::now();
    start.store(true, std::memory_order_release);

    if (consumer.joinable()) consumer.join();
    if (producer.joinable()) producer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    double ops_per_second = TOTAL_OPS / diff.count();

    std::cout << std::left << std::setw(30) << queue_name 
              << "| P=1, C=1" 
              << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
              << " | Throughput: " << std::scientific << std::setprecision(2) << ops_per_second << " Ops/sec\n";

    outfile << std::left << std::setw(30) << queue_name 
            << "| P=1, C=1" 
            << " | Time: " << std::setw(8) << std::fixed << std::setprecision(4) << diff.count() << " s"
            << " | Throughput: " << std::scientific << std::setprecision(2) << ops_per_second << " Ops/sec\n";
}

int main() {
    std::ofstream outfile("benchmark_results_spsc.txt");
    
    std::cout << "Starting Benchmarks (SPSC Only)... Please wait (This might take a minute)..." << std::endl;
    std::cout << "Results will be saved to 'benchmark_results_spsc.txt'" << std::endl << std::endl;

    std::string header = "--- THROUGHPUT BENCHMARK (TOTAL_OPS = " + std::to_string(TOTAL_OPS) + ") ---\n\n";
    std::cout << header << std::flush;
    outfile << header;
    
    std::string divider = "-------------------------------------------------------------------\n";

    // Run multiple iterations of the 1-producer, 1-consumer setup
    for (int run = 1; run <= 5; ++run) {
        std::string run_name = "SPSC_Unbounded (Run " + std::to_string(run) + ")";
        run_throughput_benchmark<tsfqueue::impl::lockfree_spsc_unbounded<int>>(run_name, outfile);
    }
    
    std::cout << divider;
    outfile << divider;

    outfile.close();
    
    std::cout << "\nBenchmarks complete!\n";

    return 0;
}