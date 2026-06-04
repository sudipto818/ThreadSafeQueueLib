#include <iostream>
#include <fstream>   
#include <thread>
#include <vector>
#include <atomic>
#include <string>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <algorithm>

#include "tsfqueue.hpp"
#include "lockfree_mpsc_unbounded/queue.hpp"

// Use 1 Million total operations
constexpr size_t TOTAL_OPS = 1000000;

inline uint64_t get_time_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// Synthetic workload to throttle producers. 
// This keeps the queue relatively empty to measure true uncontended transit latency (Metric B)
inline void spin_wait(int cycles) {
    volatile int dummy = 0; // volatile prevents optimization
    for (int i = 0; i < cycles; ++i) {
        dummy = i; 
    }
}

template <typename QueueType>
void producer_thread_lat(QueueType* q, size_t items_to_push, std::atomic_bool* start, std::vector<uint64_t>* push_latencies) {
    push_latencies->reserve(items_to_push);
    
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    
    for (size_t i = 0; i < items_to_push; ++i) {
        // 1. Record the exact time before push. We will pass THIS timestamp into the queue.
        uint64_t start_op = get_time_ns(); 
        
        // 2. Perform the operation (Payload = start_op)
        if constexpr (requires { q->push(start_op); }) {
            q->push(start_op);
        } else if constexpr (requires { q->wait_and_push(start_op); }) {
            q->wait_and_push(start_op);
        } else if constexpr (requires { q->emplace_back(start_op); }) {
            q->emplace_back(start_op);
        }

        // 3. Measure Execution Time (Metric A - Push)
        uint64_t end_op = get_time_ns();
        push_latencies->push_back(end_op - start_op); 
        
        // 4. Throttle slightly to pace the queue depth and prevent bloat
        spin_wait(200); 
    }
}

