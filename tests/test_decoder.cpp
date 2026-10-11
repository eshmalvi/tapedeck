// Every decoded message type round-trips through the independent Encoder,
// which writes fields from the spec's tables rather than the decoder's, so a
// layout mistake in either shows up as a disagreement.

#include "tapedeck/itch/decoder.hpp"
#include "tapedeck/itch/encoder.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>

using namespace tapedeck;
using namespace tapedeck::itch;

namespace {

const Header kHeader{MessageType::AddOrder, 1234, 7, 34'200'000'000'123ull};  // 09:30:00.000000123

Header header_of(MessageType type) {
    Header h = kHeader;
    h.type = type;
    return h;
}

// Captures whichever callback fired, so a test can assert on exactly one.
struct CaptureHandler : NullHandler {
    std::optional<SystemEvent> system_event;
    std::optional<StockDirectory> stock_directory;
    std::optional<TradingAction> trading_action;
    std::optional<AddOrder> add_order;
    std::optional<OrderExecuted> order_executed;
    std::optional<OrderExecutedWithPrice> order_executed_with_price;
    std::optional<OrderCancel> order_cancel;
    std::optional<OrderDelete> order_delete;
    std::optional<OrderReplace> order_replace;
    std::optional<Trade> trade;
    std::optional<CrossTrade> cross_trade;
    std::optional<MessageType> other;
    int malformed = 0;

    void on_system_event(const SystemEvent& m) { system_event = m; }
    void on_stock_directory(const StockDirectory& m) { stock_directory = m; }
    void on_trading_action(const TradingAction& m) { trading_action = m; }
    void on_add_order(const AddOrder& m) { add_order = m; }
    void on_order_executed(const OrderExecuted& m) { order_executed = m; }
    void on_order_executed_with_price(const OrderExecutedWithPrice& m) {
        order_executed_with_price = m;
    }
    void on_order_cancel(const OrderCancel& m) { order_cancel = m; }
    void on_order_delete(const OrderDelete& m) { order_delete = m; }
    void on_order_replace(const OrderReplace& m) { order_replace = m; }
    void on_trade(const Trade& m) { trade = m; }
    void on_cross_trade(const CrossTrade& m) { cross_trade = m; }
    void on_other(MessageType type, std::span<const std::byte>) { other = type; }
    void on_malformed(std::span<const std::byte>) { ++malformed; }
};

void expect_header(const Header& h, MessageType type) {
    EXPECT_EQ(h.type, type);
    EXPECT_EQ(h.stock_locate, kHeader.stock_locate);
    EXPECT_EQ(h.tracking_number, kHeader.tracking_number);
    EXPECT_EQ(h.timestamp, kHeader.timestamp);
}

}  // namespace

TEST(Decoder, SystemEvent) {
    CaptureHandler h;
    decode(Encoder::system_event(header_of(MessageType::SystemEvent), 'Q'), h);
    ASSERT_TRUE(h.system_event);
    expect_header(h.system_event->header, MessageType::SystemEvent);
    EXPECT_EQ(h.system_event->event_code, 'Q');
}

TEST(Decoder, StockDirectory) {
    CaptureHandler h;
    decode(Encoder::stock_directory(header_of(MessageType::StockDirectory), Symbol::from("AAPL"),
                                    'Q', 100),
           h);
    ASSERT_TRUE(h.stock_directory);
    expect_header(h.stock_directory->header, MessageType::StockDirectory);
    EXPECT_EQ(h.stock_directory->symbol.view(), "AAPL");
    EXPECT_EQ(h.stock_directory->market_category, 'Q');
    EXPECT_EQ(h.stock_directory->round_lot_size, 100u);
}

TEST(Decoder, TradingAction) {
    CaptureHandler h;
    decode(
        Encoder::trading_action(header_of(MessageType::TradingAction), Symbol::from("TSLA"), 'H'),
        h);
    ASSERT_TRUE(h.trading_action);
    EXPECT_EQ(h.trading_action->symbol.view(), "TSLA");
    EXPECT_EQ(h.trading_action->trading_state, 'H');
}

TEST(Decoder, AddOrder) {
    CaptureHandler h;
    decode(Encoder::add_order(header_of(MessageType::AddOrder), 0xDEADBEEFCAFEull, Side::Sell, 600,
                              Symbol::from("MSFT"), 1072600),
           h);
    ASSERT_TRUE(h.add_order);
    expect_header(h.add_order->header, MessageType::AddOrder);
    EXPECT_EQ(h.add_order->order_id, 0xDEADBEEFCAFEull);
    EXPECT_EQ(h.add_order->side, Side::Sell);
    EXPECT_EQ(h.add_order->shares, 600u);
    EXPECT_EQ(h.add_order->symbol.view(), "MSFT");
    EXPECT_EQ(h.add_order->price, 1072600u);
    EXPECT_FALSE(h.add_order->has_mpid);
}

