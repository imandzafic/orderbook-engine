#include "order_book.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <algorithm>
#include <cassert>
#include <string>
#include <numeric>

using namespace ob;

// ═════════════════════════════════════════════════════════════════════
//  ANSI colors for terminal output
// ═════════════════════════════════════════════════════════════════════
namespace col {
    const char* RST  = "\033[0m";
    const char* BOLD = "\033[1m";
    const char* DIM  = "\033[2m";
    const char* GRN  = "\033[32m";
    const char* RED  = "\033[31m";
    const char* YEL  = "\033[33m";
    const char* CYN  = "\033[36m";
    const char* MAG  = "\033[35m";
    const char* BLU  = "\033[34m";
}

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << col::RED << "  ✗ FAIL: " << msg                    \
                      << " (" #cond ")" << col::RST << "\n";               \
            ++tests_failed;                                                 \
        } else {                                                            \
            std::cout << col::GRN << "  ✓ " << msg << col::RST << "\n";    \
            ++tests_passed;                                                 \
        }                                                                   \
    } while (0)

// ═════════════════════════════════════════════════════════════════════
//  CORRECTNESS TESTS
// ═════════════════════════════════════════════════════════════════════

void test_basic_add() {
    std::cout << col::BOLD << "\n── Basic Add ──" << col::RST << "\n";
    OrderBook book;

    auto id1 = book.add_order(Side::Buy,  100, 10);
    auto id2 = book.add_order(Side::Sell, 105, 20);

    CHECK(id1 > 0, "Buy order assigned valid ID");
    CHECK(id2 > 0, "Sell order assigned valid ID");
    CHECK(book.order_count() == 2, "Two orders resting");
    CHECK(book.bid_depth() == 1, "One bid level");
    CHECK(book.ask_depth() == 1, "One ask level");

    auto tob = book.top_of_book();
    CHECK(tob.bid_price == 100, "Best bid = 100");
    CHECK(tob.ask_price == 105, "Best ask = 105");
    CHECK(tob.bid_qty == 10, "Bid qty = 10");
    CHECK(tob.ask_qty == 20, "Ask qty = 20");
}

void test_spread_and_mid() {
    std::cout << col::BOLD << "\n── Spread & Mid-price ──" << col::RST << "\n";
    OrderBook book;

    book.add_order(Side::Buy,  100, 10);
    book.add_order(Side::Sell, 110, 10);

    CHECK(book.spread() == 10, "Spread = 10 ticks");
    CHECK(book.mid_price() == 105.0, "Mid-price = 105.0");
}

void test_full_match() {
    std::cout << col::BOLD << "\n── Full Match ──" << col::RST << "\n";
    OrderBook book;

    std::vector<Execution> execs;
    book.set_execution_callback([&](const Execution& e) { execs.push_back(e); });

    book.add_order(Side::Sell, 100, 50);    // resting ask
    book.add_order(Side::Buy,  100, 50);    // crosses → full fill

    CHECK(execs.size() == 1, "One execution");
    CHECK(execs[0].price == 100, "Exec price = 100 (passive price)");
    CHECK(execs[0].qty == 50, "Exec qty = 50");
    CHECK(book.order_count() == 0, "Book empty after full match");
}

void test_partial_fill() {
    std::cout << col::BOLD << "\n── Partial Fill ──" << col::RST << "\n";
    OrderBook book;

    std::vector<Execution> execs;
    book.set_execution_callback([&](const Execution& e) { execs.push_back(e); });

    book.add_order(Side::Sell, 100, 100);   // resting 100
    book.add_order(Side::Buy,  100, 30);    // takes 30

    CHECK(execs.size() == 1, "One execution");
    CHECK(execs[0].qty == 30, "Partial fill qty = 30");
    CHECK(book.order_count() == 1, "One order still resting");

    auto tob = book.top_of_book();
    CHECK(tob.ask_qty == 70, "Remaining ask qty = 70");
}