template <typename QueueType>
void consumer_thread_lat(QueueType* q, size_t items_to_pop, std::atomic_bool* start, std::vector<uint64_t>* pop_latencies, std::vector<uint64_t>* transit_latencies) {
    pop_latencies->reserve(items_to_pop);
    transit_latencies->reserve(items_to_pop);
    
    while (!start->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    
    size_t popped_count = 0;
    uint64_t pushed_time = 0;
    size_t fails = 0;
    
    while (popped_count < items_to_pop) {
        // 1. Record time before pop attempt
        uint64_t start_op = get_time_ns();
        
        // 2. Attempt to pop (pushed_time will hold the producer's timestamp if successful)
        bool success = q->try_pop(pushed_time);
        
        // 3. Record time immediately after
        uint64_t end_op = get_time_ns();

        if (success) {
            // Metric A (Pop Latency): How long the try_pop function took to execute
            pop_latencies->push_back(end_op - start_op);
            
            // Metric B (Transit Latency): Core-to-Core time (End Pop Time - Start Push Time)
            transit_latencies->push_back(end_op - pushed_time);
            
            popped_count++;
            fails = 0;
        } else {
            fails++;
            // Increased threshold before yielding so we don't accidentally measure OS wake-up time 
            if (fails > 10000) { 
                std::this_thread::yield();
            }
        }
    }
}

// Helper function to calculate percentiles and print with dynamic scaling/units
void print_stats(const std::string& metric_name, const std::string& label, std::vector<uint64_t>& latencies, std::ofstream& outfile, double scale_factor = 1.0, const std::string& unit = "ns") {
    if (latencies.empty()) return;
    std::sort(latencies.begin(), latencies.end());

    double avg = (std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size()) / scale_factor;
    double p50 = (latencies[latencies.size() * 0.50]) / scale_factor;
    double p99 = (latencies[latencies.size() * 0.99]) / scale_factor;
    double p999 = (latencies[latencies.size() * 0.999]) / scale_factor;

    std::cout << std::left << std::setw(25) << metric_name 
              << "| " << std::left << std::setw(8) << label 
              << " | Avg: " << std::setw(8) << std::fixed << std::setprecision(2) << avg << " " << unit
              << " | p50: " << std::setw(8) << p50 << " " << unit
              << " | p99: " << std::setw(8) << p99 << " " << unit
              << " | p99.9: " << p999 << " " << unit << "\n";

    outfile << std::left << std::setw(25) << metric_name 
            << "| " << std::left << std::setw(8) << label 
            << " | Avg: " << std::setw(8) << std::fixed << std::setprecision(2) << avg << " " << unit
            << " | p50: " << std::setw(8) << p50 << " " << unit
            << " | p99: " << std::setw(8) << p99 << " " << unit
            << " | p99.9: " << p999 << " " << unit << "\n";
}

template <typename QueueType>
void run_latency_benchmark(int num_producers, int num_consumers, const std::string& queue_name, std::ofstream& outfile) {
    QueueType q;
    const size_t items_per_producer = TOTAL_OPS / num_producers;
    const size_t items_per_consumer = TOTAL_OPS / num_consumers;
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    std::vector<std::vector<uint64_t>> producer_push_latencies(num_producers);
    
    // MPSC means only 1 consumer, but we keep it modular
    std::vector<std::vector<uint64_t>> consumer_pop_latencies(num_consumers);
    std::vector<std::vector<uint64_t>> consumer_transit_latencies(num_consumers);
    
    std::atomic_bool start{ false };

    // Spin up Consumers 
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back(consumer_thread_lat<QueueType>, &q, items_per_consumer, &start, 
                               &consumer_pop_latencies[i], &consumer_transit_latencies[i]);
    }
    
    // Spin up Producers
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back(producer_thread_lat<QueueType>, &q, items_per_producer, &start, 
                               &producer_push_latencies[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Unleash threads
    start.store(true, std::memory_order_release);

    for (auto& c : consumers) c.join();
    for (auto& p : producers) p.join();

    // Aggregate Data
    std::vector<uint64_t> all_push, all_pop, all_transit;
    all_push.reserve(TOTAL_OPS);
    all_pop.reserve(TOTAL_OPS);
    all_transit.reserve(TOTAL_OPS);

    for (const auto& vec : producer_push_latencies) all_push.insert(all_push.end(), vec.begin(), vec.end());
    for (const auto& vec : consumer_pop_latencies) all_pop.insert(all_pop.end(), vec.begin(), vec.end());
    for (const auto& vec : consumer_transit_latencies) all_transit.insert(all_transit.end(), vec.begin(), vec.end());

    std::string label = "P=" + std::to_string(num_producers) + ",C=" + std::to_string(num_consumers);

    // Print Metric A in nanoseconds (Scale = 1.0)
    print_stats("PUSH EXEC (Metric A)", label, all_push, outfile, 1.0, "ns");
    print_stats("POP EXEC  (Metric A)", label, all_pop, outfile, 1.0, "ns");
    
    // Print Metric B in microseconds (Scale = 1000.0)
    print_stats("TRANSIT   (Metric B)", label, all_transit, outfile, 1000.0, "us");
    
    std::cout << "--------------------------------------------------------------------------------------\n";
    outfile << "--------------------------------------------------------------------------------------\n";
}

int main() {
    std::ofstream outfile("latency_results_mpsc.txt");
    
    std::cout << "Starting Comprehensive Latency Benchmarks (MPSC Only)...\n\n";
    
    std::string header = "--- LATENCY BENCHMARK (TOTAL_OPS = " + std::to_string(TOTAL_OPS) + ") ---\n";
    header += "Execution (Metric A) is in NANOSECONDS (ns).\n";
    header += "Transit   (Metric B) is in MICROSECONDS (us).\n";
    header += "Lower is better.\n";
    header += "--------------------------------------------------------------------------------------\n";
    
    std::cout << header << std::flush;
    outfile << header;

    // MPSC Unbounded Queue Test
    for (int p : {1, 2, 4, 8}) {
        run_latency_benchmark<tsfqueue::MPSCUnbounded<uint64_t>>(p, 1, "MPSC_Unbounded", outfile);
    }

    outfile.close();
    std::cout << "\nBenchmarks complete! Results saved to 'latency_results_mpsc.txt'\n";

    return 0;
}