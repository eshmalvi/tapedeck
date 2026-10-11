#pragma once

#include "tapedeck/book/book_manager.hpp"
#include "tapedeck/core/types.hpp"
#include "tapedeck/itch/decoder.hpp"
#include "tapedeck/signals/book_signals.hpp"
#include "tapedeck/signals/rolling.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace tapedeck::replay {

// How many messages of each type we have seen, indexed by the type character.
struct MessageCounts {
    std::array<std::uint64_t, 256> by_type{};
    std::uint64_t total = 0;
    std::uint64_t malformed = 0;

    void count(itch::MessageType type) {
        ++by_type[static_cast<unsigned char>(type)];
        ++total;
    }
    std::uint64_t of(itch::MessageType type) const {
        return by_type[static_cast<unsigned char>(type)];
    }
};

// One print on the trade tape of the watched symbol.
struct TradePrint {
    Timestamp timestamp;
    Price price;
    Qty qty;
    char kind;  // 'E' execution, 'C' execution at price, 'P' hidden trade, 'Q' cross
};

// Live view of the one symbol the user is watching: top of book, derived
// signals, a short trade tape and the stock's trading state.
struct WatchState {
    static constexpr std::size_t kTapeLength = 12;
    static constexpr std::size_t kStatsWindow = 100;

    Locate locate = 0;
    bool resolved = false;  // true once the stock directory told us the locate
    signals::TopOfBook top;
    std::uint64_t book_updates = 0;
    char trading_state = '?';

    signals::RollingStats mid_returns{kStatsWindow};  // volatility of mid-price changes
    signals::RollingMedian spread_median{kStatsWindow};
    signals::Ewma microprice_ewma = signals::Ewma::with_half_life(20.0);
    std::optional<double> last_mid;
    std::deque<TradePrint> tape;

    void on_book_changed(const book::OrderBook& book);
    void on_trade(TradePrint print);
};

// Receives decoded messages and applies them to the books.
//
// This is the only handler on the hot path. It does three things per message:
// count it, update the right order book, and, if the message concerns the
// watched symbol, refresh that symbol's signals and tape.
class ReplayHandler : public itch::NullHandler {
 public:
    explicit ReplayHandler(book::BookManager& books) : books_(books) {}

    // Symbol to track for signals and the tape (e.g. "AAPL"). Resolved when its
    // stock directory message arrives.
    void watch(std::string symbol);

    void on_system_event(const itch::SystemEvent& msg);
    void on_stock_directory(const itch::StockDirectory& msg);
    void on_trading_action(const itch::TradingAction& msg);
    void on_add_order(const itch::AddOrder& msg);
    void on_order_executed(const itch::OrderExecuted& msg);
    void on_order_executed_with_price(const itch::OrderExecutedWithPrice& msg);
    void on_order_cancel(const itch::OrderCancel& msg);
    void on_order_delete(const itch::OrderDelete& msg);
    void on_order_replace(const itch::OrderReplace& msg);
    void on_trade(const itch::Trade& msg);
    void on_cross_trade(const itch::CrossTrade& msg);
    void on_other(itch::MessageType type, std::span<const std::byte> payload);
    void on_malformed(std::span<const std::byte> payload);

    const MessageCounts& counts() const { return counts_; }
    const WatchState& watch_state() const { return watch_; }
    const book::BookManager& books() const { return books_; }
    Timestamp last_timestamp() const { return last_timestamp_; }
    char last_system_event() const { return last_system_event_; }

 private:
    bool is_watched(Locate locate) const { return watch_.resolved && watch_.locate == locate; }
    void refresh_watch(Locate locate);
    void record_trade(Locate locate, Timestamp ts, Price price, Qty qty, char kind);

    book::BookManager& books_;
    MessageCounts counts_;
    WatchState watch_;
    std::string watched_symbol_;
    Timestamp last_timestamp_ = 0;
    char last_system_event_ = ' ';
};

}  // namespace tapedeck::replay
