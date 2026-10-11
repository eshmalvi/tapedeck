// End-to-end: a synthetic trading session goes through source -> frames ->
// decoder -> books -> signals, and the oracle must come out clean.

#include "tapedeck/book/book_manager.hpp"
#include "tapedeck/itch/encoder.hpp"
#include "tapedeck/itch/source.hpp"
#include "tapedeck/replay/replay_handler.hpp"
#include "tapedeck/replay/replayer.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace tapedeck;
using namespace tapedeck::itch;

namespace {

constexpr Locate kAapl = 1;
constexpr Locate kMsft = 2;
constexpr Timestamp kOpen = 9ull * 3600 * kNanosPerSecond + 30 * 60 * kNanosPerSecond;

// Writes a coherent session: directory, a resting book, trades at the touch,
// cancels, a replace, hidden trades and the opening cross.
class SessionBuilder {
 public:
    std::vector<std::byte> bytes;
    Timestamp clock = 4ull * 3600 * kNanosPerSecond;  // 04:00

    Header next(MessageType type, Locate locate) {
        clock += 1000;
        return Header{type, locate, 0, clock};
    }

    void system_event(char code) {
        Encoder::frame(bytes, Encoder::system_event(next(MessageType::SystemEvent, 0), code));
    }

    void directory(Locate locate, const char* symbol) {
        Encoder::frame(bytes, Encoder::stock_directory(next(MessageType::StockDirectory, locate),
                                                       Symbol::from(symbol), 'Q', 100));
    }

    void add(Locate locate, OrderId id, Side side, Qty qty, Price price) {
        Encoder::frame(bytes, Encoder::add_order(next(MessageType::AddOrder, locate), id, side, qty,
                                                 Symbol{}, price));
    }

    void execute(Locate locate, OrderId id, Qty qty) {
        Encoder::frame(bytes, Encoder::order_executed(next(MessageType::OrderExecuted, locate), id,
                                                      qty, ++match_));
    }

    void execute_at(Locate locate, OrderId id, Qty qty, Price price) {
        Encoder::frame(bytes, Encoder::order_executed_with_price(
                                  next(MessageType::OrderExecutedWithPrice, locate), id, qty,
                                  ++match_, true, price));
    }

    void cancel(Locate locate, OrderId id, Qty qty) {
        Encoder::frame(bytes,
                       Encoder::order_cancel(next(MessageType::OrderCancel, locate), id, qty));
    }

    void remove(Locate locate, OrderId id) {
        Encoder::frame(bytes, Encoder::order_delete(next(MessageType::OrderDelete, locate), id));
    }

    void replace(Locate locate, OrderId old_id, OrderId new_id, Qty qty, Price price) {
        Encoder::frame(bytes, Encoder::order_replace(next(MessageType::OrderReplace, locate),
                                                     old_id, new_id, qty, price));
    }

    void hidden_trade(Locate locate, Qty qty, Price price) {
        Encoder::frame(bytes, Encoder::trade(next(MessageType::Trade, locate), 0, Side::Buy, qty,
                                             Symbol{}, price, ++match_));
    }

    void cross(Locate locate, std::uint64_t qty, Price price) {
        Encoder::frame(bytes, Encoder::cross_trade(next(MessageType::CrossTrade, locate), qty,
                                                   Symbol{}, price, ++match_, 'O'));
    }

    void noii(Locate locate) {
        Encoder::frame(bytes, Encoder::other(next(MessageType::Noii, locate), 50));
    }

