# Order Book Engine

A high-performance limit order book and matching engine written in C++17.

Processes orders in **~280 ns** with price-time priority matching, object-pooled memory (zero allocations on the hot path), and intrusive linked lists for O(1) cancel.

## What Is This?

An order book is the core data structure behind every stock exchange. It maintains a sorted list of buy orders (bids) and sell orders (asks). When a new order's price "crosses" the opposite side, the matching engine pairs them into a trade.

This project implements the full pipeline:

- **Limit order book** with bid/ask sides sorted by price
- **Matching engine** using price-time priority (FIFO at each price level)
- **Order management** — add, cancel, modify
- **Latency measurement** with percentile breakdowns (p50/p95/p99/p99.9)

## Architecture

```
                  ┌──────────────────────────────┐
                  │        OrderBook             │
                  │                              │
  add_order() ──▶ │  ┌─────────┐  ┌──────────┐  │ ──▶ Execution callbacks
  cancel_order()  │  │  Bids   │  │   Asks   │  │
  modify_order()  │  │ (map↓)  │  │  (map↑)  │  │
                  │  │         │  │          │  │
                  │  │ 99.50 ──┤  ├── 100.50 │  │
                  │  │ 99.00 ──┤  ├── 101.00 │  │
                  │  │ 98.50 ──┤  ├── 101.50 │  │
                  │  └─────────┘  └──────────┘  │
                  │                              │
                  │  ObjectPool<Order>            │
                  │  ObjectPool<PriceLevel>       │
                  └──────────────────────────────┘
```

Each price level is an intrusive doubly-linked list of orders (FIFO). The pools pre-allocate contiguous memory at startup so the matching loop never calls `malloc`.

## Building

Requires a C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+).

```bash
# With CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./orderbook

# Or directly with g++
g++ -std=c++17 -O3 -march=native -flto -Iinclude -o orderbook src/main.cpp -lpthread
./orderbook
```

## Benchmark Results

Measured on a single core (your numbers will vary):

| Operation | Avg | Min | Notes |
|-----------|-----|-----|-------|
| `add_order` (no cross) | ~280 ns | ~170 ns | Insert into book |
| `cancel_order` | ~110 ns | ~58 ns | O(1) via intrusive list |
| `match` (per call) | ~78 ns | ~34 ns | Price-time priority walk |
| Hot path (add + cancel) p50 | ~349 ns | | Round-trip baseline |
| Hot path p99 | ~482 ns | | |
| Hot path p99.9 | ~626 ns | | Tail latency |

Throughput: ~4.3M executions/sec on the matching benchmark.

## Project Structure

```
orderbook/
├── include/
│   ├── order.h          # Order, Execution, enums, timestamp utilities
│   ├── price_level.h    # Intrusive linked list of orders at one price
│   ├── object_pool.h    # Pre-allocated memory pool (no malloc on hot path)
│   └── order_book.h     # The matching engine — add/cancel/match/query
├── src/
│   └── main.cpp         # Correctness tests (50) + latency benchmarks
├── CMakeLists.txt
└── .gitignore
```

## Design Decisions

**Why `std::map` for price levels?** The number of active price levels is typically small (< 200). `std::map` gives O(log N) insert/find with free ordered iteration for crossing detection. For the hot path, the constant factors of a sorted array or skip list don't meaningfully beat `std::map` at this N.

**Why intrusive lists instead of `std::list`?** Cancel-by-ID needs O(1) removal given a pointer to the order. `std::list` requires an iterator; intrusive prev/next pointers let us unlink in-place with no container overhead.

**Why single-threaded?** Real exchanges serialize the order stream before it reaches the matching engine (the "Sync & Admin" layer). Lock-free structures add complexity without benefit when the input is already serialized. The engine processes orders faster than any network can deliver them.

**Why fixed-point prices?** Floating-point arithmetic is non-deterministic across platforms and has rounding errors (`0.1 + 0.2 ≠ 0.3`). Financial systems store prices as integer ticks (e.g., cents) to guarantee exact results.

## License

MIT