void test_price_time_priority() {
    std::cout << col::BOLD << "\n── Price-Time Priority ──" << col::RST << "\n";
    OrderBook book;

    std::vector<Execution> execs;
    book.set_execution_callback([&](const Execution& e) { execs.push_back(e); });

    auto id1 = book.add_order(Side::Sell, 100, 10);  // first at 100
    auto id2 = book.add_order(Side::Sell, 100, 10);  // second at 100
    auto id3 = book.add_order(Side::Sell,  99, 10);  // better price

    // Aggressive buy sweeps: should hit 99 first, then 100 (FIFO)
    book.add_order(Side::Buy, 100, 25);

    CHECK(execs.size() == 3, "Three executions");
    CHECK(execs[0].passive_id == id3, "First fill: best price (99)");
    CHECK(execs[0].price == 99, "Fill at 99");
    CHECK(execs[1].passive_id == id1, "Second fill: first-in at 100");
    CHECK(execs[2].passive_id == id2, "Third fill: second-in at 100");
    CHECK(execs[2].qty == 5, "Last fill partial: 5 of 10");
}

void test_cancel() {
    std::cout << col::BOLD << "\n── Cancel ──" << col::RST << "\n";
    OrderBook book;

    auto id1 = book.add_order(Side::Buy, 100, 10);
    auto id2 = book.add_order(Side::Buy, 100, 20);

    CHECK(book.cancel_order(id1), "Cancel first order");
    CHECK(book.order_count() == 1, "One order remains");
    CHECK(!book.cancel_order(id1), "Double cancel fails");

    auto tob = book.top_of_book();
    CHECK(tob.bid_qty == 20, "Remaining qty after cancel");
}

void test_cancel_then_match() {
    std::cout << col::BOLD << "\n── Cancel Then Match ──" << col::RST << "\n";
    OrderBook book;

    std::vector<Execution> execs;
    book.set_execution_callback([&](const Execution& e) { execs.push_back(e); });

    auto id1 = book.add_order(Side::Sell, 100, 10);
    auto id2 = book.add_order(Side::Sell, 100, 10);

    book.cancel_order(id1);  // cancel first-in

    book.add_order(Side::Buy, 100, 10);  // should match id2

    CHECK(execs.size() == 1, "One execution after cancel");
    CHECK(execs[0].passive_id == id2, "Matched against surviving order");
}

void test_multi_level_sweep() {
    std::cout << col::BOLD << "\n── Multi-Level Sweep ──" << col::RST << "\n";
    OrderBook book;

    std::vector<Execution> execs;
    book.set_execution_callback([&](const Execution& e) { execs.push_back(e); });

    book.add_order(Side::Sell, 100, 10);
    book.add_order(Side::Sell, 101, 10);
    book.add_order(Side::Sell, 102, 10);

    // Aggressive buy sweeps all 3 levels
    book.add_order(Side::Buy, 102, 30);

    CHECK(execs.size() == 3, "Three fills across 3 levels");
    CHECK(execs[0].price == 100, "First fill at best ask");
    CHECK(execs[1].price == 101, "Second fill at next level");
    CHECK(execs[2].price == 102, "Third fill at worst level");
    CHECK(book.ask_depth() == 0, "All ask levels consumed");
}

void test_no_cross() {
    std::cout << col::BOLD << "\n── No Cross (orders rest) ──" << col::RST << "\n";
    OrderBook book;

    std::vector<Execution> execs;
    book.set_execution_callback([&](const Execution& e) { execs.push_back(e); });

    book.add_order(Side::Buy,  100, 10);
    book.add_order(Side::Sell, 101, 10);

    CHECK(execs.empty(), "No executions when spread > 0");
    CHECK(book.order_count() == 2, "Both orders resting");
}

