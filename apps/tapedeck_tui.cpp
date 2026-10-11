// Interactive market replay: watch one symbol's order book, trades and signals
// move through a real trading day, like a video player.
//
//   tapedeck_tui data/01302019.prefix.itch --symbol AAPL [--start 09:29:30] [--speed 1] [--depth
//   10] tapedeck_tui data/01302019.prefix.itch --symbol AAPL --start 07:00:00 --snapshot   # one
//   frame to stdout
//
// keys: space play/pause   n step one message   + / - speed   f skip ahead 1 minute   q quit
//
// Two threads: the engine thread runs the replay pipeline and publishes a
// snapshot a few times per second; the UI thread renders the latest snapshot.
// The hot path never waits on the UI.

#include "tapedeck/book/book_manager.hpp"
#include "tapedeck/core/clock.hpp"
#include "tapedeck/itch/source.hpp"
#include "tapedeck/replay/replay_handler.hpp"
#include "tapedeck/replay/replayer.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cli.hpp"

using namespace tapedeck;

namespace {

// ---------------------------------------------------------------------------
// Shared state between the engine thread and the UI thread.
// ---------------------------------------------------------------------------
struct Controls {
    std::atomic<bool> paused{false};
    std::atomic<bool> quit{false};
    std::atomic<double> speed{1.0};
    std::atomic<int> step_requests{0};
    std::atomic<Timestamp> skip_to{0};  // fast-forward target, 0 = none
};

struct Snapshot {
    std::string symbol;
    bool resolved = false;
    bool finished = false;
    Timestamp time = 0;
    char trading_state = '?';
    std::vector<book::PriceLevel> bids;
    std::vector<book::PriceLevel> asks;
    signals::TopOfBook top;
    double mid_return_stddev = 0.0;
    double spread_median = 0.0;
    double microprice_ewma = 0.0;
    std::size_t stats_window_count = 0;
    std::deque<replay::TradePrint> tape;
    std::uint64_t messages = 0;
    std::uint64_t live_orders = 0;
    double messages_per_second = 0.0;
    std::uint64_t p50 = 0, p99 = 0, p999 = 0;
    double touch_agreement = 1.0;
    std::uint64_t unknown_ids = 0;
};

class SharedSnapshot {
 public:
    void publish(Snapshot snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::move(snapshot);
    }
    Snapshot read() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_;
    }

 private:
    mutable std::mutex mutex_;
    Snapshot latest_;
};

// ---------------------------------------------------------------------------
// Engine thread: runs the pipeline with pacing.
// ---------------------------------------------------------------------------
class Engine {
 public:
    Engine(itch::ByteSource& source, std::string symbol, std::size_t depth, Controls& controls,
           SharedSnapshot& shared)
        : handler_(books_),
          replayer_(source, handler_),
          symbol_(std::move(symbol)),
          depth_(depth),
          controls_(controls),
          shared_(shared) {
        handler_.watch(symbol_);
    }

    void skip_to(Timestamp target) {
        replayer_.run_until(target);
        publish();
    }

