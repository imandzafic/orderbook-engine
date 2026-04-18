#pragma once

#include <vector>
#include <cstddef>
#include <cassert>
#include <new>

namespace ob {

// ─── ObjectPool ──────────────────────────────────────────────────────
// Pre-allocates a contiguous block of T objects.  alloc()/free() are
// O(1) and never call malloc on the hot path.
//
// Design note (ref image 3 — arrowhead queues):
//   The arrowhead system uses per-symbol queues to isolate allocation.
//   Similarly, each OrderBook instance owns its own pools so there is
//   zero contention between symbols.

template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t capacity) : capacity_(capacity) {
        // Single contiguous allocation — cache-line friendly
        storage_.resize(capacity);
        free_list_.reserve(capacity);
        for (size_t i = capacity; i > 0; --i) {
            free_list_.push_back(&storage_[i - 1]);
        }
    }

    // Non-copyable, movable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = default;
    ObjectPool& operator=(ObjectPool&&) = default;

    template <typename... Args>
    T* alloc(Args&&... args) {
        if (free_list_.empty()) return nullptr;  // pool exhausted
        T* obj = free_list_.back();
        free_list_.pop_back();
        ::new (static_cast<void*>(obj)) T(std::forward<Args>(args)...);
        return obj;
    }

    void free(T* obj) {
        obj->~T();
        free_list_.push_back(obj);
    }

    size_t available() const { return free_list_.size(); }
    size_t capacity()  const { return capacity_; }

private:
    size_t          capacity_;
    std::vector<T>  storage_;
    std::vector<T*> free_list_;
};

} // namespace ob
