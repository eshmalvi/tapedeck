#include "tapedeck/core/types.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace tapedeck {

Symbol Symbol::from(std::string_view text) {
    Symbol symbol;
    symbol.chars.fill(' ');
    const auto n = std::min(text.size(), symbol.chars.size());
    std::copy_n(text.begin(), n, symbol.chars.begin());
    return symbol;
}

std::string_view Symbol::view() const {
    // The exchange pads with spaces; a default-constructed Symbol is NUL-filled. Trim both.
    static constexpr std::string_view kPadding("\0 ", 2);
    std::string_view all(chars.data(), chars.size());
    const auto end = all.find_last_not_of(kPadding);
    return end == std::string_view::npos ? std::string_view{} : all.substr(0, end + 1);
}

std::string format_price(Price price) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%u.%04u", price / kPriceScale, price % kPriceScale);
    return buffer;
}

std::string format_timestamp(Timestamp nanos) {
    const auto seconds = nanos / kNanosPerSecond;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02llu:%02llu:%02llu.%09llu",
                  static_cast<unsigned long long>(seconds / 3600),
                  static_cast<unsigned long long>(seconds / 60 % 60),
                  static_cast<unsigned long long>(seconds % 60),
                  static_cast<unsigned long long>(nanos % kNanosPerSecond));
    return buffer;
}

std::string format_time_hms(Timestamp nanos) {
    return format_timestamp(nanos).substr(0, 8);
}

std::string format_count(std::uint64_t count) {
    std::string digits = std::to_string(count);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(digits[i]);
    }
    return out;
}

bool parse_time_of_day(std::string_view text, Timestamp& out) {
    unsigned parts[3] = {0, 0, 0};
    int count = 0;
    std::size_t start = 0;
    while (count < 3 && start <= text.size()) {
        const auto colon = text.find(':', start);
        const auto piece = text.substr(
            start, colon == std::string_view::npos ? std::string_view::npos : colon - start);
        const auto result =
            std::from_chars(piece.data(), piece.data() + piece.size(), parts[count]);
        if (result.ec != std::errc{} || result.ptr != piece.data() + piece.size()) {
            return false;
        }
        ++count;
        if (colon == std::string_view::npos) {
            break;
        }
        start = colon + 1;
    }
    if (count < 2 || parts[0] > 23 || parts[1] > 59 || parts[2] > 59) {
        return false;
    }
    out = (parts[0] * 3600ULL + parts[1] * 60ULL + parts[2]) * kNanosPerSecond;
    return true;
}

}  // namespace tapedeck
