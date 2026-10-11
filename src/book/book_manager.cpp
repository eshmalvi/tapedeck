#include "tapedeck/book/book_manager.hpp"

#include <algorithm>

namespace tapedeck::book {

OrderBook& BookManager::book_for(Locate locate) {
    if (locate >= books_.size()) {
        const auto size = static_cast<std::size_t>(locate) + 1;
        books_.resize(size);
        symbols_.resize(size);
        trading_states_.resize(size, '?');
    }
    return books_[locate];
}

// A crossed book is a bug while a stock trades (the matching engine would have
// matched the orders), but normal during a halt or quote-only period, when
// orders accumulate without matching until trading resumes.
void BookManager::note_crossed(const OrderBook& book, Locate locate) {
    if (!book.crossed()) {
        return;
    }
    if (!is_trading(locate)) {
        ++oracle_.crossed_while_halted;
        return;
    }
    ++oracle_.crossed_while_trading;
    auto& seen = oracle_.crossed_locates;
    if (seen.size() < OracleStats::kMaxCrossedLocates &&
        std::find(seen.begin(), seen.end(), locate) == seen.end()) {
        seen.push_back(locate);
    }
}

void BookManager::apply(const itch::StockDirectory& msg) {
    const Locate locate = msg.header.stock_locate;
    book_for(locate);
    if (symbols_[locate].view().empty()) {
        ++symbol_count_;
    }
    symbols_[locate] = msg.symbol;
}

void BookManager::apply(const itch::TradingAction& msg) {
    book_for(msg.header.stock_locate);
    trading_states_[msg.header.stock_locate] = msg.trading_state;
}

char BookManager::trading_state(Locate locate) const {
    return locate < trading_states_.size() ? trading_states_[locate] : '?';
}

void BookManager::apply(const itch::AddOrder& msg) {
    OrderBook& book = book_for(msg.header.stock_locate);
    if (!book.add(msg.order_id, msg.side, msg.price, msg.shares)) {
        ++oracle_.duplicate_order_ids;
        return;
    }
    note_crossed(book, msg.header.stock_locate);
}

std::optional<Price> BookManager::apply(const itch::OrderExecuted& msg) {
    const ExecutionResult result =
        book_for(msg.header.stock_locate).execute(msg.order_id, msg.shares);
    if (!result.found) {
        ++oracle_.unknown_order_ids;
        return std::nullopt;
    }
    ++oracle_.executions;
    oracle_.executions_at_touch += result.at_touch ? 1 : 0;
    oracle_.overfills += result.overfill ? 1 : 0;
    return result.price;
}

std::optional<Price> BookManager::apply(const itch::OrderExecutedWithPrice& msg) {
    const ExecutionResult result =
        book_for(msg.header.stock_locate).execute(msg.order_id, msg.shares);
    if (!result.found) {
        ++oracle_.unknown_order_ids;
        return std::nullopt;
    }
    ++oracle_.executions_with_price;
    oracle_.overfills += result.overfill ? 1 : 0;
    return msg.price;  // 'C' carries its own execution price
}

void BookManager::apply(const itch::OrderCancel& msg) {
    if (!book_for(msg.header.stock_locate).cancel(msg.order_id, msg.shares)) {
        ++oracle_.unknown_order_ids;
    }
}

void BookManager::apply(const itch::OrderDelete& msg) {
    if (!book_for(msg.header.stock_locate).remove(msg.order_id)) {
        ++oracle_.unknown_order_ids;
    }
}

void BookManager::apply(const itch::OrderReplace& msg) {
    OrderBook& book = book_for(msg.header.stock_locate);
    if (!book.replace(msg.old_order_id, msg.new_order_id, msg.price, msg.shares)) {
        ++oracle_.unknown_order_ids;
        return;
    }
    note_crossed(book, msg.header.stock_locate);
}

std::optional<Locate> BookManager::locate_of(std::string_view symbol) const {
    for (std::size_t i = 0; i < symbols_.size(); ++i) {
        if (symbols_[i].view() == symbol) {
            return static_cast<Locate>(i);
        }
    }
    return std::nullopt;
}

Symbol BookManager::symbol_of(Locate locate) const {
    return locate < symbols_.size() ? symbols_[locate] : Symbol{};
}

std::uint64_t BookManager::live_orders() const {
    std::uint64_t total = 0;
    for (const OrderBook& book : books_) {
        total += book.order_count();
    }
    return total;
}

}  // namespace tapedeck::book
