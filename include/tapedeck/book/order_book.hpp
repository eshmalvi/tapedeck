#pragma once

#include "tapedeck/core/types.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace tapedeck::book {

// Aggregated size resting at one price.
struct Level {
    Qty qty = 0;
    std::uint32_t order_count = 0;
};

struct PriceLevel {
    Price price;
    Level level;
};

// One side's best price, or `valid == false` if that side is empty.
struct Quote {
    Price price = 0;
    Qty qty = 0;
    bool valid = false;
};

// What happened when we applied an execution. Used by the correctness oracle.
struct ExecutionResult {
    bool found = false;     // the order id was known
    bool at_touch = false;  // the order rested at its side's best price
    bool overfill = false;  // more shares executed than were resting
    Price price = 0;        // the resting order's display price
    Side side = Side::Buy;
};

// Limit order book for a single symbol.
//
// Two containers do all the work:
//   * `orders_` maps order id -> (side, price, qty) so cancels/executes, which
//     arrive by id, can find their order.
//   * `bids_` / `asks_` keep one aggregated Level per price, sorted so the best
//     price is always at begin().
//
// This is the straightforward v1. The data structures are chosen for clarity
// and will be replaced step by step in the optimisation ladder, with the
// before/after numbers recorded in docs/benchmarks.md.
class OrderBook {
 public:
    // Returns false if the id is already on the book (the message is ignored).
    bool add(OrderId id, Side side, Price price, Qty qty);

    // 'E' and 'C': `qty` shares of the order traded. Removes the order when fully filled.
    ExecutionResult execute(OrderId id, Qty qty);

    // 'X': `qty` shares are cancelled, the order stays. Returns false for an unknown id.
    bool cancel(OrderId id, Qty qty);

    // 'D': the order leaves the book. Returns false for an unknown id.
    bool remove(OrderId id);

    // 'U': the order gets a new id, price and size (and loses queue priority).
    // Returns false if the old id is unknown.
    bool replace(OrderId old_id, OrderId new_id, Price price, Qty qty);

    Quote best_bid() const;
    Quote best_ask() const;
    bool crossed() const;  // best bid >= best ask

    std::size_t order_count() const { return orders_.size(); }
    std::size_t bid_level_count() const { return bids_.size(); }
    std::size_t ask_level_count() const { return asks_.size(); }

    // Best `depth` levels of one side, best first. For display and tests.
    std::vector<PriceLevel> top_levels(Side side, std::size_t depth) const;

 private:
    struct Order {
        Price price;
        Qty qty;
        Side side;
    };

    using Bids = std::map<Price, Level, std::greater<Price>>;  // highest price first
    using Asks = std::map<Price, Level, std::less<Price>>;     // lowest price first

    template <typename Levels>
    static void add_to_level(Levels& levels, Price price, Qty qty);

    template <typename Levels>
    static void reduce_level(Levels& levels, Price price, Qty qty, bool order_gone);

    void add_to_side(Side side, Price price, Qty qty);
    void reduce_side(Side side, Price price, Qty qty, bool order_gone);
    bool is_at_touch(const Order& order) const;

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Order> orders_;
};

}  // namespace tapedeck::book
