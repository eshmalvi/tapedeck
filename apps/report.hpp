#pragma once

#include "tapedeck/book/book_manager.hpp"
#include "tapedeck/core/types.hpp"
#include "tapedeck/replay/replay_handler.hpp"
#include "tapedeck/replay/replayer.hpp"

#include <cstdio>
#include <string>

// Plain-text reports shared by the command-line tools.
namespace report {

inline void print_message_counts(const tapedeck::replay::MessageCounts& counts) {
    using tapedeck::itch::MessageType;
    struct Row {
        MessageType type;
        const char* name;
    };
    static constexpr Row rows[] = {
        {MessageType::AddOrder, "A  add order"},
        {MessageType::AddOrderWithMpid, "F  add order (attributed)"},
        {MessageType::OrderDelete, "D  order delete"},
        {MessageType::OrderCancel, "X  order cancel"},
        {MessageType::OrderReplace, "U  order replace"},
        {MessageType::OrderExecuted, "E  order executed"},
        {MessageType::OrderExecutedWithPrice, "C  order executed w/ price"},
        {MessageType::Trade, "P  trade (hidden order)"},
        {MessageType::CrossTrade, "Q  cross trade"},
        {MessageType::StockDirectory, "R  stock directory"},
        {MessageType::TradingAction, "H  trading action"},
        {MessageType::SystemEvent, "S  system event"},
        {MessageType::Noii, "I  net order imbalance"},
    };
    std::printf("%-30s %15s %8s\n", "message type", "count", "share");
    for (const Row& row : rows) {
        const auto n = counts.of(row.type);
        const double share =
            counts.total == 0 ? 0.0
                              : 100.0 * static_cast<double>(n) / static_cast<double>(counts.total);
        std::printf("%-30s %15s %7.2f%%\n", row.name, tapedeck::format_count(n).c_str(), share);
    }
    std::printf("%-30s %15s\n", "total", tapedeck::format_count(counts.total).c_str());
    if (counts.malformed > 0) {
        std::printf("%-30s %15s\n", "MALFORMED", tapedeck::format_count(counts.malformed).c_str());
    }
}

inline void print_oracle(const tapedeck::book::OracleStats& oracle,
                         const tapedeck::book::BookManager& books) {
    std::printf("executions ('E') checked           %15s\n",
                tapedeck::format_count(oracle.executions).c_str());
    std::printf("  ... at our best price            %15s  (%.4f%%)\n",
                tapedeck::format_count(oracle.executions_at_touch).c_str(),
                100.0 * oracle.touch_agreement());
    std::printf("executions with price ('C')        %15s\n",
                tapedeck::format_count(oracle.executions_with_price).c_str());
    std::printf("unknown order ids                  %15s\n",
                tapedeck::format_count(oracle.unknown_order_ids).c_str());
    std::printf("duplicate order ids                %15s\n",
                tapedeck::format_count(oracle.duplicate_order_ids).c_str());
    std::printf("overfills                          %15s\n",
                tapedeck::format_count(oracle.overfills).c_str());
    std::printf("crossed book while halted/quoting  %15s  (expected: no matching during halts)\n",
                tapedeck::format_count(oracle.crossed_while_halted).c_str());
    std::printf("crossed book while trading         %15s",
                tapedeck::format_count(oracle.crossed_while_trading).c_str());
    if (!oracle.crossed_locates.empty()) {
        std::printf("  (");
        for (std::size_t i = 0; i < oracle.crossed_locates.size(); ++i) {
            std::printf("%s%.*s", i == 0 ? "" : " ",
                        static_cast<int>(books.symbol_of(oracle.crossed_locates[i]).view().size()),
                        books.symbol_of(oracle.crossed_locates[i]).view().data());
        }
        std::printf(")");
    }
    std::printf("\nverdict                            %15s\n",
                oracle.clean() ? "CLEAN" : "MISMATCHES");
}

inline void print_latency(const tapedeck::LatencyHistogram& hist) {
    if (hist.count() == 0) {
        std::printf("latency: not measured\n");
        return;
    }
    std::printf("per-message latency (decode + book + signals), %s samples\n",
                tapedeck::format_count(hist.count()).c_str());
    std::printf(
        "  p50 %6llu ns   p90 %6llu ns   p99 %6llu ns   p99.9 %6llu ns   max %llu ns   mean %.1f "
        "ns\n",
        static_cast<unsigned long long>(hist.percentile(0.50)),
        static_cast<unsigned long long>(hist.percentile(0.90)),
        static_cast<unsigned long long>(hist.percentile(0.99)),
        static_cast<unsigned long long>(hist.percentile(0.999)),
        static_cast<unsigned long long>(hist.max()), hist.mean());
}

inline void print_throughput(const tapedeck::replay::ReplayStats& stats) {
    const double seconds = static_cast<double>(stats.wall_ns) / 1e9;
    const double mb = static_cast<double>(stats.bytes) / 1e6;
    std::printf("processed %s messages (%.1f MB) in %.3f s  ->  %.2f M msg/s, %.1f MB/s\n",
                tapedeck::format_count(stats.messages).c_str(), mb, seconds,
                stats.messages_per_second() / 1e6, seconds == 0.0 ? 0.0 : mb / seconds);
    std::printf("session time covered: %s -> %s\n",
                tapedeck::format_timestamp(stats.first_timestamp).c_str(),
                tapedeck::format_timestamp(stats.last_timestamp).c_str());
}

inline void print_watch(const tapedeck::replay::WatchState& watch,
                        const tapedeck::book::BookManager& books, const std::string& symbol) {
    if (!watch.resolved) {
        std::printf("symbol %s: not found in the stock directory\n", symbol.c_str());
        return;
    }
    const auto& book = books.book(watch.locate);
    std::printf(
        "%s: %zu live orders, %zu bid levels, %zu ask levels, %s book updates, trading state "
        "'%c'%s\n",
        symbol.c_str(), book.order_count(), book.bid_level_count(), book.ask_level_count(),
        tapedeck::format_count(watch.book_updates).c_str(), watch.trading_state,
        book.crossed() ? ", CROSSED" : "");
    if (watch.top.two_sided()) {
        std::printf(
            "  bid %s x %u   ask %s x %u   spread %.4f   mid %.4f   microprice %.4f   imbalance "
            "%+.3f\n",
            tapedeck::format_price(watch.top.bid.price).c_str(), watch.top.bid.qty,
            tapedeck::format_price(watch.top.ask.price).c_str(), watch.top.ask.qty,
            watch.top.spread(), watch.top.mid(), watch.top.microprice(), watch.top.imbalance());
    }
    if (watch.mid_returns.count() > 1) {
        std::printf(
            "  rolling(%zu): mid-return stddev %.3e   median spread %.4f   microprice ewma %.4f\n",
            watch.mid_returns.count(), watch.mid_returns.stddev(), watch.spread_median.median(),
            watch.microprice_ewma.value());
    }
    if (!watch.tape.empty()) {
        std::printf("  last trades:\n");
        for (const auto& print : watch.tape) {
            std::printf("    %s  %c  %10s  x %u\n",
                        tapedeck::format_timestamp(print.timestamp).c_str(), print.kind,
                        tapedeck::format_price(print.price).c_str(), print.qty);
        }
    }
}

}  // namespace report