    void run() {
        reset_pace();
        auto last_publish = now_ns();
        while (!controls_.quit.load(std::memory_order_relaxed) && !replayer_.finished()) {
            if (const Timestamp target = controls_.skip_to.exchange(0); target != 0) {
                replayer_.run_until(target);
                reset_pace();
            } else if (controls_.paused.load(std::memory_order_relaxed)) {
                if (controls_.step_requests.load() > 0) {
                    controls_.step_requests.fetch_sub(1);
                    replayer_.step();
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                reset_pace();
            } else {
                replayer_.step();
                pace();
            }
            if (now_ns() - last_publish > 40'000'000) {  // ~25 snapshots per second
                publish();
                last_publish = now_ns();
            }
        }
        publish();
    }

 private:
    // Playback at `speed`x: message time must not run ahead of (wall time * speed).
    void pace() {
        const double speed = controls_.speed.load(std::memory_order_relaxed);
        if (speed != pace_speed_) {
            reset_pace();
        }
        const Timestamp now_msg = handler_.last_timestamp();
        if (now_msg < pace_anchor_msg_) {
            return;
        }
        const auto msg_elapsed = static_cast<double>(now_msg - pace_anchor_msg_);
        const auto due_wall = pace_anchor_wall_ + static_cast<std::uint64_t>(msg_elapsed / speed);
        const auto now_wall = now_ns();
        if (due_wall > now_wall + 2'000'000) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(due_wall - now_wall - 1'000'000));
        }
        while (now_ns() < due_wall) {
            // spin out the last millisecond for accuracy
        }
    }

    void reset_pace() {
        pace_anchor_wall_ = now_ns();
        pace_anchor_msg_ = handler_.last_timestamp();
        pace_speed_ = controls_.speed.load(std::memory_order_relaxed);
    }

    void publish() {
        const auto& watch = handler_.watch_state();
        const auto& stats = replayer_.stats();
        Snapshot s;
        s.symbol = symbol_;
        s.resolved = watch.resolved;
        s.finished = replayer_.finished();
        s.time = handler_.last_timestamp();
        s.trading_state = watch.trading_state;
        if (watch.resolved) {
            const auto& book = books_.book(watch.locate);
            s.bids = book.top_levels(Side::Buy, depth_);
            s.asks = book.top_levels(Side::Sell, depth_);
        }
        s.top = watch.top;
        s.mid_return_stddev = watch.mid_returns.stddev();
        s.stats_window_count = watch.mid_returns.count();
        s.spread_median = watch.spread_median.median();
        s.microprice_ewma = watch.microprice_ewma.value();
        s.tape = watch.tape;
        s.messages = stats.messages;
        s.live_orders = books_.live_orders();
        s.messages_per_second = stats.messages_per_second();
        s.p50 = stats.latency.percentile(0.50);
        s.p99 = stats.latency.percentile(0.99);
        s.p999 = stats.latency.percentile(0.999);
        s.touch_agreement = books_.oracle().touch_agreement();
        s.unknown_ids = books_.oracle().unknown_order_ids;
        shared_.publish(std::move(s));
    }

    book::BookManager books_;
    replay::ReplayHandler handler_;
    replay::Replayer replayer_;
    std::string symbol_;
    std::size_t depth_;
    Controls& controls_;
    SharedSnapshot& shared_;
    std::uint64_t pace_anchor_wall_ = 0;
    Timestamp pace_anchor_msg_ = 0;
    double pace_speed_ = 1.0;
};

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
using namespace ftxui;

