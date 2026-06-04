#ifndef LOCKFREE_MPSC_UNBOUNDED_IMPL
#define LOCKFREE_MPSC_UNBOUNDED_IMPL

#include "defs.hpp"
#include <thread>
#include <utility>

namespace tsfqueue::impl {
    template<typename T, typename Allocator, bool TrackMetrics>
    lockfree_mpsc_unbounded<T,Allocator,TrackMetrics>::lockfree_mpsc_unbounded(){
        node* stub = allocate_node();
        head = stub;
        tail.store(stub, std::memory_order_relaxed);
        
        if constexpr (TrackMetrics) {
            size.store(0, std::memory_order_relaxed);
            max_size_val.store(0, std::memory_order_relaxed);
        }
    }

    template<typename T, typename Allocator, bool TrackMetrics>
    lockfree_mpsc_unbounded<T,Allocator,TrackMetrics>::lockfree_mpsc_unbounded(
        lockfree_mpsc_unbounded &&other) noexcept
        : lockfree_mpsc_unbounded(){
            swap(other);
        }

    template <typename T, typename Allocator, bool TrackMetrics>
    lockfree_mpsc_unbounded<T, Allocator, TrackMetrics> &
    lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::operator=(
    lockfree_mpsc_unbounded &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        lockfree_mpsc_unbounded tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    template<typename T, typename Allocator, bool TrackMetrics>
    lockfree_mpsc_unbounded<T,Allocator,TrackMetrics>::~lockfree_mpsc_unbounded(){
        node* curr = head;
        while(curr != nullptr){
            node* nxt = curr->next.load(std::memory_order_relaxed);
            node_alloc_traits::destroy(alloc, curr);
            node_alloc_traits::deallocate(alloc, curr, 1);
            curr = nxt;
        }
    }

    template<typename T, typename Allocator, bool TrackMetrics>
    void lockfree_mpsc_unbounded<T,Allocator,TrackMetrics>::swap(
        lockfree_mpsc_unbounded &other) noexcept {
            using std::swap;
            swap(alloc, other.alloc);
            swap(head, other.head);
            
            node* this_tail = tail.load(std::memory_order_relaxed);
            node* other_tail = other.tail.load(std::memory_order_relaxed);
            tail.store(other_tail, std::memory_order_relaxed);
            other.tail.store(this_tail, std::memory_order_relaxed);

            if constexpr (TrackMetrics) {
                size_t this_size = size.load(std::memory_order_relaxed);
                size_t other_size = other.size.load(std::memory_order_relaxed);
                size.store(other_size, std::memory_order_relaxed);
                other.size.store(this_size, std::memory_order_relaxed);

                size_t this_max = max_size_val.load(std::memory_order_relaxed);
                size_t other_max = other.max_size_val.load(std::memory_order_relaxed);
                max_size_val.store(other_max, std::memory_order_relaxed);
                other.max_size_val.store(this_max, std::memory_order_relaxed);
            }
        }
    
    template <typename T, typename Allocator, bool TrackMetrics>
    typename lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::node *
    lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::allocate_node() {
        node *p = node_alloc_traits::allocate(alloc, 1);
        node_alloc_traits::construct(alloc, p);
        p->next.store(nullptr, std::memory_order_relaxed);
        return p;
   }

   template <typename T, typename Allocator, bool TrackMetrics>
   void lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::deallocate_node(node *p) noexcept {
        node_alloc_traits::destroy(alloc, p);
        node_alloc_traits::deallocate(alloc, p, 1);
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  void lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::push(T value) {
    emplace(std::move(value));
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  template <typename... Args>
  void lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::emplace(Args &&...args) {
    node *new_stub = allocate_node();
    node *old_tail = tail.exchange(new_stub, std::memory_order_acq_rel);
    old_tail->data = T(std::forward<Args>(args)...);
    old_tail->next.store(new_stub, std::memory_order_release);
    
    if constexpr (TrackMetrics) {
        size.fetch_add(1, std::memory_order_relaxed);
    }
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  bool lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::try_pop(T& value){
    node *old_head = head;
    node *nxt = old_head->next.load(std::memory_order_acquire);
    if(nxt == nullptr) return false;
    value = std::move(old_head->data);
    head = nxt;
    deallocate_node(old_head);
    
    if constexpr (TrackMetrics) {
        size_t c_size = size.fetch_sub(1, std::memory_order_relaxed) - 1;
        size_t m_size = max_size_val.load(std::memory_order_relaxed);
        while (c_size > m_size && !max_size_val.compare_exchange_weak(m_size, c_size, 
               std::memory_order_relaxed, std::memory_order_relaxed));
    }
    return true;
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  void lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::wait_and_pop(T &value) {
    while (!try_pop(value)) {
        std::this_thread::yield();
    }
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  bool lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::peek(T &value) const { 
    static_assert(std::is_copy_assignable_v<T>, "peek() requires T to be copy assignable");
    node *current_head = head;
    node *next = current_head->next.load(std::memory_order_acquire);
    if (next == nullptr) {
        return false;
    }
    value = current_head->data;
    return true;
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  bool lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::empty() const {
    return head->next.load(std::memory_order_acquire) == nullptr;
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  size_t lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::get_size() const noexcept {
    if constexpr (TrackMetrics) {
        return size.load(std::memory_order_relaxed);
    }
    return 0;
  }

  template <typename T, typename Allocator, bool TrackMetrics>
  size_t lockfree_mpsc_unbounded<T, Allocator, TrackMetrics>::max_size() const noexcept {
    if constexpr (TrackMetrics) {
        return max_size_val.load(std::memory_order_relaxed);
    }
    return 0;
  }

}

#endif