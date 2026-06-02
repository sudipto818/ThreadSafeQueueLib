#include <gtest/gtest.h>
#include <thread>
#include <string>
#include <memory>

#include <lockfree_spsc_bounded/queue.hpp> 

using namespace tsfqueue::impl;


class SPSCBoundedTest : public ::testing::Test {
protected:
	lockfree_spsc_bounded<int, 100000> q;
	
	void SetUp() override {
	 
	}
	void TearDown() override {
	   
	}
};

//Basic Tests

// checking if it is empty initially
TEST_F(SPSCBoundedTest, Is_Empty_Initially) {
	EXPECT_TRUE(q.empty());
	EXPECT_EQ(q.size(), 0);
}

// checking if push and pop works
TEST_F(SPSCBoundedTest, Push_Pop_Works) {
	EXPECT_TRUE(q.try_push(100));
	EXPECT_FALSE(q.empty());
	
	int result = 0;
	EXPECT_TRUE(q.try_pop(result));
	EXPECT_EQ(result, 100);
}

// checking if the queue maintains the FIFO order
TEST_F(SPSCBoundedTest, Maintains_Order) {
	EXPECT_TRUE(q.try_push(1));
	EXPECT_TRUE(q.try_push(2));
	EXPECT_TRUE(q.try_push(3));
	
	int result = 0;
	EXPECT_TRUE(q.try_pop(result)); EXPECT_EQ(result, 1);
	EXPECT_TRUE(q.try_pop(result)); EXPECT_EQ(result, 2);
	EXPECT_TRUE(q.try_pop(result)); EXPECT_EQ(result, 3);
}

// checking if peek works
TEST_F(SPSCBoundedTest, Peek_Works_Without_Removing) {
	EXPECT_TRUE(q.try_push(42));
	
	int peek_val = 0;
	EXPECT_TRUE(q.peek(peek_val));
	EXPECT_EQ(peek_val, 42);
	
	EXPECT_EQ(q.size(), 1);
}


// checking for memoryleaks since we are doing dynamic memory allocation
// basically  by the domino effect if the last pointed value is not there then everything else which is  a superset of it has also been destroyed properly
// this test can also be thought of as checking if my queues destructore destroys  everything properly in a proper manner
TEST(SPSCBoundedTests, No_Memory_Leaks) {
   
// used shared pointer so that i can have this tracker concept  to prove the domino effect

	auto my_object = std::make_shared<int>(99);
	std::weak_ptr<int> tracker = my_object;
	
	{
		lockfree_spsc_bounded<std::shared_ptr<int>, 10> queue;
		
	   
		EXPECT_TRUE(queue.try_push(my_object));
		my_object.reset();   // because the pushing mechanism creates a copy and increases strong reference count of the control block so -1 for the object.reset
		// now only queue's copied shared ptr has ownership
		
		EXPECT_FALSE(tracker.expired());
		
		
	}
	
	EXPECT_TRUE(tracker.expired()); 
}


// checking if the queue can handle large objects
TEST(SPSCBoundedTests, Handles_Large_Objects) {
	lockfree_spsc_bounded<std::string, 100> string_queue;
	
	
	std::string s(1000, 'X'); 
	
	EXPECT_TRUE(string_queue.try_push(s));
	
	std::string output;
	EXPECT_TRUE(string_queue.try_pop(output));
	EXPECT_EQ(output.length(), 1000);
}



//testing spsc
TEST_F(SPSCBoundedTest,Testing_SPSC) {
	const int total = 100000;
	
	
	auto producer = [&]() {
		for (int i = 0; i < total; ++i) {
			q.wait_and_push(i);
		}
	};
	
	
	auto consumer = [&]() {
		for (int i = 0; i < total; ++i) {
			int val = -1;
		   
			q.wait_and_pop(val); 
			EXPECT_EQ(val, i);
		}
	};
	
	std::thread prod_thread(producer);
	std::thread cons_thread(consumer);
	
	prod_thread.join();
	cons_thread.join();
	
	
	EXPECT_TRUE(q.empty());
}