std::string fmt(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

Element ladder_row(const book::PriceLevel& level, Qty max_qty, Color colour, bool bar_left) {
    const float fill =
        max_qty == 0 ? 0.0f : static_cast<float>(level.level.qty) / static_cast<float>(max_qty);
    auto price = text(format_price(level.price)) | color(colour) | size(WIDTH, EQUAL, 11);
    auto qty = text(format_count(level.level.qty)) | size(WIDTH, EQUAL, 9) | align_right;
    auto orders =
        text("(" + std::to_string(level.level.order_count) + ")") | dim | size(WIDTH, EQUAL, 6);
    auto bar = gauge(fill) | color(colour) | size(WIDTH, EQUAL, 24);
    if (bar_left) {
        return hbox({bar, text(" "), price, qty, text(" "), orders});
    }
    return hbox({price, qty, text(" "), orders, text(" "), bar});
}

Element render_ladder(const Snapshot& s) {
    Qty max_qty = 0;
    for (const auto& level : s.bids)
        max_qty = std::max(max_qty, level.level.qty);
    for (const auto& level : s.asks)
        max_qty = std::max(max_qty, level.level.qty);

    Elements rows;
    // Asks: worst at the top, best just above the spread line.
    for (auto it = s.asks.rbegin(); it != s.asks.rend(); ++it) {
        rows.push_back(ladder_row(*it, max_qty, Color::Red, false));
    }
    std::string spread_text =
        s.top.two_sided() ? "spread " + fmt(s.top.spread(), 4) + "   mid " + fmt(s.top.mid(), 4)
                          : "one-sided book";
    rows.push_back(
        hbox({separator() | flex, text(" " + spread_text + " ") | bold, separator() | flex}));
    for (const auto& level : s.bids) {
        rows.push_back(ladder_row(level, max_qty, Color::Green, true));
    }
    return vbox(std::move(rows));
}

Element render_signals(const Snapshot& s) {
    Elements rows;
    auto row = [&](const std::string& name, const std::string& value) {
        rows.push_back(hbox({text(name) | dim | size(WIDTH, EQUAL, 18), text(value)}));
    };
    if (s.top.two_sided()) {
        row("best bid", format_price(s.top.bid.price) + " x " + std::to_string(s.top.bid.qty));
        row("best ask", format_price(s.top.ask.price) + " x " + std::to_string(s.top.ask.qty));
        row("microprice", fmt(s.top.microprice(), 4));
        row("imbalance", fmt(s.top.imbalance(), 3));
        row("ewma microprice", fmt(s.microprice_ewma, 4));
        row("median spread", fmt(s.spread_median, 4));
        row("mid-return vol", fmt(s.mid_return_stddev * 1e4, 2) +
                                  " bp  (n=" + std::to_string(s.stats_window_count) + ")");
    } else {
        rows.push_back(text("waiting for a two-sided book") | dim);
    }
    std::string state = "trading";
    if (s.trading_state == 'H')
        state = "HALTED";
    if (s.trading_state == 'P')
        state = "PAUSED";
    if (s.trading_state == 'Q')
        state = "quote only";
    row("state", state);
    return vbox(std::move(rows));
}

Element render_tape(const Snapshot& s) {
    Elements rows;
    for (const auto& print : s.tape) {
        const char* kind = print.kind == 'P' ? "hidden" : print.kind == 'Q' ? "cross" : "trade";
        rows.push_back(hbox({text(format_timestamp(print.timestamp).substr(0, 12)) | dim,
                             text("  " + format_price(print.price)) | size(WIDTH, EQUAL, 12),
                             text(format_count(print.qty)) | size(WIDTH, EQUAL, 8) | align_right,
                             text(std::string("  ") + kind) | dim}));
    }
    if (rows.empty()) {
        rows.push_back(text("no trades yet") | dim);
    }
    return vbox(std::move(rows));
}

Element render_stats(const Snapshot& s, const Controls& controls) {
    Elements rows;
    auto row = [&](const std::string& name, const std::string& value) {
        rows.push_back(hbox({text(name) | dim | size(WIDTH, EQUAL, 18), text(value)}));
    };
    row("messages", format_count(s.messages));
    row("live orders", format_count(s.live_orders));
    row("throughput", fmt(s.messages_per_second / 1e6, 2) + " M msg/s");
    row("latency p50/p99", std::to_string(s.p50) + " / " + std::to_string(s.p99) + " ns");
    row("latency p99.9", std::to_string(s.p999) + " ns");
    row("oracle", fmt(100.0 * s.touch_agreement, 3) + "% at touch");
    row("unknown ids", std::to_string(s.unknown_ids));
    row("speed", controls.paused ? "paused" : fmt(controls.speed.load(), 1) + "x");
    return vbox(std::move(rows));
}

Element render(const Snapshot& s, const Controls& controls) {
    const std::string status = s.finished ? "end of file" : controls.paused ? "paused" : "playing";
    auto title = hbox({text(" tapedeck ") | bold | inverted, text("  " + s.symbol + "  ") | bold,
                       text(format_timestamp(s.time)), text("  " + status) | dim, filler()});
    auto left = window(text(" order book "),
                       s.resolved ? render_ladder(s) : text("symbol not in stock directory"));
    // On a short terminal the trade tape gives up rows first; signals and engine stats stay intact.
    auto right = vbox({window(text(" signals "), render_signals(s)),
                       window(text(" trades "), render_tape(s)) | yflex,
                       window(text(" engine "), render_stats(s, controls))});
    auto help = text(" space play/pause   n step   +/- speed   f skip 1 min   q quit ") | dim;
    return vbox({title, hbox({left | flex, right | size(WIDTH, EQUAL, 46)}) | flex, help});
}

}  // namespace

