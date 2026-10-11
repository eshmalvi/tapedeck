#include "tapedeck/book/book_manager.hpp"
#include "tapedeck/book/order_book.hpp"

#include <gtest/gtest.h>

using namespace tapedeck;
using namespace tapedeck::book;

TEST(OrderBook, EmptyBookHasNoQuotes) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid().valid);
    EXPECT_FALSE(book.best_ask().valid);
    EXPECT_FALSE(book.crossed());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBook, AddAggregatesByPriceAndKeepsBestFirst) {
    OrderBook book;
    ASSERT_TRUE(book.add(1, Side::Buy, 1000000, 100));
    ASSERT_TRUE(book.add(2, Side::Buy, 1000100, 200));  // better bid
    ASSERT_TRUE(book.add(3, Side::Buy, 1000100, 50));   // joins the best level
    ASSERT_TRUE(book.add(4, Side::Sell, 1000300, 75));
    ASSERT_TRUE(book.add(5, Side::Sell, 1000200, 25));  // better ask

    const Quote bid = book.best_bid();
    EXPECT_TRUE(bid.valid);
    EXPECT_EQ(bid.price, 1000100u);
    EXPECT_EQ(bid.qty, 250u);

    const Quote ask = book.best_ask();
    EXPECT_TRUE(ask.valid);
    EXPECT_EQ(ask.price, 1000200u);
    EXPECT_EQ(ask.qty, 25u);

    EXPECT_EQ(book.bid_level_count(), 2u);
    EXPECT_EQ(book.ask_level_count(), 2u);
    EXPECT_FALSE(book.crossed());

    const auto bids = book.top_levels(Side::Buy, 5);
    ASSERT_EQ(bids.size(), 2u);
    EXPECT_EQ(bids[0].price, 1000100u);
    EXPECT_EQ(bids[0].level.order_count, 2u);
    EXPECT_EQ(bids[1].price, 1000000u);

    const auto asks = book.top_levels(Side::Sell, 1);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].price, 1000200u);
}

TEST(OrderBook, DuplicateIdIsRejected) {
    OrderBook book;
    EXPECT_TRUE(book.add(1, Side::Buy, 1000000, 100));
    EXPECT_FALSE(book.add(1, Side::Sell, 2000000, 100));
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_FALSE(book.best_ask().valid);
}

TEST(OrderBook, PartialExecutionReducesSizeFullExecutionRemovesOrder) {
    OrderBook book;
    book.add(1, Side::Sell, 1000200, 100);
    book.add(2, Side::Sell, 1000200, 100);

    ExecutionResult r1 = book.execute(1, 40);
    EXPECT_TRUE(r1.found);
    EXPECT_TRUE(r1.at_touch);
    EXPECT_FALSE(r1.overfill);
    EXPECT_EQ(r1.price, 1000200u);
    EXPECT_EQ(r1.side, Side::Sell);
    EXPECT_EQ(book.best_ask().qty, 160u);
    EXPECT_EQ(book.order_count(), 2u);

    ExecutionResult r2 = book.execute(1, 60);
    EXPECT_TRUE(r2.found);
    EXPECT_EQ(book.best_ask().qty, 100u);
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_EQ(book.ask_level_count(), 1u);

    book.execute(2, 100);
    EXPECT_FALSE(book.best_ask().valid);
    EXPECT_EQ(book.ask_level_count(), 0u);
}

TEST(OrderBook, ExecutionAwayFromTouchIsFlagged) {
    OrderBook book;
    book.add(1, Side::Buy, 1000100, 100);  // best bid
    book.add(2, Side::Buy, 1000000, 100);  // behind
    const ExecutionResult r = book.execute(2, 10);
    EXPECT_TRUE(r.found);
    EXPECT_FALSE(r.at_touch);
}

TEST(OrderBook, OverfillIsFlaggedAndClamped) {
    OrderBook book;
    book.add(1, Side::Buy, 1000000, 100);
    const ExecutionResult r = book.execute(1, 150);
    EXPECT_TRUE(r.overfill);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_bid().valid);
}

