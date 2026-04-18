#pragma once

#include "order.h"
#include <cstddef>

namespace ob {

// ─── PriceLevel ──────────────────────────────────────────────────────
// Intrusive doubly-linked list of orders at one price point.
// All operations are O(1). Orders are kept in FIFO arrival order
// so that price-time priority is satisfied by simply walking head→tail.
//
// Design note (ref image 2 — bid/ask depth chart):
//   Each bar in the depth chart corresponds to one PriceLevel.
//   total_qty is the "depth available" at that price.

class PriceLevel {
public:
    Price    price;
    Quantity total_qty{0};
    size_t   order_count{0};

    PriceLevel() : price(0), head_(nullptr), tail_(nullptr) {}
    explicit PriceLevel(Price p) : price(p), head_(nullptr), tail_(nullptr) {}

    // Append order to tail (FIFO).  O(1).
    void push_back(Order* order) {
        order->prev = tail_;
        order->next = nullptr;
        if (tail_) tail_->next = order;
        else       head_ = order;
        tail_ = order;
        total_qty += order->qty;
        ++order_count;
    }

    // Remove arbitrary order.  O(1) given pointer.
    void remove(Order* order) {
        if (order->prev) order->prev->next = order->next;
        else             head_ = order->next;

        if (order->next) order->next->prev = order->prev;
        else             tail_ = order->prev;

        total_qty -= order->qty;
        --order_count;
        order->prev = order->next = nullptr;
    }

    // Decrease qty of front order after partial fill.
    void reduce_front(Quantity delta) {
        total_qty -= delta;
        head_->qty -= delta;
        head_->filled_qty += delta;
    }

    Order* front() const { return head_; }
    bool   empty() const { return head_ == nullptr; }

private:
    Order* head_;
    Order* tail_;
};

} // namespace ob