//final check and also checking peek function
TEST_F(SPSCBoundedTest, final_spsccheck_with_peek_and_pop) {
	const int total = 50000;
	
	auto producer = [&]() {
		for (int i = 0; i < total; ++i) q.wait_and_push(i);
	};
	
	auto consumer = [&]() {
		int expected = 0;
		while (expected < total) {
			int peek_val = -1;
			int pop_val = -1;
			
			
			if (q.peek(peek_val)) {
				
				bool success = q.try_pop(pop_val);
				EXPECT_TRUE(success);
				EXPECT_EQ(peek_val, pop_val);
				EXPECT_EQ(pop_val, expected);
				expected++;
			}
		}
	};
	
	std::thread t1(producer);
	std::thread t2(consumer);
	t1.join();
	t2.join();
}

TEST(SPSCBoundedTests, Try_Push_Fails_When_Full) {
	lockfree_spsc_bounded<int, 2> queue;
	EXPECT_TRUE(queue.try_push(1));
	EXPECT_TRUE(queue.try_push(2));
	EXPECT_FALSE(queue.try_push(3)); // Should fail because capacity is 2
}

// --- RIGOROUS TESTS ---

// 1. Stress testing wrap-around with a small queue capacity.
// This forces the queue to wrap around frequently and triggers full/empty conditions constantly.
TEST(SPSCBoundedTests, HighContention_WrapAround) {
	lockfree_spsc_bounded<int, 1024> small_q;
	const int total = 5000000; // 5 million items

	std::atomic<bool> start_flag{false};

	auto producer = [&]() {
		while (!start_flag.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		for (int i = 0; i < total; ++i) {
			small_q.wait_and_push(i);
		}
	};

	auto consumer = [&]() {
		while (!start_flag.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		for (int i = 0; i < total; ++i) {
			int val = -1;
			small_q.wait_and_pop(val);
			EXPECT_EQ(val, i);
		}
	};

	std::thread prod(producer);
	std::thread cons(consumer);

	// Start both threads simultaneously
	start_flag.store(true, std::memory_order_release);

	prod.join();
	cons.join();

	EXPECT_TRUE(small_q.empty());
}

// 2. Hammering try_push and try_pop in tight loops
// Tests the non-blocking endpoints under heavy load.
TEST(SPSCBoundedTests, TryPushPop_Spinning) {
	lockfree_spsc_bounded<int, 4096> spin_q;
	const int total = 5000000;

	auto producer = [&]() {
		for (int i = 0; i < total; ++i) {
			while (!spin_q.try_push(i)) {
				// busy spin (no yield, maximizing contention)
			}
		}
	};

	auto consumer = [&]() {
		for (int i = 0; i < total; ++i) {
			int val = -1;
			while (!spin_q.try_pop(val)) {
				// busy spin
			}
			EXPECT_EQ(val, i);
		}
	};

	std::thread prod(producer);
	std::thread cons(consumer);
	prod.join();
	cons.join();
}

// 3. Complex Object Integrity Test
// Ensures that large structures don't suffer from partial reads (tearing) during concurrent access.
struct ComplexData {
	int id;
	double values[4];
	char name[16];

	ComplexData() : id(-1) {
		for (int i = 0; i < 4; ++i) values[i] = 0.0;
		name[0] = '\0';
	}

	ComplexData(int i) : id(i) {
		for (int j = 0; j < 4; ++j) values[j] = i * 3.14 + j;
		snprintf(name, sizeof(name), "Name%d", i);
	}

	bool isValid(int expected_id) const {
		if (id != expected_id) return false;
		for (int j = 0; j < 4; ++j) {
			if (values[j] != expected_id * 3.14 + j) return false;
		}
		char expected_name[16];
		snprintf(expected_name, sizeof(expected_name), "Name%d", expected_id);
		if (strncmp(name, expected_name, 16) != 0) return false;
		return true;
	}
};

TEST(SPSCBoundedTests, ComplexObjectIntegrity) {
	lockfree_spsc_bounded<ComplexData, 1024> obj_q;
	const int total = 1000000;

	auto producer = [&]() {
		for (int i = 0; i < total; ++i) {
			obj_q.wait_and_push(ComplexData(i));
		}
	};

	auto consumer = [&]() {
		for (int i = 0; i < total; ++i) {
			ComplexData data;
			obj_q.wait_and_pop(data);
			EXPECT_TRUE(data.isValid(i));
		}
	};

	std::thread prod(producer);
	std::thread cons(consumer);
	prod.join();
	cons.join();
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
