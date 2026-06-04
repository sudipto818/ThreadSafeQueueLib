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
// Assuming your directory structure mirrors the MPSC one
#include "lockfree_spsc_unbounded/queue.hpp" 

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
        // 1. Record the exact time before push.
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
        
        // 2. Attempt to pop
        bool success = q->try_pop(pushed_time);
        
        // 3. Record time immediately after
        uint64_t end_op = get_time_ns();

        if (success) {
            // Metric A (Pop Latency)
            pop_latencies->push_back(end_op - start_op);
            
            // Metric B (Transit Latency)
            transit_latencies->push_back(end_op - pushed_time);
            
            popped_count++;
            fails = 0;
        } else {
            fails++;
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
void run_latency_benchmark(const std::string& queue_name, std::ofstream& outfile) {
    QueueType q;
    
    // SPSC implies exactly 1 Producer and 1 Consumer
    const size_t items_per_producer = TOTAL_OPS;
    const size_t items_per_consumer = TOTAL_OPS;
    
    std::vector<uint64_t> producer_push_latencies;
    std::vector<uint64_t> consumer_pop_latencies;
    std::vector<uint64_t> consumer_transit_latencies;
    
    std::atomic_bool start{ false };

    // Spin up Consumer
    std::thread consumer(consumer_thread_lat<QueueType>, &q, items_per_consumer, &start, 
                         &consumer_pop_latencies, &consumer_transit_latencies);
    
    // Spin up Producer
    std::thread producer(producer_thread_lat<QueueType>, &q, items_per_producer, &start, 
                         &producer_push_latencies);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Unleash threads
    start.store(true, std::memory_order_release);

    consumer.join();
    producer.join();

    std::string label = "P=1,C=1";

    // Print Metric A in nanoseconds (Scale = 1.0)
    print_stats("PUSH EXEC (Metric A)", label, producer_push_latencies, outfile, 1.0, "ns");
    print_stats("POP EXEC  (Metric A)", label, consumer_pop_latencies, outfile, 1.0, "ns");
    
    // Print Metric B in microseconds (Scale = 1000.0)
    print_stats("TRANSIT   (Metric B)", label, consumer_transit_latencies, outfile, 1000.0, "us");
    
    std::cout << "--------------------------------------------------------------------------------------\n";
    outfile << "--------------------------------------------------------------------------------------\n";
}

int main() {
    std::ofstream outfile("latency_results_spsc.txt");
    
    std::cout << "Starting Comprehensive Latency Benchmarks (SPSC Only)...\n\n";
    
    std::string header = "--- LATENCY BENCHMARK (TOTAL_OPS = " + std::to_string(TOTAL_OPS) + ") ---\n";
    header += "Execution (Metric A) is in NANOSECONDS (ns).\n";
    header += "Transit   (Metric B) is in MICROSECONDS (us).\n";
    header += "Lower is better.\n";
    header += "--------------------------------------------------------------------------------------\n";
    
    std::cout << header << std::flush;
    outfile << header;

    // Run the SPSC benchmark a few times to show consistency, since we can't scale thread counts
    // Assuming you have a wrapper named tsfqueue::SPSCUnbounded or similar. 
    // If not, use tsfqueue::impl::lockfree_spsc_unbounded<uint64_t> directly.
    for (int run = 1; run <= 3; ++run) {
        std::string run_name = "SPSC_Unbounded (Run " + std::to_string(run) + ")";
        run_latency_benchmark<tsfqueue::impl::lockfree_spsc_unbounded<uint64_t>>(run_name, outfile);
    }

    outfile.close();
    std::cout << "\nBenchmarks complete! Results saved to 'latency_results_spsc.txt'\n";

    return 0;
}