int main(int argc, char** argv) {
    const Cli cli(argc, argv);
    if (cli.positional().empty()) {
        std::fprintf(
            stderr,
            "usage: tapedeck_tui <itch-file[.gz]> [--symbol SYM] [--start HH:MM:SS] [--speed X] "
            "[--depth N] [--snapshot]\n");
        return 2;
    }
    const std::string symbol = cli.get("symbol", "AAPL");
    const auto depth = static_cast<std::size_t>(std::max(1L, cli.get_long("depth", 10)));
    Timestamp start_at = 0;
    if (cli.has("start") && !parse_time_of_day(cli.get("start", ""), start_at)) {
        std::fprintf(stderr, "bad --start time\n");
        return 2;
    }

    try {
        auto source = itch::open_source(cli.positional().front());
        Controls controls;
        controls.speed = cli.get_double("speed", 1.0);
        SharedSnapshot shared;
        Engine engine(*source, symbol, depth, controls, shared);

        if (cli.has("snapshot")) {
            // Render a single frame as plain text: for READMEs, CI and debugging.
            engine.skip_to(start_at > 0 ? start_at : 24 * 3600 * kNanosPerSecond);
            controls.paused = true;
            auto document = render(shared.read(), controls);
            // Tall enough for the ladder (2*depth + spread line + border) or the right-hand column,
            // whichever wins.
            const int ladder_rows = static_cast<int>(2 * depth + 3);
            const int side_rows = 10 + (static_cast<int>(replay::WatchState::kTapeLength) + 2) + 10;
            auto frame = Screen::Create(Dimension::Fixed(110),
                                        Dimension::Fixed(std::max(ladder_rows, side_rows) + 2));
            Render(frame, document);
            std::printf("%s\n", frame.ToString().c_str());
            return 0;
        }

        if (start_at > 0) {
            std::printf("fast-forwarding to %s ...\n", format_time_hms(start_at).c_str());
            engine.skip_to(start_at);
        }
        controls.paused = true;  // start paused so the user sees the first frame

        std::thread engine_thread([&] { engine.run(); });

        auto screen = ScreenInteractive::Fullscreen();
        auto renderer = Renderer([&] { return render(shared.read(), controls); });
        auto component = CatchEvent(renderer, [&](Event event) {
            if (event == Event::Character('q') || event == Event::Escape) {
                controls.quit = true;
                screen.ExitLoopClosure()();
                return true;
            }
            if (event == Event::Character(' ')) {
                controls.paused = !controls.paused;
                return true;
            }
            if (event == Event::Character('n')) {
                controls.paused = true;
                controls.step_requests.fetch_add(1);
                return true;
            }
            if (event == Event::Character('+') || event == Event::Character('=')) {
                controls.speed = std::min(100000.0, controls.speed.load() * 2.0);
                return true;
            }
            if (event == Event::Character('-')) {
                controls.speed = std::max(0.01, controls.speed.load() / 2.0);
                return true;
            }
            if (event == Event::Character('f')) {
                controls.skip_to = shared.read().time + 60 * kNanosPerSecond;
                return true;
            }
            return false;
        });

        // Redraw ~20 times per second so the view follows the engine while playing.
        std::thread refresher([&] {
            while (!controls.quit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                screen.PostEvent(Event::Custom);
            }
        });

        screen.Loop(component);
        controls.quit = true;
        refresher.join();
        engine_thread.join();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
