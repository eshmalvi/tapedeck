#pragma once

#include "tapedeck/core/byte_reader.hpp"
#include "tapedeck/itch/messages.hpp"

#include <cstddef>
#include <span>

namespace tapedeck::itch {

// Exact payload sizes from the spec, including the leading type byte.
// A payload of the wrong size is reported as malformed rather than decoded.
inline constexpr std::size_t kSystemEventSize = 12;
inline constexpr std::size_t kStockDirectorySize = 39;
inline constexpr std::size_t kTradingActionSize = 25;
inline constexpr std::size_t kAddOrderSize = 36;
inline constexpr std::size_t kAddOrderWithMpidSize = 40;
inline constexpr std::size_t kOrderExecutedSize = 31;
inline constexpr std::size_t kOrderExecutedWithPriceSize = 36;
inline constexpr std::size_t kOrderCancelSize = 23;
inline constexpr std::size_t kOrderDeleteSize = 19;
inline constexpr std::size_t kOrderReplaceSize = 35;
inline constexpr std::size_t kTradeSize = 44;
inline constexpr std::size_t kCrossTradeSize = 40;

// Base class for message handlers. Derive from it and define only the
// callbacks you care about; the rest fall through to these no-ops.
//
// Dispatch is resolved at compile time (the handler is a template parameter
// of `decode`), so there is no virtual call per message.
struct NullHandler {
    void on_system_event(const SystemEvent&) {}
    void on_stock_directory(const StockDirectory&) {}
    void on_trading_action(const TradingAction&) {}
    void on_add_order(const AddOrder&) {}
    void on_order_executed(const OrderExecuted&) {}
    void on_order_executed_with_price(const OrderExecutedWithPrice&) {}
    void on_order_cancel(const OrderCancel&) {}
    void on_order_delete(const OrderDelete&) {}
    void on_order_replace(const OrderReplace&) {}
    void on_trade(const Trade&) {}
    void on_cross_trade(const CrossTrade&) {}
    // A valid message type we chose not to decode.
    void on_other(MessageType, std::span<const std::byte>) {}
    // A payload whose length does not match its type.
    void on_malformed(std::span<const std::byte>) {}
};

// ---------------------------------------------------------------------------
// Field parsers. Offsets are straight from the spec tables.
// ---------------------------------------------------------------------------
namespace detail {

inline Header parse_header(std::span<const std::byte> p) {
    return Header{
        .type = static_cast<MessageType>(bytes::ch(p, 0)),
        .stock_locate = bytes::u16(p, 1),
        .tracking_number = bytes::u16(p, 3),
        .timestamp = bytes::u48(p, 5),
    };
}

inline Symbol parse_symbol(std::span<const std::byte> p, std::size_t offset) {
    return Symbol{bytes::chars<8>(p, offset)};
}

inline SystemEvent parse_system_event(std::span<const std::byte> p) {
    return SystemEvent{.header = parse_header(p), .event_code = bytes::ch(p, 11)};
}

inline StockDirectory parse_stock_directory(std::span<const std::byte> p) {
    return StockDirectory{
        .header = parse_header(p),
        .symbol = parse_symbol(p, 11),
        .market_category = bytes::ch(p, 19),
        .round_lot_size = bytes::u32(p, 21),
    };
}

inline TradingAction parse_trading_action(std::span<const std::byte> p) {
    return TradingAction{
        .header = parse_header(p),
        .symbol = parse_symbol(p, 11),
        .trading_state = bytes::ch(p, 19),
        .reason = bytes::chars<4>(p, 21),
    };
}

inline AddOrder parse_add_order(std::span<const std::byte> p, bool has_mpid) {
    return AddOrder{
        .header = parse_header(p),
        .order_id = bytes::u64(p, 11),
        .side = static_cast<Side>(bytes::ch(p, 19)),
        .shares = bytes::u32(p, 20),
        .symbol = parse_symbol(p, 24),
        .price = bytes::u32(p, 32),
        .has_mpid = has_mpid,
        .mpid = has_mpid ? bytes::chars<4>(p, 36) : std::array<char, 4>{' ', ' ', ' ', ' '},
    };
}

inline OrderExecuted parse_order_executed(std::span<const std::byte> p) {
    return OrderExecuted{
        .header = parse_header(p),
        .order_id = bytes::u64(p, 11),
        .shares = bytes::u32(p, 19),
        .match_number = bytes::u64(p, 23),
    };
}

inline OrderExecutedWithPrice parse_order_executed_with_price(std::span<const std::byte> p) {
    return OrderExecutedWithPrice{
        .header = parse_header(p),
        .order_id = bytes::u64(p, 11),
        .shares = bytes::u32(p, 19),
        .match_number = bytes::u64(p, 23),
        .printable = bytes::ch(p, 31) == 'Y',
        .price = bytes::u32(p, 32),
    };
}

inline OrderCancel parse_order_cancel(std::span<const std::byte> p) {
    return OrderCancel{
        .header = parse_header(p),
        .order_id = bytes::u64(p, 11),
        .shares = bytes::u32(p, 19),
    };
}

inline OrderDelete parse_order_delete(std::span<const std::byte> p) {
    return OrderDelete{.header = parse_header(p), .order_id = bytes::u64(p, 11)};
}

inline OrderReplace parse_order_replace(std::span<const std::byte> p) {
    return OrderReplace{
        .header = parse_header(p),
        .old_order_id = bytes::u64(p, 11),
        .new_order_id = bytes::u64(p, 19),
        .shares = bytes::u32(p, 27),
        .price = bytes::u32(p, 31),
    };
}

inline Trade parse_trade(std::span<const std::byte> p) {
    return Trade{
        .header = parse_header(p),
        .order_id = bytes::u64(p, 11),
        .side = static_cast<Side>(bytes::ch(p, 19)),
        .shares = bytes::u32(p, 20),
        .symbol = parse_symbol(p, 24),
        .price = bytes::u32(p, 32),
        .match_number = bytes::u64(p, 36),
    };
}

inline CrossTrade parse_cross_trade(std::span<const std::byte> p) {
    return CrossTrade{
        .header = parse_header(p),
        .shares = bytes::u64(p, 11),
        .symbol = parse_symbol(p, 19),
        .price = bytes::u32(p, 27),
        .match_number = bytes::u64(p, 31),
        .cross_type = bytes::ch(p, 39),
    };
}

}  // namespace detail

// Decodes one framed message payload and calls the matching handler callback.
//
// The payload is read in place: nothing is copied or allocated, and the
// message structs are built directly from the bytes.
template <typename Handler>
void decode(std::span<const std::byte> payload, Handler& handler) {
    if (payload.empty()) {
        handler.on_malformed(payload);
        return;
    }

    const auto type = static_cast<MessageType>(bytes::ch(payload, 0));
    const auto size = payload.size();

    // Order-book messages are listed first: they are ~95% of a real feed.
    switch (type) {
        case MessageType::AddOrder:
            if (size != kAddOrderSize)
                break;
            handler.on_add_order(detail::parse_add_order(payload, false));
            return;
        case MessageType::OrderDelete:
            if (size != kOrderDeleteSize)
                break;
            handler.on_order_delete(detail::parse_order_delete(payload));
            return;
        case MessageType::OrderCancel:
            if (size != kOrderCancelSize)
                break;
            handler.on_order_cancel(detail::parse_order_cancel(payload));
            return;
        case MessageType::OrderReplace:
            if (size != kOrderReplaceSize)
                break;
            handler.on_order_replace(detail::parse_order_replace(payload));
            return;
        case MessageType::OrderExecuted:
            if (size != kOrderExecutedSize)
                break;
            handler.on_order_executed(detail::parse_order_executed(payload));
            return;
        case MessageType::AddOrderWithMpid:
            if (size != kAddOrderWithMpidSize)
                break;
            handler.on_add_order(detail::parse_add_order(payload, true));
            return;
        case MessageType::OrderExecutedWithPrice:
            if (size != kOrderExecutedWithPriceSize)
                break;
            handler.on_order_executed_with_price(detail::parse_order_executed_with_price(payload));
            return;
        case MessageType::Trade:
            if (size != kTradeSize)
                break;
            handler.on_trade(detail::parse_trade(payload));
            return;
        case MessageType::CrossTrade:
            if (size != kCrossTradeSize)
                break;
            handler.on_cross_trade(detail::parse_cross_trade(payload));
            return;
        case MessageType::StockDirectory:
            if (size != kStockDirectorySize)
                break;
            handler.on_stock_directory(detail::parse_stock_directory(payload));
            return;
        case MessageType::TradingAction:
            if (size != kTradingActionSize)
                break;
            handler.on_trading_action(detail::parse_trading_action(payload));
            return;
        case MessageType::SystemEvent:
            if (size != kSystemEventSize)
                break;
            handler.on_system_event(detail::parse_system_event(payload));
            return;
        default:
            // A type we do not decode (or one added in a later ITCH revision).
            // Framing already succeeded, so just report it and move on.
            handler.on_other(type, payload);
            return;
    }
    // A known type whose payload length does not match the spec.
    handler.on_malformed(payload);
}

}  // namespace tapedeck::itch
