#pragma once

#include "tapedeck/core/types.hpp"

#include <array>
#include <cstdint>

// Nasdaq TotalView-ITCH 5.0 message definitions.
//
// Only the messages that affect the order book or the trade tape are decoded
// into structs. Everything else (halts, imbalance indicators, circuit breaker
// levels, ...) is still framed correctly and reported as `other`.
//
// Spec:
// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification_5.0.pdf
namespace tapedeck::itch {

enum class MessageType : char {
    SystemEvent = 'S',
    StockDirectory = 'R',
    TradingAction = 'H',
    RegShoRestriction = 'Y',
    MarketParticipantPosition = 'L',
    MwcbDeclineLevel = 'V',
    MwcbStatus = 'W',
    IpoQuotingPeriod = 'K',
    LuldAuctionCollar = 'J',
    OperationalHalt = 'h',
    AddOrder = 'A',
    AddOrderWithMpid = 'F',
    OrderExecuted = 'E',
    OrderExecutedWithPrice = 'C',
    OrderCancel = 'X',
    OrderDelete = 'D',
    OrderReplace = 'U',
    Trade = 'P',
    CrossTrade = 'Q',
    BrokenTrade = 'B',
    Noii = 'I',
    Rpii = 'N',
    DirectListingPriceDiscovery = 'O',
};

// Every message starts with the same 11-byte header.
struct Header {
    MessageType type;
    Locate stock_locate;
    std::uint16_t tracking_number;
    Timestamp timestamp;
};

// 'S' - start/end of day markers. event_code: O start of messages, S start of
// system hours, Q start of market hours, M end of market hours, E end of system
// hours, C end of messages.
struct SystemEvent {
    Header header;
    char event_code;
};

// 'R' - maps a stock locate to its symbol. One per symbol at the start of the day.
struct StockDirectory {
    Header header;
    Symbol symbol;
    char market_category;
    Qty round_lot_size;
};

// 'H' - trading state changes. trading_state: H halted, P paused, Q quotation only, T trading.
struct TradingAction {
    Header header;
    Symbol symbol;
    char trading_state;
    std::array<char, 4> reason;
};

// 'A' / 'F' - a new displayed order joins the book.
struct AddOrder {
    Header header;
    OrderId order_id;
    Side side;
    Qty shares;
    Symbol symbol;
    Price price;
    bool has_mpid;             // true for 'F'
    std::array<char, 4> mpid;  // market participant, only for 'F'
};

// 'E' - part or all of a resting order traded at its displayed price.
struct OrderExecuted {
    Header header;
    OrderId order_id;
    Qty shares;
    std::uint64_t match_number;
};

// 'C' - like 'E' but the execution price differs from the displayed price
// (e.g. opening/closing cross). printable == false means it is not reported to the tape.
struct OrderExecutedWithPrice {
    Header header;
    OrderId order_id;
    Qty shares;
    std::uint64_t match_number;
    bool printable;
    Price price;
};

// 'X' - part of an order is cancelled; the order stays on the book.
struct OrderCancel {
    Header header;
    OrderId order_id;
    Qty shares;
};

// 'D' - an order is removed from the book entirely.
struct OrderDelete {
    Header header;
    OrderId order_id;
};

// 'U' - an order is modified: it gets a new id, size and price, and loses queue priority.
struct OrderReplace {
    Header header;
    OrderId old_order_id;
    OrderId new_order_id;
    Qty shares;
    Price price;
};

// 'P' - a trade against a non-displayed order. Does not change the visible book.
struct Trade {
    Header header;
    OrderId order_id;
    Side side;
    Qty shares;
    Symbol symbol;
    Price price;
    std::uint64_t match_number;
};

// 'Q' - opening/closing/IPO/halt cross trade. cross_type: O opening, C closing, H halt/IPO.
struct CrossTrade {
    Header header;
    std::uint64_t shares;
    Symbol symbol;
    Price price;
    std::uint64_t match_number;
    char cross_type;
};

}  // namespace tapedeck::itch