void test_modify_order() {
    std::cout << col::BOLD << "\n── Modify Order ──" << col::RST << "\n";
    OrderBook book;

    auto id1 = book.add_order(Side::Buy, 100, 10);
    auto id2 = book.modify_order(id1, Side::Buy, 105, 20);

    CHECK(id2 > 0, "Modify returns new ID");
    CHECK(id2 != id1, "New ID differs from old");
    CHECK(book.order_count() == 1, "Still one order");

    auto tob = book.top_of_book();
    CHECK(tob.bid_price == 105, "Price updated to 105");
    CHECK(tob.bid_qty == 20, "Qty updated to 20");
}

void test_depth_snapshot() {
    std::cout << col::BOLD << "\n── Depth Snapshot ──" << col::RST << "\n";
    OrderBook book;

    book.add_order(Side::Buy,  100, 10);
    book.add_order(Side::Buy,  100, 20);
    book.add_order(Side::Buy,   99, 15);
    book.add_order(Side::Sell, 101, 25);
    book.add_order(Side::Sell, 102, 30);

    auto bids = book.bid_depth_snapshot();
    auto asks = book.ask_depth_snapshot();

    CHECK(bids.size() == 2, "Two bid levels");
    CHECK(bids[0].price == 100, "Best bid = 100");
    CHECK(bids[0].qty == 30, "Aggregated bid qty at 100 = 30");
    CHECK(bids[0].order_count == 2, "Two orders at best bid");
    CHECK(bids[1].price == 99, "Second bid = 99");
    CHECK(asks.size() == 2, "Two ask levels");
    CHECK(asks[0].price == 101, "Best ask = 101");
}

// ═════════════════════════════════════════════════════════════════════
//  LATENCY BENCHMARKS
// ═════════════════════════════════════════════════════════════════════

void print_stats(const char* label, const LatencyStats& s) {
    if (s.count == 0) return;
    std::cout << col::CYN << "    " << std::setw(16) << std::left << label
              << col::RST
              << "  avg " << col::BOLD << std::setw(8) << std::right
              << static_cast<uint64_t>(s.avg_ns()) << " ns" << col::RST
              << "  min " << std::setw(6) << s.min_ns << " ns"
              << "  max " << std::setw(8) << s.max_ns << " ns"
              << "  (n=" << s.count << ")"
              << "\n";
}

void benchmark_add_cancel(int n_orders) {
    std::cout << col::BOLD << col::MAG
              << "\n══ Benchmark: " << n_orders << " add + cancel ══"
              << col::RST << "\n";

    OrderBook book;
    std::mt19937 rng(42);
    std::uniform_int_distribution<Price> price_dist(9900, 10100);
    std::uniform_int_distribution<Quantity> qty_dist(1, 100);

    std::vector<OrderId> ids;
    ids.reserve(n_orders);

    // Add orders (no crossing — bids below 10000, asks above)
    auto t0 = now_ns();
    for (int i = 0; i < n_orders; ++i) {
        Price p = price_dist(rng);
        Quantity q = qty_dist(rng);
        Side s = (p < 10000) ? Side::Buy : Side::Sell;
        ids.push_back(book.add_order(s, p, q));
    }
    auto t1 = now_ns();

    std::cout << col::DIM << "    Wall time (adds): "
              << (t1 - t0) / 1000 << " µs" << col::RST << "\n";
    print_stats("add_order", book.add_latency());

    // Cancel all
    auto t2 = now_ns();
    for (auto id : ids) {
        book.cancel_order(id);
    }
    auto t3 = now_ns();

    std::cout << col::DIM << "    Wall time (cancels): "
              << (t3 - t2) / 1000 << " µs" << col::RST << "\n";
    print_stats("cancel_order", book.cancel_latency());
}

