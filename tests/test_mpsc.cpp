#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <chrono>

#include <lockfree_mpsc_unbounded/queue.hpp> 

using namespace tsfqueue::impl;


class MPSCTest : public ::testing::Test {
protected:
	lockfree_mpsc_unbounded<int, std::allocator<int>, true> q;
	
	void SetUp() override {
	 
	}
	void TearDown() override {
	   
	}
};

// --- BASIC TESTS ---

TEST_F(MPSCTest, Is_Empty_Initially) {
	EXPECT_TRUE(q.empty());
	EXPECT_EQ(q.get_size(), 0u);
}

TEST_F(MPSCTest, Push_Pop_Works) {
	q.push(100);
	EXPECT_FALSE(q.empty());
	EXPECT_EQ(q.get_size(), 1u);
	
	int result = 0;
	EXPECT_TRUE(q.try_pop(result));
	EXPECT_EQ(result, 100);
	EXPECT_TRUE(q.empty());
}

TEST_F(MPSCTest, Emplace_Works) {
	lockfree_mpsc_unbounded<std::string, std::allocator<std::string>, true> str_q;
	str_q.emplace(5, 'A'); // Constructs "AAAAA" in place
	EXPECT_EQ(str_q.get_size(), 1u);
	
	std::string res;
	EXPECT_TRUE(str_q.try_pop(res));
	EXPECT_EQ(res, "AAAAA");
}

TEST_F(MPSCTest, Maintains_Order_SPSC) {
	q.push(1);
	q.push(2);
	q.push(3);
	
	int result = 0;
	q.try_pop(result); EXPECT_EQ(result, 1);
	q.try_pop(result); EXPECT_EQ(result, 2);
	q.try_pop(result); EXPECT_EQ(result, 3);
}

TEST_F(MPSCTest, Peek_Works_Without_Removing) {
	q.push(42);
	
	int peek_val = 0;
	EXPECT_TRUE(q.peek(peek_val));
	EXPECT_EQ(peek_val, 42);
	EXPECT_EQ(q.get_size(), 1u);
}

TEST_F(MPSCTest, Move_Constructor_Works) {
	q.push(10);
	q.push(20);

	lockfree_mpsc_unbounded<int, std::allocator<int>, true> moved_q(std::move(q));

	// Original should be empty after move
	EXPECT_TRUE(q.empty());
	EXPECT_EQ(moved_q.get_size(), 2u);

	int val;
	EXPECT_TRUE(moved_q.try_pop(val)); EXPECT_EQ(val, 10);
	EXPECT_TRUE(moved_q.try_pop(val)); EXPECT_EQ(val, 20);
}

// --- ADVANCED / RIGOROUS TESTS ---

TEST(MPSCObjectTests, No_Memory_Leaks) {
	auto my_object = std::make_shared<int>(99);
	std::weak_ptr<int> tracker = my_object;
	
	{
		lockfree_mpsc_unbounded<std::shared_ptr<int>> queue;
		queue.push(my_object);
		my_object.reset(); 
		EXPECT_FALSE(tracker.expired());
	}
	EXPECT_TRUE(tracker.expired()); 
}

// testing multiple producers and single consumer
TEST_F(MPSCTest, Multiple_Producers_Single_Consumer) {
	const int num_producers = 4;
	const int items_per_producer = 25000;
	const int total_items = num_producers * items_per_producer;

	std::atomic<bool> start_flag{false};
	std::atomic<int> producers_done{0};

	auto producer = [&](int producer_id) {
		while (!start_flag.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		for (int i = 0; i < items_per_producer; ++i) {
			q.push(producer_id * items_per_producer + i);
		}
		producers_done.fetch_add(1, std::memory_order_release);
	};

	std::atomic<int> pop_count{0};
	auto consumer = [&]() {
		while (!start_flag.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}

		while (pop_count.load(std::memory_order_acquire) < total_items) {
			int val = -1;
			if (q.try_pop(val)) {
				pop_count.fetch_add(1, std::memory_order_relaxed);
			} else if (producers_done.load(std::memory_order_acquire) == num_producers && q.empty()) {
				// Safety break if we missed something (should not happen in a correct MPSC)
				break;
			} else {
				std::this_thread::yield();
			}
		}
	};

	std::vector<std::thread> threads;
	for (int i = 0; i < num_producers; ++i) {
		threads.emplace_back(producer, i);
	}
	threads.emplace_back(consumer);

	// Start all
	start_flag.store(true, std::memory_order_release);

	for (auto& t : threads) {
		t.join();
	}

	EXPECT_TRUE(q.empty());
	EXPECT_EQ(pop_count.load(), total_items);
}

// Structure for integrity check
struct MpscData {
	int producer_id;
	int seq_id;
	char payload[32];

	MpscData() : producer_id(-1), seq_id(-1) { payload[0] = '\0'; }

	MpscData(int p_id, int s_id) : producer_id(p_id), seq_id(s_id) {
		snprintf(payload, sizeof(payload), "P%d-S%d", p_id, s_id);
	}

	bool isValid() const {
		if (producer_id < 0 || seq_id < 0) return false;
		char expected[32];
		snprintf(expected, sizeof(expected), "P%d-S%d", producer_id, seq_id);
		return strncmp(payload, expected, 32) == 0;
	}
};

TEST(MPSCObjectTests, MultiThreaded_ComplexObjectIntegrity) {
	lockfree_mpsc_unbounded<MpscData> obj_q;
	const int num_producers = 6;
	const int items_per_producer = 50000;
	const int total_items = num_producers * items_per_producer;

	std::atomic<bool> start_flag{false};

	auto producer = [&](int p_id) {
		while (!start_flag.load(std::memory_order_acquire)) {
			// spin
		}
		for (int i = 0; i < items_per_producer; ++i) {
			obj_q.push(MpscData(p_id, i));
		}
	};

	auto consumer = [&]() {
		while (!start_flag.load(std::memory_order_acquire)) {
			// spin
		}
		
		int count = 0;
		while (count < total_items) {
			MpscData data;
			// using wait_and_pop
			obj_q.wait_and_pop(data);
			EXPECT_TRUE(data.isValid());
			count++;
		}
	};

	std::vector<std::thread> producers;
	for (int i = 0; i < num_producers; ++i) {
		producers.emplace_back(producer, i);
	}
	std::thread cons(consumer);

	start_flag.store(true, std::memory_order_release);

	for (auto& p : producers) p.join();
	cons.join();

	EXPECT_TRUE(obj_q.empty());
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