TEST(OrderBook, UnknownIdsAreReported) {
    OrderBook book;
    EXPECT_FALSE(book.execute(99, 1).found);
    EXPECT_FALSE(book.cancel(99, 1));
    EXPECT_FALSE(book.remove(99));
    EXPECT_FALSE(book.replace(99, 100, 1, 1));
}

TEST(OrderBook, CancelReducesAndMayEmptyAnOrder) {
    OrderBook book;
    book.add(1, Side::Buy, 1000000, 100);
    EXPECT_TRUE(book.cancel(1, 30));
    EXPECT_EQ(book.best_bid().qty, 70u);
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_TRUE(book.cancel(1, 70));
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_EQ(book.bid_level_count(), 0u);
}

TEST(OrderBook, DeleteRemovesOrderAndEmptyLevel) {
    OrderBook book;
    book.add(1, Side::Buy, 1000000, 100);
    book.add(2, Side::Buy, 1000000, 100);
    EXPECT_TRUE(book.remove(1));
    EXPECT_EQ(book.best_bid().qty, 100u);
    EXPECT_EQ(book.bid_level_count(), 1u);
    EXPECT_TRUE(book.remove(2));
    EXPECT_EQ(book.bid_level_count(), 0u);
}

TEST(OrderBook, ReplaceKeepsSideAndMovesPrice) {
    OrderBook book;
    book.add(1, Side::Sell, 1000500, 100);
    EXPECT_TRUE(book.replace(1, 2, 1000400, 150));
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_FALSE(book.execute(1, 1).found);  // old id retired
    const Quote ask = book.best_ask();
    EXPECT_EQ(ask.price, 1000400u);
    EXPECT_EQ(ask.qty, 150u);
    EXPECT_EQ(book.ask_level_count(), 1u);
    EXPECT_FALSE(book.best_bid().valid);
}

TEST(OrderBook, DetectsCrossedBook) {
    OrderBook book;
    book.add(1, Side::Buy, 1000500, 100);
    book.add(2, Side::Sell, 1000400, 100);
    EXPECT_TRUE(book.crossed());
    book.remove(2);
    book.add(3, Side::Sell, 1000500, 100);  // locked
    EXPECT_TRUE(book.crossed());
    book.remove(3);
    book.add(4, Side::Sell, 1000600, 100);
    EXPECT_FALSE(book.crossed());
}

// ---------------------------------------------------------------------------
// BookManager routes by stock locate and tallies the oracle.
// ---------------------------------------------------------------------------
namespace {

itch::Header hdr(itch::MessageType type, Locate locate) {
    return itch::Header{type, locate, 0, 1000};
}

}  // namespace

TEST(BookManager, RoutesBySymbolLocate) {
    BookManager books;
    books.apply(itch::StockDirectory{hdr(itch::MessageType::StockDirectory, 7),
                                     Symbol::from("AAPL"), 'Q', 100});
    books.apply(itch::StockDirectory{hdr(itch::MessageType::StockDirectory, 9),
                                     Symbol::from("MSFT"), 'Q', 100});
    EXPECT_EQ(books.symbol_count(), 2u);
    EXPECT_EQ(books.locate_of("MSFT"), std::optional<Locate>(9));
    EXPECT_FALSE(books.locate_of("TSLA"));
    EXPECT_EQ(books.symbol_of(7).view(), "AAPL");

    books.apply(itch::AddOrder{hdr(itch::MessageType::AddOrder, 7),
                               1,
                               Side::Buy,
                               100,
                               Symbol::from("AAPL"),
                               1000000,
                               false,
                               {}});
    books.apply(itch::AddOrder{hdr(itch::MessageType::AddOrder, 9),
                               2,
                               Side::Sell,
                               50,
                               Symbol::from("MSFT"),
                               3000000,
                               false,
                               {}});
    EXPECT_EQ(books.book(7).best_bid().price, 1000000u);
    EXPECT_FALSE(books.book(7).best_ask().valid);
    EXPECT_EQ(books.book(9).best_ask().price, 3000000u);
    EXPECT_EQ(books.live_orders(), 2u);
}