void benchmark_matching(int n_orders) {
    std::cout << col::BOLD << col::MAG
              << "\n══ Benchmark: " << n_orders << " crossing orders ══"
              << col::RST << "\n";

    OrderBook book;
    int exec_count = 0;
    book.set_execution_callback([&](const Execution&) { ++exec_count; });

    std::mt19937 rng(123);
    std::uniform_int_distribution<Quantity> qty_dist(1, 50);

    // Seed the book with asks
    for (int i = 0; i < n_orders; ++i) {
        book.add_order(Side::Sell, 10000 + (i % 50), qty_dist(rng));
    }

    // Now send crossing buys
    auto t0 = now_ns();
    for (int i = 0; i < n_orders; ++i) {
        book.add_order(Side::Buy, 10000 + (i % 50), qty_dist(rng));
    }
    auto t1 = now_ns();

    std::cout << col::DIM << "    Wall time: "
              << (t1 - t0) / 1000 << " µs" << col::RST << "\n";
    std::cout << col::DIM << "    Executions: " << exec_count << col::RST << "\n";
    print_stats("add_order", book.add_latency());
    print_stats("match", book.match_latency());
}

void benchmark_hot_path(int iterations) {
    std::cout << col::BOLD << col::MAG
              << "\n══ Benchmark: Hot path (add+immediate cancel) × "
              << iterations << " ══" << col::RST << "\n";

    OrderBook book;
    std::vector<uint64_t> latencies;
    latencies.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto t0 = now_ns();
        auto id = book.add_order(Side::Buy, 9900, 10);
        book.cancel_order(id);
        auto t1 = now_ns();
        latencies.push_back(t1 - t0);
    }

    std::sort(latencies.begin(), latencies.end());

    auto percentile = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(p / 100.0 * latencies.size());
        if (idx >= latencies.size()) idx = latencies.size() - 1;
        return latencies[idx];
    };

    uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0ULL);
    double avg = static_cast<double>(sum) / iterations;

    std::cout << col::CYN
              << "    avg  = " << static_cast<uint64_t>(avg) << " ns\n"
              << "    p50  = " << percentile(50) << " ns\n"
              << "    p95  = " << percentile(95) << " ns\n"
              << "    p99  = " << percentile(99) << " ns\n"
              << "    p999 = " << percentile(99.9) << " ns\n"
              << "    min  = " << latencies.front() << " ns\n"
              << "    max  = " << latencies.back() << " ns"
              << col::RST << "\n";
}

// ═════════════════════════════════════════════════════════════════════
//  MAIN
// ═════════════════════════════════════════════════════════════════════

int main() {
    std::cout << col::BOLD << col::BLU
              << R"(
  ╔══════════════════════════════════════════════╗
  ║     ORDER BOOK ENGINE — Test & Benchmark     ║
  ╠══════════════════════════════════════════════╣
  ║  Price-time priority · Lock-free hot path    ║
  ║  Object-pooled · Cache-line aware            ║
  ╚══════════════════════════════════════════════╝
)" << col::RST;

    // ── Correctness tests ────────────────────────────────────────────
    std::cout << col::BOLD << col::YEL
              << "━━━ CORRECTNESS TESTS ━━━" << col::RST << "\n";

    test_basic_add();
    test_spread_and_mid();
    test_full_match();
    test_partial_fill();
    test_price_time_priority();
    test_cancel();
    test_cancel_then_match();
    test_multi_level_sweep();
    test_no_cross();
    test_modify_order();
    test_depth_snapshot();

    std::cout << "\n" << col::BOLD;
    if (tests_failed == 0) {
        std::cout << col::GRN << "All " << tests_passed << " tests passed ✓"
                  << col::RST << "\n";
    } else {
        std::cout << col::RED << tests_failed << " test(s) FAILED, "
                  << tests_passed << " passed" << col::RST << "\n";
    }

    // ── Benchmarks ───────────────────────────────────────────────────
    std::cout << col::BOLD << col::YEL
              << "\n━━━ LATENCY BENCHMARKS ━━━" << col::RST << "\n";

    benchmark_add_cancel(100'000);
    benchmark_matching(100'000);
    benchmark_hot_path(1'000'000);

    std::cout << col::BOLD << col::BLU
              << "\n  Done.\n" << col::RST;

    return tests_failed > 0 ? 1 : 0;
}
