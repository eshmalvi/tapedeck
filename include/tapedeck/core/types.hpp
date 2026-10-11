#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace tapedeck {

// ITCH sends prices as 32-bit unsigned integers with four implied decimals
// ($187.25 arrives as 1872500). We keep them as integers everywhere and only
// convert to a decimal string for display, so two equal prices can never
// compare unequal the way floating point would allow.
using Price = std::uint32_t;
using Qty = std::uint32_t;
using OrderId = std::uint64_t;
using Timestamp = std::uint64_t;  // nanoseconds since midnight, US/Eastern
using Locate = std::uint16_t;     // per-day integer id for a symbol ("stock locate")

inline constexpr Price kPriceScale = 10'000;
inline constexpr Timestamp kNanosPerSecond = 1'000'000'000;

enum class Side : char { Buy = 'B', Sell = 'S' };

inline Side opposite(Side side) {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

// Exchange symbols are 8 characters, right-padded with spaces ("AAPL    ").
struct Symbol {
    std::array<char, 8> chars{};

    static Symbol from(std::string_view text);

    // The symbol without trailing padding ("AAPL").
    std::string_view view() const;

    bool operator==(const Symbol&) const = default;
};

inline double price_to_double(Price price) {
    return static_cast<double>(price) / static_cast<double>(kPriceScale);
}

std::string format_price(Price price);          // 1872500 -> "187.2500"
std::string format_timestamp(Timestamp nanos);  // -> "09:30:00.123456789"
std::string format_time_hms(Timestamp nanos);   // -> "09:30:00"
std::string format_count(std::uint64_t count);  // 12554295 -> "12,554,295"

// Parses "HH:MM:SS" or "HH:MM" into nanoseconds since midnight. Returns false on bad input.
bool parse_time_of_day(std::string_view text, Timestamp& out);

}  // namespace tapedeck