TEST(BookManager, OracleCountsAgreementAndAnomalies) {
    BookManager books;
    books.apply(
        itch::TradingAction{hdr(itch::MessageType::TradingAction, 1), Symbol::from("X"), 'T', {}});
    EXPECT_TRUE(books.is_trading(1));
    books.apply(itch::AddOrder{hdr(itch::MessageType::AddOrder, 1),
                               10,
                               Side::Buy,
                               100,
                               Symbol::from("X"),
                               1000100,
                               false,
                               {}});
    books.apply(itch::AddOrder{hdr(itch::MessageType::AddOrder, 1),
                               11,
                               Side::Buy,
                               100,
                               Symbol::from("X"),
                               1000000,
                               false,
                               {}});

    // Execution at the touch: agreement.
    auto price =
        books.apply(itch::OrderExecuted{hdr(itch::MessageType::OrderExecuted, 1), 10, 40, 1});
    EXPECT_EQ(price, std::optional<Price>(1000100));
    // Execution behind the touch: disagreement.
    books.apply(itch::OrderExecuted{hdr(itch::MessageType::OrderExecuted, 1), 11, 10, 2});
    // Unknown id.
    EXPECT_FALSE(
        books.apply(itch::OrderExecuted{hdr(itch::MessageType::OrderExecuted, 1), 99, 10, 3}));
    books.apply(itch::OrderDelete{hdr(itch::MessageType::OrderDelete, 1), 98});
    // Duplicate add.
    books.apply(itch::AddOrder{
        hdr(itch::MessageType::AddOrder, 1), 10, Side::Buy, 1, Symbol::from("X"), 1, false, {}});
    // Crossing ask.
    books.apply(itch::AddOrder{hdr(itch::MessageType::AddOrder, 1),
                               12,
                               Side::Sell,
                               1,
                               Symbol::from("X"),
                               1000000,
                               false,
                               {}});
    // 'C' carries its own price and is counted separately.
    price = books.apply(itch::OrderExecutedWithPrice{
        hdr(itch::MessageType::OrderExecutedWithPrice, 1), 11, 10, 4, true, 1234567});
    EXPECT_EQ(price, std::optional<Price>(1234567));

    const OracleStats& o = books.oracle();
    EXPECT_EQ(o.executions, 2u);
    EXPECT_EQ(o.executions_at_touch, 1u);
    EXPECT_EQ(o.executions_with_price, 1u);
    EXPECT_EQ(o.unknown_order_ids, 2u);
    EXPECT_EQ(o.duplicate_order_ids, 1u);
    EXPECT_EQ(o.crossed_while_trading, 1u);
    EXPECT_EQ(o.crossed_while_halted, 0u);
    ASSERT_EQ(o.crossed_locates.size(), 1u);
    EXPECT_EQ(o.crossed_locates[0], 1);
    EXPECT_EQ(o.overfills, 0u);
    EXPECT_FALSE(o.clean());
    EXPECT_DOUBLE_EQ(o.touch_agreement(), 0.5);
}

TEST(BookManager, CrossedBookDuringHaltIsNotAnError) {
    BookManager books;
    EXPECT_EQ(books.trading_state(3), '?');
    books.apply(
        itch::TradingAction{hdr(itch::MessageType::TradingAction, 3), Symbol::from("X"), 'H', {}});
    EXPECT_EQ(books.trading_state(3), 'H');
    EXPECT_FALSE(books.is_trading(3));
    books.apply(itch::AddOrder{hdr(itch::MessageType::AddOrder, 3),
                               1,
                               Side::Buy,
                               100,
                               Symbol::from("X"),
                               1000500,
                               false,
                               {}});
    books.apply(itch::AddOrder{hdr(itch::MessageType::AddOrder, 3),
                               2,
                               Side::Sell,
                               100,
                               Symbol::from("X"),
                               1000400,
                               false,
                               {}});
    EXPECT_TRUE(books.book(3).crossed());
    EXPECT_EQ(books.oracle().crossed_while_halted, 1u);
    EXPECT_EQ(books.oracle().crossed_while_trading, 0u);
    EXPECT_TRUE(books.oracle().clean());
}