TEST(Decoder, AddOrderWithMpid) {
    CaptureHandler h;
    decode(Encoder::add_order_mpid(header_of(MessageType::AddOrderWithMpid), 42, Side::Buy, 100,
                                   Symbol::from("SPY"), 2700000, "NSDQ"),
           h);
    ASSERT_TRUE(h.add_order);
    EXPECT_TRUE(h.add_order->has_mpid);
    EXPECT_EQ(std::string(h.add_order->mpid.data(), 4), "NSDQ");
    EXPECT_EQ(h.add_order->side, Side::Buy);
    EXPECT_EQ(h.add_order->price, 2700000u);
}

TEST(Decoder, OrderExecuted) {
    CaptureHandler h;
    decode(Encoder::order_executed(header_of(MessageType::OrderExecuted), 9005, 100, 777), h);
    ASSERT_TRUE(h.order_executed);
    EXPECT_EQ(h.order_executed->order_id, 9005u);
    EXPECT_EQ(h.order_executed->shares, 100u);
    EXPECT_EQ(h.order_executed->match_number, 777u);
}

TEST(Decoder, OrderExecutedWithPrice) {
    CaptureHandler h;
    decode(Encoder::order_executed_with_price(header_of(MessageType::OrderExecutedWithPrice), 9005,
                                              50, 778, false, 1500000),
           h);
    ASSERT_TRUE(h.order_executed_with_price);
    EXPECT_EQ(h.order_executed_with_price->order_id, 9005u);
    EXPECT_EQ(h.order_executed_with_price->shares, 50u);
    EXPECT_EQ(h.order_executed_with_price->match_number, 778u);
    EXPECT_FALSE(h.order_executed_with_price->printable);
    EXPECT_EQ(h.order_executed_with_price->price, 1500000u);
}

TEST(Decoder, OrderCancelDeleteReplace) {
    CaptureHandler h;
    decode(Encoder::order_cancel(header_of(MessageType::OrderCancel), 11, 25), h);
    ASSERT_TRUE(h.order_cancel);
    EXPECT_EQ(h.order_cancel->order_id, 11u);
    EXPECT_EQ(h.order_cancel->shares, 25u);

    decode(Encoder::order_delete(header_of(MessageType::OrderDelete), 12), h);
    ASSERT_TRUE(h.order_delete);
    EXPECT_EQ(h.order_delete->order_id, 12u);

    decode(Encoder::order_replace(header_of(MessageType::OrderReplace), 13, 14, 300, 999900), h);
    ASSERT_TRUE(h.order_replace);
    EXPECT_EQ(h.order_replace->old_order_id, 13u);
    EXPECT_EQ(h.order_replace->new_order_id, 14u);
    EXPECT_EQ(h.order_replace->shares, 300u);
    EXPECT_EQ(h.order_replace->price, 999900u);
}

TEST(Decoder, TradeAndCrossTrade) {
    CaptureHandler h;
    decode(Encoder::trade(header_of(MessageType::Trade), 0, Side::Buy, 200, Symbol::from("QQQ"),
                          1650000, 5),
           h);
    ASSERT_TRUE(h.trade);
    EXPECT_EQ(h.trade->side, Side::Buy);
    EXPECT_EQ(h.trade->shares, 200u);
    EXPECT_EQ(h.trade->symbol.view(), "QQQ");
    EXPECT_EQ(h.trade->price, 1650000u);
    EXPECT_EQ(h.trade->match_number, 5u);

    decode(Encoder::cross_trade(header_of(MessageType::CrossTrade), 5'000'000'000ull,
                                Symbol::from("QQQ"), 1650100, 6, 'O'),
           h);
    ASSERT_TRUE(h.cross_trade);
    EXPECT_EQ(h.cross_trade->shares, 5'000'000'000ull);
    EXPECT_EQ(h.cross_trade->price, 1650100u);
    EXPECT_EQ(h.cross_trade->cross_type, 'O');
}

TEST(Decoder, UndecodedTypesAreReportedNotDropped) {
    CaptureHandler h;
    decode(Encoder::other(header_of(MessageType::Noii), 50), h);
    ASSERT_TRUE(h.other);
    EXPECT_EQ(*h.other, MessageType::Noii);
    EXPECT_EQ(h.malformed, 0);
}

TEST(Decoder, WrongLengthIsMalformed) {
    CaptureHandler h;
    auto payload = Encoder::order_delete(header_of(MessageType::OrderDelete), 12);
    payload.pop_back();
    decode(payload, h);
    EXPECT_FALSE(h.order_delete);
    EXPECT_EQ(h.malformed, 1);

    decode(std::span<const std::byte>{}, h);
    EXPECT_EQ(h.malformed, 2);
}
