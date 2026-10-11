#pragma once

#include "tapedeck/book/order_book.hpp"
#include "tapedeck/core/types.hpp"

namespace tapedeck::signals {

// Top-of-book snapshot and the short-horizon signals derived from it.
struct TopOfBook {
    book::Quote bid;
    book::Quote ask;

    bool two_sided() const { return bid.valid && ask.valid; }

    double bid_price() const { return price_to_double(bid.price); }
    double ask_price() const { return price_to_double(ask.price); }

    double spread() const { return ask_price() - bid_price(); }
    double mid() const { return (bid_price() + ask_price()) / 2.0; }

    // Size-weighted mid: leans toward the side with less resting size, which is
    // the side more likely to be consumed next. A better "fair price" than mid.
    double microprice() const {
        const double bid_qty = bid.qty;
        const double ask_qty = ask.qty;
        const double total = bid_qty + ask_qty;
        if (total == 0.0) {
            return mid();
        }
        return (bid_price() * ask_qty + ask_price() * bid_qty) / total;
    }

    // In [-1, 1]: +1 means all resting size is on the bid (buying pressure).
    double imbalance() const {
        const double bid_qty = bid.qty;
        const double ask_qty = ask.qty;
        const double total = bid_qty + ask_qty;
        return total == 0.0 ? 0.0 : (bid_qty - ask_qty) / total;
    }
};

inline TopOfBook top_of_book(const book::OrderBook& book) {
    return TopOfBook{book.best_bid(), book.best_ask()};
}

}  // namespace tapedeck::signals
