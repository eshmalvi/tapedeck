// Message census: how fast can we frame and decode the file, and what is in it?
// No order books are built, so this isolates the decoder's throughput.
//
//   tapedeck_stats data/01302019.prefix.itch

#include "tapedeck/core/clock.hpp"
#include "tapedeck/itch/decoder.hpp"
#include "tapedeck/itch/frame_stream.hpp"
#include "tapedeck/itch/source.hpp"

#include <cstdio>
#include <exception>

#include "cli.hpp"
#include "report.hpp"

namespace {

// Counts every message and remembers the first and last timestamps.
struct CensusHandler : tapedeck::itch::NullHandler {
    tapedeck::replay::MessageCounts counts;
    tapedeck::Timestamp first = 0;
    tapedeck::Timestamp last = 0;

    void seen(const tapedeck::itch::Header& header) {
        counts.count(header.type);
        last = header.timestamp;
        if (first == 0) {
            first = last;
        }
    }

    void on_system_event(const tapedeck::itch::SystemEvent& m) { seen(m.header); }
    void on_stock_directory(const tapedeck::itch::StockDirectory& m) { seen(m.header); }
    void on_trading_action(const tapedeck::itch::TradingAction& m) { seen(m.header); }
    void on_add_order(const tapedeck::itch::AddOrder& m) { seen(m.header); }
    void on_order_executed(const tapedeck::itch::OrderExecuted& m) { seen(m.header); }
    void on_order_executed_with_price(const tapedeck::itch::OrderExecutedWithPrice& m) {
        seen(m.header);
    }
    void on_order_cancel(const tapedeck::itch::OrderCancel& m) { seen(m.header); }
    void on_order_delete(const tapedeck::itch::OrderDelete& m) { seen(m.header); }
    void on_order_replace(const tapedeck::itch::OrderReplace& m) { seen(m.header); }
    void on_trade(const tapedeck::itch::Trade& m) { seen(m.header); }
    void on_cross_trade(const tapedeck::itch::CrossTrade& m) { seen(m.header); }
    void on_other(tapedeck::itch::MessageType type, std::span<const std::byte> payload) {
        counts.count(type);
        if (payload.size() >= 11) {
            last = tapedeck::itch::detail::parse_header(payload).timestamp;
        }
    }
    void on_malformed(std::span<const std::byte>) {
        ++counts.malformed;
        ++counts.total;
    }
};

}  // namespace

int main(int argc, char** argv) {
    const Cli cli(argc, argv);
    if (cli.positional().empty()) {
        std::fprintf(stderr, "usage: tapedeck_stats <itch-file[.gz]>\n");
        return 2;
    }
    try {
        auto source = tapedeck::itch::open_source(cli.positional().front());
        tapedeck::itch::FrameStream frames(*source);
        CensusHandler handler;

        const auto start = tapedeck::now_ns();
        for (auto payload = frames.next(); !payload.empty(); payload = frames.next()) {
            tapedeck::itch::decode(payload, handler);
        }
        const double seconds = static_cast<double>(tapedeck::now_ns() - start) / 1e9;

        const double mb = static_cast<double>(frames.bytes_consumed()) / 1e6;
        std::printf("decoded %s messages (%.1f MB) in %.3f s  ->  %.2f M msg/s, %.1f MB/s\n",
                    tapedeck::format_count(handler.counts.total).c_str(), mb, seconds,
                    static_cast<double>(handler.counts.total) / seconds / 1e6, mb / seconds);
        std::printf("session time covered: %s -> %s\n\n",
                    tapedeck::format_timestamp(handler.first).c_str(),
                    tapedeck::format_timestamp(handler.last).c_str());
        report::print_message_counts(handler.counts);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
