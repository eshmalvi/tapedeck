#include "tapedeck/replay/replay_handler.hpp"

#include <cmath>

namespace tapedeck::replay {

// ---------------------------------------------------------------------------
// WatchState
// ---------------------------------------------------------------------------
void WatchState::on_book_changed(const book::OrderBook& book) {
    ++book_updates;
    top = signals::top_of_book(book);
    if (!top.two_sided()) {
        return;
    }
    const double mid = top.mid();
    if (last_mid && *last_mid > 0.0) {
        mid_returns.push(std::log(mid / *last_mid));
    }
    last_mid = mid;
    spread_median.push(top.spread());
    microprice_ewma.push(top.microprice());
}

void WatchState::on_trade(TradePrint print) {
    tape.push_front(print);
    if (tape.size() > kTapeLength) {
        tape.pop_back();
    }
}

// ---------------------------------------------------------------------------
// ReplayHandler
// ---------------------------------------------------------------------------
void ReplayHandler::watch(std::string symbol) {
    watched_symbol_ = std::move(symbol);
    watch_ = WatchState{};
}

void ReplayHandler::refresh_watch(Locate locate) {
    if (is_watched(locate)) {
        watch_.on_book_changed(books_.book(locate));
    }
}

void ReplayHandler::record_trade(Locate locate, Timestamp ts, Price price, Qty qty, char kind) {
    if (is_watched(locate)) {
        watch_.on_trade(TradePrint{ts, price, qty, kind});
    }
}

void ReplayHandler::on_system_event(const itch::SystemEvent& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    last_system_event_ = msg.event_code;
}

void ReplayHandler::on_stock_directory(const itch::StockDirectory& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    books_.apply(msg);
    if (!watch_.resolved && msg.symbol.view() == watched_symbol_) {
        watch_.locate = msg.header.stock_locate;
        watch_.resolved = true;
    }
}

void ReplayHandler::on_trading_action(const itch::TradingAction& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    books_.apply(msg);
    if (is_watched(msg.header.stock_locate)) {
        watch_.trading_state = msg.trading_state;
    }
}

void ReplayHandler::on_add_order(const itch::AddOrder& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    books_.apply(msg);
    refresh_watch(msg.header.stock_locate);
}

void ReplayHandler::on_order_executed(const itch::OrderExecuted& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    if (const auto price = books_.apply(msg)) {
        record_trade(msg.header.stock_locate, msg.header.timestamp, *price, msg.shares, 'E');
    }
    refresh_watch(msg.header.stock_locate);
}

void ReplayHandler::on_order_executed_with_price(const itch::OrderExecutedWithPrice& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    const auto price = books_.apply(msg);
    if (price && msg.printable) {
        record_trade(msg.header.stock_locate, msg.header.timestamp, *price, msg.shares, 'C');
    }
    refresh_watch(msg.header.stock_locate);
}

void ReplayHandler::on_order_cancel(const itch::OrderCancel& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    books_.apply(msg);
    refresh_watch(msg.header.stock_locate);
}

void ReplayHandler::on_order_delete(const itch::OrderDelete& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    books_.apply(msg);
    refresh_watch(msg.header.stock_locate);
}

void ReplayHandler::on_order_replace(const itch::OrderReplace& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    books_.apply(msg);
    refresh_watch(msg.header.stock_locate);
}

void ReplayHandler::on_trade(const itch::Trade& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    // Trades against hidden orders never touch the displayed book; only the tape sees them.
    record_trade(msg.header.stock_locate, msg.header.timestamp, msg.price, msg.shares, 'P');
}

void ReplayHandler::on_cross_trade(const itch::CrossTrade& msg) {
    counts_.count(msg.header.type);
    last_timestamp_ = msg.header.timestamp;
    const auto qty = static_cast<Qty>(std::min<std::uint64_t>(msg.shares, 0xFFFFFFFFu));
    record_trade(msg.header.stock_locate, msg.header.timestamp, msg.price, qty, 'Q');
}

void ReplayHandler::on_other(itch::MessageType type, std::span<const std::byte> payload) {
    counts_.count(type);
    if (payload.size() >= 11) {
        last_timestamp_ = itch::detail::parse_header(payload).timestamp;
    }
}

void ReplayHandler::on_malformed(std::span<const std::byte>) {
    ++counts_.malformed;
    ++counts_.total;
}

}  // namespace tapedeck::replay
