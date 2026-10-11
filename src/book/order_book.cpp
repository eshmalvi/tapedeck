#include "tapedeck/book/order_book.hpp"

#include <algorithm>

namespace tapedeck::book {

// ---------------------------------------------------------------------------
// Level bookkeeping
// ---------------------------------------------------------------------------
template <typename Levels>
void OrderBook::add_to_level(Levels& levels, Price price, Qty qty) {
    Level& level = levels[price];  // creates the level if the price is new
    level.qty += qty;
    level.order_count += 1;
}

template <typename Levels>
void OrderBook::reduce_level(Levels& levels, Price price, Qty qty, bool order_gone) {
    auto it = levels.find(price);
    if (it == levels.end()) {
        return;  // cannot happen if orders_ and levels agree; defensive
    }
    Level& level = it->second;
    level.qty -= std::min(level.qty, qty);
    if (order_gone) {
        level.order_count -= 1;
    }
    if (level.order_count == 0) {
        levels.erase(it);  // empty levels never linger: best price must stay meaningful
    }
}

void OrderBook::add_to_side(Side side, Price price, Qty qty) {
    if (side == Side::Buy) {
        add_to_level(bids_, price, qty);
    } else {
        add_to_level(asks_, price, qty);
    }
}

void OrderBook::reduce_side(Side side, Price price, Qty qty, bool order_gone) {
    if (side == Side::Buy) {
        reduce_level(bids_, price, qty, order_gone);
    } else {
        reduce_level(asks_, price, qty, order_gone);
    }
}

bool OrderBook::is_at_touch(const Order& order) const {
    const Quote best = order.side == Side::Buy ? best_bid() : best_ask();
    return best.valid && best.price == order.price;
}

// ---------------------------------------------------------------------------
// Order lifecycle
// ---------------------------------------------------------------------------
bool OrderBook::add(OrderId id, Side side, Price price, Qty qty) {
    const auto [it, inserted] = orders_.try_emplace(id, Order{price, qty, side});
    if (!inserted) {
        return false;
    }
    add_to_side(side, price, qty);
    return true;
}

ExecutionResult OrderBook::execute(OrderId id, Qty qty) {
    ExecutionResult result;
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return result;
    }
    Order& order = it->second;
    result.found = true;
    result.price = order.price;
    result.side = order.side;
    result.at_touch = is_at_touch(order);
    result.overfill = qty > order.qty;

    const Qty filled = std::min(qty, order.qty);
    order.qty -= filled;
    const bool gone = order.qty == 0;
    reduce_side(order.side, order.price, filled, gone);
    if (gone) {
        orders_.erase(it);
    }
    return result;
}

bool OrderBook::cancel(OrderId id, Qty qty) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }
    Order& order = it->second;
    const Qty cancelled = std::min(qty, order.qty);
    order.qty -= cancelled;
    const bool gone = order.qty == 0;  // a cancel of all remaining shares empties the order
    reduce_side(order.side, order.price, cancelled, gone);
    if (gone) {
        orders_.erase(it);
    }
    return true;
}

bool OrderBook::remove(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }
    const Order order = it->second;
    orders_.erase(it);
    reduce_side(order.side, order.price, order.qty, true);
    return true;
}

bool OrderBook::replace(OrderId old_id, OrderId new_id, Price price, Qty qty) {
    auto it = orders_.find(old_id);
    if (it == orders_.end()) {
        return false;
    }
    const Side side = it->second.side;  // a replace keeps the side
    remove(old_id);
    add(new_id, side, price, qty);
    return true;
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------
Quote OrderBook::best_bid() const {
    if (bids_.empty()) {
        return {};
    }
    const auto& [price, level] = *bids_.begin();
    return Quote{price, level.qty, true};
}

Quote OrderBook::best_ask() const {
    if (asks_.empty()) {
        return {};
    }
    const auto& [price, level] = *asks_.begin();
    return Quote{price, level.qty, true};
}

bool OrderBook::crossed() const {
    const Quote bid = best_bid();
    const Quote ask = best_ask();
    return bid.valid && ask.valid && bid.price >= ask.price;
}

std::vector<PriceLevel> OrderBook::top_levels(Side side, std::size_t depth) const {
    std::vector<PriceLevel> out;
    out.reserve(depth);
    auto collect = [&](const auto& levels) {
        for (const auto& [price, level] : levels) {
            if (out.size() == depth) {
                break;
            }
            out.push_back(PriceLevel{price, level});
        }
    };
    if (side == Side::Buy) {
        collect(bids_);
    } else {
        collect(asks_);
    }
    return out;
}

}  // namespace tapedeck::book
