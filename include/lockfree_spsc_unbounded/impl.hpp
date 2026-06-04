#ifndef LOCKFREE_SPSC_UNBOUNDED_IMPL
#define LOCKFREE_SPSC_UNBOUNDED_IMPL

#include "defs.hpp"
#include <thread>
#include <utility>

namespace tsfqueue::impl {

    template <typename T, typename Allocator, bool TrackMetrics>
    lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::lockfree_spsc_unbounded() {
        node* stub = allocate_node_();
        head_ = stub;
        tail_ = stub;

        if constexpr (TrackMetrics) {
            size_.store(0, std::memory_order_relaxed);
            max_size_val_.store(0, std::memory_order_relaxed);
        }
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::lockfree_spsc_unbounded(
        lockfree_spsc_unbounded &&other) noexcept 
        : lockfree_spsc_unbounded() {
        swap(other);
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    lockfree_spsc_unbounded<T, Allocator, TrackMetrics>& 
    lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::operator=(
        lockfree_spsc_unbounded &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        lockfree_spsc_unbounded tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::~lockfree_spsc_unbounded() {
        node* curr = head_;
        while (curr != nullptr) {
            node* nxt = curr->next.load(std::memory_order_relaxed);
            deallocate_node_(curr);
            curr = nxt;
        }
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    void lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::swap(
        lockfree_spsc_unbounded &other) noexcept {
        using std::swap;
        swap(alloc_, other.alloc_);
        swap(head_, other.head_);
        swap(tail_, other.tail_);

        if constexpr (TrackMetrics) {
            size_t this_size = size_.load(std::memory_order_relaxed);
            size_t other_size = other.size_.load(std::memory_order_relaxed);
            size_.store(other_size, std::memory_order_relaxed);
            other.size_.store(this_size, std::memory_order_relaxed);

            size_t this_max = max_size_val_.load(std::memory_order_relaxed);
            size_t other_max = other.max_size_val_.load(std::memory_order_relaxed);
            max_size_val_.store(other_max, std::memory_order_relaxed);
            other.max_size_val_.store(this_max, std::memory_order_relaxed);
        }
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    typename lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::node* lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::allocate_node_() {
        node* p = node_alloc_traits::allocate(alloc_, 1);
        node_alloc_traits::construct(alloc_, p);
        p->next.store(nullptr, std::memory_order_relaxed);
        return p;
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    void lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::deallocate_node_(node *p) noexcept {
        node_alloc_traits::destroy(alloc_, p);
        node_alloc_traits::deallocate(alloc_, p, 1);
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    void lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::push(T value) {
        emplace(std::move(value));
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    template <typename... Args>
    void lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::emplace(Args &&...args) {
        node* new_stub = allocate_node_();
        
        tail_->data = T(std::forward<Args>(args)...);
        tail_->next.store(new_stub, std::memory_order_release);
        tail_ = new_stub;

        if constexpr (TrackMetrics) {
            size_t current_sz = size_.fetch_add(1, std::memory_order_relaxed) + 1;
            size_t max_sz = max_size_val_.load(std::memory_order_relaxed);
            while (current_sz > max_sz && !max_size_val_.compare_exchange_weak(max_sz, current_sz, 
                   std::memory_order_relaxed, std::memory_order_relaxed));
        }
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    bool lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::try_pop(T& value) {
        node* old_head = head_;
        node* nxt = old_head->next.load(std::memory_order_acquire);
        
        if (nxt == nullptr) {
            return false; 
        }

        value = std::move(old_head->data);
        head_ = nxt;
        deallocate_node_(old_head);

        if constexpr (TrackMetrics) {
            size_.fetch_sub(1, std::memory_order_relaxed);
        }

        return true;
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    void lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::wait_and_pop(T &value) {
        while (!try_pop(value)) {
            std::this_thread::yield();
        }
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    bool lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::peek(T &value) const {
        static_assert(std::is_copy_assignable_v<T>, "peek() requires T to be copy assignable");
        
        node* current_head = head_;
        node* next_node = current_head->next.load(std::memory_order_acquire);
        
        if (next_node == nullptr) {
            return false;
        }
        
        value = current_head->data;
        return true;
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    bool lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::empty() const {
        return head_->next.load(std::memory_order_acquire) == nullptr;
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    size_t lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::size() const noexcept {
        if constexpr (TrackMetrics) {
            return size_.load(std::memory_order_relaxed);
        }
        return 0;
    }

    template <typename T, typename Allocator, bool TrackMetrics>
    size_t lockfree_spsc_unbounded<T, Allocator, TrackMetrics>::max_size() const noexcept {
        if constexpr (TrackMetrics) {
            return max_size_val_.load(std::memory_order_relaxed);
        }
        return 0;
    }

} // namespace tsfqueue::impl

#endif