#pragma once

#include "tapedeck/book/order_book.hpp"
#include "tapedeck/core/types.hpp"
#include "tapedeck/itch/messages.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace tapedeck::book {

// Evidence that the reconstruction is right, gathered while replaying.
//
// The exchange never tells us what the book looks like, but it does tell us
// facts we can check against our own state:
//   * Every execute/cancel/delete/replace names an order we must already hold.
//   * An 'E' execution of a displayed order happens at the best price on its
//     side (price priority), so the executed order must be at our touch.
//   * The displayed book must never be crossed while trading is open.
struct OracleStats {
    std::uint64_t executions = 0;           // 'E' messages applied
    std::uint64_t executions_at_touch = 0;  // ... whose order was at our best price
    std::uint64_t executions_with_price =
        0;                                  // 'C' messages applied (cross trades: price can differ)
    std::uint64_t unknown_order_ids = 0;    // E/C/X/D/U naming an id we do not hold
    std::uint64_t duplicate_order_ids = 0;  // A/F reusing an id already on the book
    std::uint64_t overfills = 0;            // executions larger than the resting size
    std::uint64_t crossed_while_trading =
        0;  // best bid >= best ask while the stock was open for trading
    std::uint64_t crossed_while_halted =
        0;  // same, but during a halt / quote-only period (legitimate)
    std::vector<Locate> crossed_locates;  // which symbols crossed while trading (first few, unique)

    static constexpr std::size_t kMaxCrossedLocates = 32;

    double touch_agreement() const {
        return executions == 0
                   ? 1.0
                   : static_cast<double>(executions_at_touch) / static_cast<double>(executions);
    }
    bool clean() const {
        return executions_at_touch == executions && unknown_order_ids == 0 &&
               duplicate_order_ids == 0 && overfills == 0 && crossed_while_trading == 0;
    }
};

// Owns one OrderBook per symbol and routes ITCH messages to the right one.
//
// Books are indexed by the message's stock locate, a small integer the
// exchange assigns per symbol per day, so routing is a vector lookup.
class BookManager {
 public:
    void apply(const itch::StockDirectory& msg);
    void apply(const itch::TradingAction& msg);
    void apply(const itch::AddOrder& msg);
    // Return the execution price (the resting order's price for 'E'), or nullopt if unknown.
    std::optional<Price> apply(const itch::OrderExecuted& msg);
    std::optional<Price> apply(const itch::OrderExecutedWithPrice& msg);
    void apply(const itch::OrderCancel& msg);
    void apply(const itch::OrderDelete& msg);
    void apply(const itch::OrderReplace& msg);

    const OrderBook& book(Locate locate) const { return books_[locate]; }
    bool has_book(Locate locate) const { return locate < books_.size(); }

    std::optional<Locate> locate_of(std::string_view symbol) const;
    Symbol symbol_of(Locate locate) const;
    std::size_t symbol_count() const { return symbol_count_; }

    // 'T' trading, 'H' halted, 'P' paused, 'Q' quotation only; '?' before any trading action.
    char trading_state(Locate locate) const;
    bool is_trading(Locate locate) const { return trading_state(locate) == 'T'; }

    const OracleStats& oracle() const { return oracle_; }
    std::uint64_t live_orders() const;

 private:
    OrderBook& book_for(Locate locate);
    void note_crossed(const OrderBook& book, Locate locate);

    std::vector<OrderBook> books_;
    std::vector<Symbol> symbols_;
    std::vector<char> trading_states_;
    std::size_t symbol_count_ = 0;
    OracleStats oracle_;
};

}  // namespace tapedeck::book
