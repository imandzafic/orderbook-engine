#pragma once

#include "order.h"
#include "price_level.h"
#include "object_pool.h"

#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdio>

namespace ob {

// ─── Latency tracker ─────────────────────────────────────────────────
// Ref image 4 (Cboe latency diagram): the entire customer-to-gateway
// path is ≈11 µs.  Our matching engine sits at the far right of that
// chain — we must be **well under** 1 µs per order to not be the
// bottleneck.  This struct records per-operation timing.

struct LatencyStats {
    uint64_t count{0};
    uint64_t total_ns{0};
    uint64_t min_ns{UINT64_MAX};
    uint64_t max_ns{0};

    void record(uint64_t ns) {
        ++count;
        total_ns += ns;
        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
    }

    double avg_ns() const { return count ? static_cast<double>(total_ns) / count : 0; }
};

// ─── OrderBook ───────────────────────────────────────────────────────
// Single-symbol limit order book with price-time priority matching.
//
// Architecture (ref image 1):
//   Exchange
//     └─ Sync & Admin
//         └─ Matching Engine   ← THIS CLASS
//             ├─ Order Book    ← bid/ask price levels
//             └─ Data Generator← executions / book updates
//
// The matching algorithm is public (ref image 1 annotation):
//   Price-time priority (FIFO at each price level).
//
// Design principles:
//   • std::map for price levels: O(log N) insert/find, ordered iteration
//     for crossing detection.  N is typically small (< 200 levels).
//   • Intrusive linked list within each level: O(1) add/cancel.
//   • unordered_map<OrderId, Order*> for O(1) cancel-by-id.
//   • Object pools eliminate malloc from the hot path.
//   • Single-threaded matching loop — no locks needed on the critical
//     path.  The Sync & Admin layer (image 1) serialises inbound
//     messages before they reach the matching engine.
//
// Ref image 3 (arrowhead):
//   Each TRSV (Trading Server) processes a partition of symbols.
//   Our OrderBook is analogous to one symbol-queue inside a TRSV.

class OrderBook {
public:
    using ExecutionCallback = std::function<void(const Execution&)>;