 private:
    std::uint64_t match_ = 0;
};

SessionBuilder build_session() {
    SessionBuilder s;
    s.system_event('O');
    s.directory(kAapl, "AAPL");
    s.directory(kMsft, "MSFT");
    s.system_event('S');

    // AAPL: bids 150.00 x 300 (two orders), 149.99 x 100; asks 150.02 x 200, 150.05 x 500
    s.add(kAapl, 1, Side::Buy, 200, 1500000);
    s.add(kAapl, 2, Side::Buy, 100, 1500000);
    s.add(kAapl, 3, Side::Buy, 100, 1499900);
    s.add(kAapl, 4, Side::Sell, 200, 1500200);
    s.add(kAapl, 5, Side::Sell, 500, 1500500);
    // MSFT: one-sided then two-sided
    s.add(kMsft, 100, Side::Sell, 50, 3000000);
    s.add(kMsft, 101, Side::Buy, 50, 2999000);

    s.noii(kAapl);
    s.execute(kAapl, 4, 50);   // buyer lifts 50 @ 150.02: ask left 150 (at touch)
    s.execute(kAapl, 1, 200);  // seller hits 200 @ 150.00: order 1 gone, level left 100 (at touch)
    s.cancel(kAapl, 5, 100);   // ask 150.05 down to 400
    s.replace(kAapl, 3, 6, 150, 1500100);  // bid 149.99 -> 150.01 x 150 (new best bid)
    s.hidden_trade(kAapl, 75, 1500150);    // midpoint print, book unchanged
    s.remove(kAapl, 2);                    // level 150.00 disappears; best bid now 150.01 (order 6)
    s.execute(kAapl, 6, 150);  // 150.01 fully filled at touch; best bid now... none at 150.01
    s.add(kAapl, 7, Side::Buy, 400, 1500000);
    s.clock = kOpen;
    s.system_event('Q');
    s.cross(kAapl, 10'000, 1500100);
    s.execute_at(kAapl, 7, 100, 1500100);  // opening cross execution at the cross price
    s.execute(kMsft, 100, 50);             // MSFT ask fully filled
    return s;
}

}  // namespace

TEST(Replay, SyntheticSessionReconstructsBooksAndKeepsOracleClean) {
    SessionBuilder session = build_session();
    MemorySource source(session.bytes, 97);  // odd chunk size: frames straddle chunks
    book::BookManager books;
    replay::ReplayHandler handler(books);
    handler.watch("AAPL");
    replay::Replayer replayer(source, handler, /*measure_latency=*/true);
    replayer.run_all();

    // Every message was processed.
    const auto& counts = handler.counts();
    EXPECT_EQ(counts.total, 24u);
    EXPECT_EQ(counts.malformed, 0u);
    EXPECT_EQ(counts.of(MessageType::AddOrder), 8u);
    EXPECT_EQ(counts.of(MessageType::OrderExecuted), 4u);
    EXPECT_EQ(counts.of(MessageType::Noii), 1u);
    EXPECT_EQ(replayer.stats().messages, 24u);
    EXPECT_EQ(replayer.stats().bytes, session.bytes.size());
    EXPECT_EQ(replayer.stats().latency.count(), 24u);
    EXPECT_TRUE(replayer.finished());
    EXPECT_FALSE(replayer.step());

    // Final AAPL book: bid 150.00 x 300 (order 7), asks 150.02 x 150 and 150.05 x 400.
    const auto& aapl = books.book(kAapl);
    EXPECT_EQ(aapl.best_bid().price, 1500000u);
    EXPECT_EQ(aapl.best_bid().qty, 300u);
    EXPECT_EQ(aapl.best_ask().price, 1500200u);
    EXPECT_EQ(aapl.best_ask().qty, 150u);
    EXPECT_EQ(aapl.order_count(), 3u);
    const auto asks = aapl.top_levels(Side::Sell, 5);
    ASSERT_EQ(asks.size(), 2u);
    EXPECT_EQ(asks[1].price, 1500500u);
    EXPECT_EQ(asks[1].level.qty, 400u);

    // MSFT: ask gone, bid remains.
    const auto& msft = books.book(kMsft);
    EXPECT_FALSE(msft.best_ask().valid);
    EXPECT_EQ(msft.best_bid().price, 2999000u);

    // Oracle: 4 'E' executions, all at the touch; 1 'C'; nothing unknown; no crossed book.
    const auto& oracle = books.oracle();
    EXPECT_EQ(oracle.executions, 4u);
    EXPECT_EQ(oracle.executions_at_touch, 4u);
    EXPECT_EQ(oracle.executions_with_price, 1u);
    EXPECT_EQ(oracle.unknown_order_ids, 0u);
    EXPECT_EQ(oracle.duplicate_order_ids, 0u);
    EXPECT_EQ(oracle.crossed_while_trading, 0u);
    EXPECT_TRUE(oracle.clean());

    // Watched symbol: resolved via the directory, signals and tape populated.
    const auto& watch = handler.watch_state();
    EXPECT_TRUE(watch.resolved);
    EXPECT_EQ(watch.locate, kAapl);
    EXPECT_TRUE(watch.top.two_sided());
    EXPECT_NEAR(watch.top.spread(), 0.02, 1e-9);
    EXPECT_GT(watch.book_updates, 0u);
    ASSERT_EQ(watch.tape.size(), 6u);  // E, E, P, E, Q, C for AAPL (newest first)
    EXPECT_EQ(watch.tape.front().kind, 'C');
    EXPECT_EQ(watch.tape.front().price, 1500100u);
    EXPECT_EQ(watch.tape.back().kind, 'E');
    EXPECT_EQ(watch.tape.back().price, 1500200u);
    EXPECT_EQ(handler.last_system_event(), 'Q');
    EXPECT_EQ(books.symbol_count(), 2u);
}

TEST(Replay, RunUntilStopsAtTimestamp) {
    SessionBuilder session = build_session();
    MemorySource source(session.bytes, 1 << 16);
    book::BookManager books;
    replay::ReplayHandler handler(books);
    replay::Replayer replayer(source, handler, false);

    EXPECT_TRUE(replayer.run_until(kOpen));
    EXPECT_GE(handler.last_timestamp(), kOpen);
    EXPECT_EQ(handler.last_system_event(), 'Q');
    const auto before = replayer.stats().messages;
    EXPECT_LT(before, 24u);

    EXPECT_TRUE(replayer.step());
    EXPECT_EQ(replayer.stats().messages, before + 1);
    EXPECT_FALSE(replayer.run_until(kOpen + 3600 * kNanosPerSecond));  // stream ends first
    EXPECT_EQ(replayer.stats().messages, 24u);
    EXPECT_EQ(replayer.stats().latency.count(), 0u);  // latency measurement was off
}

TEST(Replay, UnknownSymbolIsNeverResolved) {
    SessionBuilder session = build_session();
    MemorySource source(session.bytes, 1 << 16);
    book::BookManager books;
    replay::ReplayHandler handler(books);
    handler.watch("TSLA");
    replay::Replayer replayer(source, handler);
    replayer.run_all();
    EXPECT_FALSE(handler.watch_state().resolved);
    EXPECT_TRUE(handler.watch_state().tape.empty());
    EXPECT_TRUE(books.oracle().clean());
}

TEST(Replay, RandomSessionStaysConsistentWithShadowModel) {
    // Random adds/cancels/deletes/executes on one symbol; executions always hit
    // the touch, so the oracle must agree 100% and live-order counts must match.
    std::mt19937 rng(42);
    SessionBuilder s;
    s.directory(kAapl, "AAPL");
    struct Shadow {
        Side side;
        Price price;
        Qty qty;
    };
    std::vector<std::pair<OrderId, Shadow>> live;
    OrderId next_id = 1;
    std::uniform_int_distribution<int> action(0, 9);
    std::uniform_int_distribution<Price> tick(0, 20);
    std::uniform_int_distribution<Qty> size(1, 500);

    auto best = [&](Side side) -> const std::pair<OrderId, Shadow>* {
        const std::pair<OrderId, Shadow>* found = nullptr;
        for (const auto& entry : live) {
            if (entry.second.side != side)
                continue;
            if (found == nullptr ||
                (side == Side::Buy ? entry.second.price > found->second.price
                                   : entry.second.price < found->second.price)) {
                found = &entry;
            }
        }
        return found;
    };

    std::size_t executions = 0;
    for (int i = 0; i < 5000; ++i) {
        const int a = action(rng);
        if (a < 5 || live.empty()) {
            // Keep the book uncrossed: bids at/below 100.00, asks at/above 100.01.
            const Side side = (a % 2 == 0) ? Side::Buy : Side::Sell;
            const Price price =
                side == Side::Buy ? 1000000 - tick(rng) * 100 : 1000100 + tick(rng) * 100;
            const Qty qty = size(rng);
            s.add(kAapl, next_id, side, qty, price);
            live.emplace_back(next_id, Shadow{side, price, qty});
            ++next_id;
        } else if (a < 7) {
            const auto idx = std::uniform_int_distribution<std::size_t>(0, live.size() - 1)(rng);
            s.remove(kAapl, live[idx].first);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
        } else if (a < 8) {
            const auto idx = std::uniform_int_distribution<std::size_t>(0, live.size() - 1)(rng);
            auto& entry = live[idx];
            const Qty cut = std::uniform_int_distribution<Qty>(1, entry.second.qty)(rng);
            s.cancel(kAapl, entry.first, cut);
            entry.second.qty -= cut;
            if (entry.second.qty == 0)
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
        } else {
            const Side side = (a == 8) ? Side::Buy : Side::Sell;
            if (const auto* top = best(side)) {
                const OrderId id = top->first;
                const Qty fill = std::uniform_int_distribution<Qty>(1, top->second.qty)(rng);
                s.execute(kAapl, id, fill);
                ++executions;
                for (std::size_t k = 0; k < live.size(); ++k) {
                    if (live[k].first == id) {
                        live[k].second.qty -= fill;
                        if (live[k].second.qty == 0)
                            live.erase(live.begin() + static_cast<std::ptrdiff_t>(k));
                        break;
                    }
                }
            }
        }
    }

    MemorySource source(s.bytes, 1 << 20);
    book::BookManager books;
    replay::ReplayHandler handler(books);
    handler.watch("AAPL");
    replay::Replayer replayer(source, handler);
    replayer.run_all();

    const auto& oracle = books.oracle();
    EXPECT_EQ(oracle.executions, executions);
    EXPECT_EQ(oracle.executions_at_touch, executions);
    EXPECT_TRUE(oracle.clean());
    EXPECT_EQ(oracle.crossed_while_trading, 0u);
    EXPECT_EQ(books.book(kAapl).order_count(), live.size());

    Qty shadow_bid_qty = 0;
    Price shadow_bid = 0;
    for (const auto& entry : live) {
        if (entry.second.side == Side::Buy && entry.second.price >= shadow_bid) {
            if (entry.second.price > shadow_bid)
                shadow_bid_qty = 0;
            shadow_bid = entry.second.price;
            shadow_bid_qty += entry.second.qty;
        }
    }
    const auto bid = books.book(kAapl).best_bid();
    EXPECT_EQ(bid.valid, shadow_bid != 0);
    if (bid.valid) {
        EXPECT_EQ(bid.price, shadow_bid);
        EXPECT_EQ(bid.qty, shadow_bid_qty);
    }
}
