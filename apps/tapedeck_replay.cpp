// Headless replay: rebuild every book from an ITCH file as fast as possible,
// then print the correctness oracle, throughput, latency and a snapshot of
// one symbol.
//
//   tapedeck_replay data/01302019.prefix.itch --symbol AAPL [--start 09:29:00] [--end 09:31:00]
//                    [--no-latency]

#include "tapedeck/book/book_manager.hpp"
#include "tapedeck/itch/source.hpp"
#include "tapedeck/replay/replay_handler.hpp"
#include "tapedeck/replay/replayer.hpp"

#include <cstdio>
#include <exception>

#include "cli.hpp"
#include "report.hpp"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: tapedeck_replay <itch-file[.gz]> [--symbol SYM] [--start HH:MM:SS] "
                 "[--end HH:MM:SS] [--no-latency]\n");
}

}  // namespace

int main(int argc, char** argv) {
    const Cli cli(argc, argv);
    if (cli.positional().empty()) {
        usage();
        return 2;
    }

    tapedeck::Timestamp start_at = 0;
    tapedeck::Timestamp end_at = 0;
    if (cli.has("start") && !tapedeck::parse_time_of_day(cli.get("start", ""), start_at)) {
        std::fprintf(stderr, "bad --start time\n");
        return 2;
    }
    if (cli.has("end") && !tapedeck::parse_time_of_day(cli.get("end", ""), end_at)) {
        std::fprintf(stderr, "bad --end time\n");
        return 2;
    }
    const std::string symbol = cli.get("symbol", "AAPL");

    try {
        auto source = tapedeck::itch::open_source(cli.positional().front());
        tapedeck::book::BookManager books;
        tapedeck::replay::ReplayHandler handler(books);
        handler.watch(symbol);
        tapedeck::replay::Replayer replayer(*source, handler, !cli.has("no-latency"));

        if (start_at > 0) {
            replayer.run_until(start_at);
        }
        if (end_at > 0) {
            replayer.run_until(end_at);
        } else {
            replayer.run_all();
        }

        const auto& stats = replayer.stats();
        std::printf("== throughput ==\n");
        report::print_throughput(stats);
        std::printf("\n== latency ==\n");
        report::print_latency(stats.latency);
        std::printf("\n== correctness oracle ==\n");
        report::print_oracle(books.oracle(), books);
        std::printf("\n== books ==\n%zu symbols, %s live orders at end of replay\n",
                    books.symbol_count(), tapedeck::format_count(books.live_orders()).c_str());
        std::printf("\n== %s ==\n", symbol.c_str());
        report::print_watch(handler.watch_state(), books, symbol);
        std::printf("\n== message census ==\n");
        report::print_message_counts(handler.counts());
        return books.oracle().clean() ? 0 : 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