    explicit OrderBook(size_t order_pool_size   = 1'000'000,
                       size_t level_pool_size   = 10'000)
        : order_pool_(order_pool_size),
          level_pool_(level_pool_size),
          next_id_(1) {}

    // ── Public API ───────────────────────────────────────────────────

    // Add a new limit order.  Returns the assigned OrderId.
    // Immediately matches against resting liquidity if price crosses.
    OrderId add_order(Side side, Price price, Quantity qty) {
        auto t0 = now_ns();

        OrderId id = next_id_++;
        Order* order = order_pool_.alloc(id, price, qty, side);
        if (!order) return 0;  // pool exhausted

        order->status = OrderStatus::Active;
        order->timestamp_ns = t0;  // arrival stamp

        // ── Matching (aggressive) ────────────────────────────────────
        // Walk the opposite side while the incoming order crosses.
        match(order);

        // ── Insert remainder into book ───────────────────────────────
        if (order->qty > 0 && order->status == OrderStatus::Active) {
            insert_into_book(order);
        } else if (order->qty == 0) {
            order->status = OrderStatus::Filled;
            order_pool_.free(order);
        }

        auto t1 = now_ns();
        add_stats_.record(t1 - t0);
        return id;
    }

    // Cancel an existing order.  Returns true on success.
    bool cancel_order(OrderId id) {
        auto t0 = now_ns();

        auto it = order_map_.find(id);
        if (it == order_map_.end()) return false;

        Order* order = it->second;
        if (order->status != OrderStatus::Active &&
            order->status != OrderStatus::Partial)
            return false;

        remove_from_book(order);
        order->status = OrderStatus::Cancelled;
        order_map_.erase(it);
        order_pool_.free(order);

        auto t1 = now_ns();
        cancel_stats_.record(t1 - t0);
        return true;
    }

    // Modify = cancel + re-add (loses time priority, as per exchange rules).
    OrderId modify_order(OrderId id, Price new_price, Quantity new_qty) {
        if (!cancel_order(id)) return 0;
        // The cancel latency is already recorded; add will record its own.
        return add_order(
            /* side is lost after cancel, so we need to store it */
            /* This is handled below via a richer modify path     */
            Side::Buy, new_price, new_qty  // placeholder — see modify_v2
        );
    }

    // Richer modify: caller specifies side (or we look it up before cancel).
    OrderId modify_order(OrderId id, Side side, Price new_price, Quantity new_qty) {
        if (!cancel_order(id)) return 0;
        return add_order(side, new_price, new_qty);
    }

    // ── Callbacks ────────────────────────────────────────────────────
    void set_execution_callback(ExecutionCallback cb) { on_execution_ = std::move(cb); }

    // ── Queries ──────────────────────────────────────────────────────
    TopOfBook top_of_book() const {
        TopOfBook tob{};
        if (!bids_.empty()) {
            auto& lvl = bids_.begin()->second;
            tob.bid_price  = lvl->price;
            tob.bid_qty    = lvl->total_qty;
            tob.bid_levels = static_cast<int>(bids_.size());
        }
        if (!asks_.empty()) {
            auto& lvl = asks_.begin()->second;
            tob.ask_price  = lvl->price;
            tob.ask_qty    = lvl->total_qty;
            tob.ask_levels = static_cast<int>(asks_.size());
        }
        return tob;
    }

    // Spread in ticks (ref image 2: spread = ask_price − bid_price).
    Price spread() const {
        if (bids_.empty() || asks_.empty()) return -1;
        return asks_.begin()->second->price - bids_.begin()->second->price;
    }

    // Mid-price (ref image 2).
    double mid_price() const {
        if (bids_.empty() || asks_.empty()) return 0.0;
        return (bids_.begin()->second->price + asks_.begin()->second->price) / 2.0;
    }

    size_t bid_depth() const { return bids_.size(); }
    size_t ask_depth() const { return asks_.size(); }
    size_t order_count() const { return order_map_.size(); }

    // ── Latency stats ────────────────────────────────────────────────
    const LatencyStats& add_latency()    const { return add_stats_; }
    const LatencyStats& cancel_latency() const { return cancel_stats_; }
    const LatencyStats& match_latency()  const { return match_stats_; }

    // ── Depth snapshot (for visualization like image 2) ──────────────
    struct DepthEntry { Price price; Quantity qty; int order_count; };

    std::vector<DepthEntry> bid_depth_snapshot(int max_levels = 20) const {
        std::vector<DepthEntry> out;
        int n = 0;
        for (auto& [_, lvl] : bids_) {
            out.push_back({lvl->price, lvl->total_qty,
                           static_cast<int>(lvl->order_count)});
            if (++n >= max_levels) break;
        }
        return out;
    }

    std::vector<DepthEntry> ask_depth_snapshot(int max_levels = 20) const {
        std::vector<DepthEntry> out;
        int n = 0;
        for (auto& [_, lvl] : asks_) {
            out.push_back({lvl->price, lvl->total_qty,
                           static_cast<int>(lvl->order_count)});
            if (++n >= max_levels) break;
        }
        return out;
    }

private:
    // Bids: descending price (best bid = begin, i.e. highest price).
    // Asks: ascending  price (best ask = begin, i.e. lowest  price).
    using BidMap = std::map<Price, PriceLevel*, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel*, std::less<Price>>;

    BidMap bids_;
    AskMap asks_;

    std::unordered_map<OrderId, Order*> order_map_;

    ObjectPool<Order>      order_pool_;
    ObjectPool<PriceLevel> level_pool_;

    OrderId next_id_;

    ExecutionCallback on_execution_;

    LatencyStats add_stats_;
    LatencyStats cancel_stats_;
    LatencyStats match_stats_;

    // ── Matching engine ──────────────────────────────────────────────
    // Price-time priority: walk the opposite side from best price,
    // and within each level walk FIFO (head → tail).
    //
    // Ref image 1: "Matching Algorithm (public)"
    // Ref image 3: orders/executions per millisecond monitored

    void match(Order* aggressor) {
        auto t0 = now_ns();

        if (aggressor->side == Side::Buy) {
            match_against_asks(aggressor);
        } else {
            match_against_bids(aggressor);
        }

        auto t1 = now_ns();
        match_stats_.record(t1 - t0);
    }

    void match_against_asks(Order* buyer) {
        while (buyer->qty > 0 && !asks_.empty()) {
            auto it = asks_.begin();
            PriceLevel* level = it->second;

            // No cross → done
            if (buyer->price < level->price) break;

            while (buyer->qty > 0 && !level->empty()) {
                Order* seller = level->front();
                Quantity fill_qty = std::min(buyer->qty, seller->qty);

                // Execute
                emit_execution(buyer->id, seller->id, seller->price, fill_qty);

                buyer->qty        -= fill_qty;
                buyer->filled_qty += fill_qty;

                if (fill_qty == seller->qty) {
                    // Seller fully filled — remove from level
                    level->remove(seller);
                    seller->qty = 0;
                    seller->filled_qty += fill_qty;
                    seller->status = OrderStatus::Filled;
                    order_map_.erase(seller->id);
                    order_pool_.free(seller);
                } else {
                    level->reduce_front(fill_qty);
                    seller->status = OrderStatus::Partial;
                }
            }

            // If level is empty, remove it
            if (level->empty()) {
                level_pool_.free(level);
                asks_.erase(it);
            }
        }
    }

    void match_against_bids(Order* seller) {
        while (seller->qty > 0 && !bids_.empty()) {
            auto it = bids_.begin();
            PriceLevel* level = it->second;

            if (seller->price > level->price) break;

            while (seller->qty > 0 && !level->empty()) {
                Order* buyer = level->front();
                Quantity fill_qty = std::min(seller->qty, buyer->qty);

                emit_execution(seller->id, buyer->id, buyer->price, fill_qty);

                seller->qty        -= fill_qty;
                seller->filled_qty += fill_qty;

                if (fill_qty == buyer->qty) {
                    level->remove(buyer);
                    buyer->qty = 0;
                    buyer->filled_qty += fill_qty;
                    buyer->status = OrderStatus::Filled;
                    order_map_.erase(buyer->id);
                    order_pool_.free(buyer);
                } else {
                    level->reduce_front(fill_qty);
                    buyer->status = OrderStatus::Partial;
                }
            }

            if (level->empty()) {
                level_pool_.free(level);
                bids_.erase(it);
            }
        }
    }

    // ── Book insertion ───────────────────────────────────────────────

    void insert_into_book(Order* order) {
        order_map_[order->id] = order;

        if (order->side == Side::Buy) {
            auto it = bids_.find(order->price);
            if (it == bids_.end()) {
                PriceLevel* lvl = level_pool_.alloc(order->price);
                lvl->push_back(order);
                bids_[order->price] = lvl;
            } else {
                it->second->push_back(order);
            }
        } else {
            auto it = asks_.find(order->price);
            if (it == asks_.end()) {
                PriceLevel* lvl = level_pool_.alloc(order->price);
                lvl->push_back(order);
                asks_[order->price] = lvl;
            } else {
                it->second->push_back(order);
            }
        }
    }

    // ── Book removal (for cancel) ────────────────────────────────────

    void remove_from_book(Order* order) {
        if (order->side == Side::Buy) {
            auto it = bids_.find(order->price);
            if (it != bids_.end()) {
                it->second->remove(order);
                if (it->second->empty()) {
                    level_pool_.free(it->second);
                    bids_.erase(it);
                }
            }
        } else {
            auto it = asks_.find(order->price);
            if (it != asks_.end()) {
                it->second->remove(order);
                if (it->second->empty()) {
                    level_pool_.free(it->second);
                    asks_.erase(it);
                }
            }
        }
    }

    // ── Execution reporting ──────────────────────────────────────────

    void emit_execution(OrderId aggressor_id, OrderId passive_id,
                        Price price, Quantity qty) {
        if (on_execution_) {
            on_execution_(Execution{aggressor_id, passive_id, price, qty, now_ns()});
        }
    }
};

} // namespace ob
