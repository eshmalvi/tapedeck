#pragma once

#include "tapedeck/itch/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Builds spec-conformant ITCH 5.0 byte streams.
//
// Used by the tests (encode -> decode round trips) and the benchmarks
// (synthetic traffic). It writes fields at the spec's offsets directly rather
// than reusing the decoder's tables, so a layout mistake in one shows up as a
// disagreement with the other.
namespace tapedeck::itch {

class Encoder {
 public:
    // Appends a framed message (2-byte length + payload) to `out`.
    static void frame(std::vector<std::byte>& out, const std::vector<std::byte>& payload) {
        put_u16(out, static_cast<std::uint16_t>(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
    }

    static std::vector<std::byte> system_event(const Header& h, char event_code) {
        auto p = header(h, 12);
        put_char(p, 11, event_code);
        return p;
    }

    static std::vector<std::byte> stock_directory(const Header& h, Symbol symbol,
                                                  char market_category, Qty round_lot_size) {
        auto p = header(h, 39);
        put_symbol(p, 11, symbol);
        put_char(p, 19, market_category);
        put_char(p, 20, 'N');  // financial status indicator
        put_u32(p, 21, round_lot_size);
        put_char(p, 25, 'N');  // round lots only
        put_char(p, 26, 'C');  // issue classification
        put_char(p, 27, 'Z');
        put_char(p, 28, ' ');
        put_char(p, 29, 'P');  // authenticity
        put_char(p, 30, 'N');
        put_char(p, 31, 'N');
        put_char(p, 32, '1');
        put_char(p, 33, 'N');
        put_u32(p, 34, 0);
        put_char(p, 38, 'N');
        return p;
    }

    static std::vector<std::byte> trading_action(const Header& h, Symbol symbol, char state) {
        auto p = header(h, 25);
        put_symbol(p, 11, symbol);
        put_char(p, 19, state);
        put_char(p, 20, ' ');
        put_char(p, 21, ' ');
        put_char(p, 22, ' ');
        put_char(p, 23, ' ');
        put_char(p, 24, ' ');
        return p;
    }

    static std::vector<std::byte> add_order(const Header& h, OrderId id, Side side, Qty shares,
                                            Symbol symbol, Price price) {
        auto p = header(h, 36);
        put_u64(p, 11, id);
        put_char(p, 19, static_cast<char>(side));
        put_u32(p, 20, shares);
        put_symbol(p, 24, symbol);
        put_u32(p, 32, price);
        return p;
    }

    static std::vector<std::byte> add_order_mpid(const Header& h, OrderId id, Side side, Qty shares,
                                                 Symbol symbol, Price price, const char mpid[4]) {
        auto p = add_order(h, id, side, shares, symbol, price);
        p.resize(40);
        for (int i = 0; i < 4; ++i) {
            put_char(p, 36 + static_cast<std::size_t>(i), mpid[i]);
        }
        return p;
    }

    static std::vector<std::byte> order_executed(const Header& h, OrderId id, Qty shares,
                                                 std::uint64_t match) {
        auto p = header(h, 31);
        put_u64(p, 11, id);
        put_u32(p, 19, shares);
        put_u64(p, 23, match);
        return p;
    }

    static std::vector<std::byte> order_executed_with_price(const Header& h, OrderId id, Qty shares,
                                                            std::uint64_t match, bool printable,
                                                            Price price) {
        auto p = header(h, 36);
        put_u64(p, 11, id);
        put_u32(p, 19, shares);
        put_u64(p, 23, match);
        put_char(p, 31, printable ? 'Y' : 'N');
        put_u32(p, 32, price);
        return p;
    }

    static std::vector<std::byte> order_cancel(const Header& h, OrderId id, Qty shares) {
        auto p = header(h, 23);
        put_u64(p, 11, id);
        put_u32(p, 19, shares);
        return p;
    }

    static std::vector<std::byte> order_delete(const Header& h, OrderId id) {
        auto p = header(h, 19);
        put_u64(p, 11, id);
        return p;
    }

    static std::vector<std::byte> order_replace(const Header& h, OrderId old_id, OrderId new_id,
                                                Qty shares, Price price) {
        auto p = header(h, 35);
        put_u64(p, 11, old_id);
        put_u64(p, 19, new_id);
        put_u32(p, 27, shares);
        put_u32(p, 31, price);
        return p;
    }

    static std::vector<std::byte> trade(const Header& h, OrderId id, Side side, Qty shares,
                                        Symbol symbol, Price price, std::uint64_t match) {
        auto p = header(h, 44);
        put_u64(p, 11, id);
        put_char(p, 19, static_cast<char>(side));
        put_u32(p, 20, shares);
        put_symbol(p, 24, symbol);
        put_u32(p, 32, price);
        put_u64(p, 36, match);
        return p;
    }

    static std::vector<std::byte> cross_trade(const Header& h, std::uint64_t shares, Symbol symbol,
                                              Price price, std::uint64_t match, char cross_type) {
        auto p = header(h, 40);
        put_u64(p, 11, shares);
        put_symbol(p, 19, symbol);
        put_u32(p, 27, price);
        put_u64(p, 31, match);
        put_char(p, 39, cross_type);
        return p;
    }

    // A message of a type we don't decode ('I' NOII is 50 bytes): header + zero padding.
    static std::vector<std::byte> other(const Header& h, std::size_t size) {
        return header(h, size);
    }

 private:
    static std::vector<std::byte> header(const Header& h, std::size_t size) {
        std::vector<std::byte> p(size, std::byte{0});
        put_char(p, 0, static_cast<char>(h.type));
        put_u16(p, 1, h.stock_locate);
        put_u16(p, 3, h.tracking_number);
        put_u48(p, 5, h.timestamp);
        return p;
    }

    static void put_char(std::vector<std::byte>& p, std::size_t at, char c) {
        p[at] = static_cast<std::byte>(c);
    }

    static void put_symbol(std::vector<std::byte>& p, std::size_t at, Symbol symbol) {
        for (std::size_t i = 0; i < symbol.chars.size(); ++i) {
            put_char(p, at + i, symbol.chars[i]);
        }
    }

    static void put_be(std::vector<std::byte>& p, std::size_t at, std::uint64_t value, int width) {
        for (int i = width - 1; i >= 0; --i) {
            p[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(value & 0xFF);
            value >>= 8;
        }
    }

    static void put_u16(std::vector<std::byte>& p, std::size_t at, std::uint16_t v) {
        put_be(p, at, v, 2);
    }
    static void put_u32(std::vector<std::byte>& p, std::size_t at, std::uint32_t v) {
        put_be(p, at, v, 4);
    }
    static void put_u48(std::vector<std::byte>& p, std::size_t at, std::uint64_t v) {
        put_be(p, at, v, 6);
    }
    static void put_u64(std::vector<std::byte>& p, std::size_t at, std::uint64_t v) {
        put_be(p, at, v, 8);
    }

    static void put_u16(std::vector<std::byte>& out, std::uint16_t v) {
        out.push_back(static_cast<std::byte>(v >> 8));
        out.push_back(static_cast<std::byte>(v & 0xFF));
    }
};

}  // namespace tapedeck::itch
