#pragma once

#include <cstdint>
#include <chrono>
#include <atomic>
#include <string>

namespace ob {

// ─── Enums ───────────────────────────────────────────────────────────
enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrderStatus : uint8_t {
    New       = 0,
    Active    = 1,
    Partial   = 2,
    Filled    = 3,
    Cancelled = 4
};

// ─── Timestamp utility ───────────────────────────────────────────────
using Clock     = std::chrono::steady_clock;
using Timestamp = std::chrono::steady_clock::time_point;
using Nanos     = std::chrono::nanoseconds;

inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<Nanos>(Clock::now().time_since_epoch()).count()
    );
}

// ─── Price representation ────────────────────────────────────────────
// Fixed-point: price stored as integer ticks (e.g., cents).
// Avoids floating-point entirely — critical for deterministic matching.
using Price    = int64_t;   // ticks
using Quantity = uint64_t;
using OrderId  = uint64_t;

// ─── Order ───────────────────────────────────────────────────────────
// Packed for cache-line efficiency. Intrusive doubly-linked list node
// so that cancel is O(1) given the order pointer.
struct Order {
    OrderId   id;
    Price     price;
    Quantity  qty;           // remaining quantity
    Quantity  filled_qty;
    Side      side;
    OrderStatus status;
    uint64_t  timestamp_ns;  // arrival time for price-time priority

    // Intrusive list pointers (within a PriceLevel)
    Order*    prev{nullptr};
    Order*    next{nullptr};

    Order() = default;
    Order(OrderId id_, Price price_, Quantity qty_, Side side_)
        : id(id_), price(price_), qty(qty_), filled_qty(0),
          side(side_), status(OrderStatus::New),
          timestamp_ns(now_ns()) {}
};

// ─── Execution report ────────────────────────────────────────────────
struct Execution {
    OrderId  aggressive_id;   // taker
    OrderId  passive_id;      // maker (resting)
    Price    price;
    Quantity qty;
    uint64_t timestamp_ns;
};

// ─── Order-book snapshot (top-of-book) ───────────────────────────────
struct TopOfBook {
    Price    bid_price;
    Quantity bid_qty;
    Price    ask_price;
    Quantity ask_qty;
    int      bid_levels;
    int      ask_levels;
};

} // namespace ob
