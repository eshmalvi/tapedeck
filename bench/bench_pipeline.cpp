// Micro-benchmarks for the hot path. Run: build/bench/tapedeck_bench
//
// Each benchmark reports ns per operation so the optimisation ladder in
// docs/benchmarks.md can show before/after numbers for every change.

#include "tapedeck/book/book_manager.hpp"
#include "tapedeck/book/order_book.hpp"
#include "tapedeck/itch/decoder.hpp"
#include "tapedeck/itch/encoder.hpp"
#include "tapedeck/itch/frame_stream.hpp"
#include "tapedeck/itch/source.hpp"
#include "tapedeck/replay/replay_handler.hpp"
#include "tapedeck/signals/rolling.hpp"

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

using namespace tapedeck;
using namespace tapedeck::itch;

namespace {

// A realistic synthetic feed: ~40% adds, ~35% deletes, ~10% cancels, ~8% replaces, ~5% executes,
// spread over 50 symbols, matching the mix of a real ITCH day.
std::vector<std::byte> synthetic_feed(std::size_t messages, unsigned seed = 7) {
    std::mt19937 rng(seed);
    std::vector<std::byte> out;
    out.reserve(messages * 40);
    std::uniform_int_distribution<int> pick(0, 99);
    std::uniform_int_distribution<Locate> locate(1, 50);
    std::uniform_int_distribution<Price> tick(0, 50);
    std::uniform_int_distribution<Qty> size(1, 10);
    std::vector<OrderId> live;
    live.reserve(messages);
    OrderId next_id = 1;
    Timestamp ts = 34'200'000'000'000ull;

    for (Locate l = 1; l <= 50; ++l) {
        Encoder::frame(out, Encoder::stock_directory(Header{MessageType::StockDirectory, l, 0, ts},
                                                     Symbol::from("SYM"), 'Q', 100));
    }
    for (std::size_t i = 0; i < messages; ++i) {
        ts += 500;
        const int p = pick(rng);
        if (p < 40 || live.empty()) {
            const Locate l = locate(rng);
            const Side side = p % 2 == 0 ? Side::Buy : Side::Sell;
            const Price price =
                side == Side::Buy ? 1000000 - tick(rng) * 100 : 1000100 + tick(rng) * 100;
            Encoder::frame(out, Encoder::add_order(Header{MessageType::AddOrder, l, 0, ts}, next_id,
                                                   side, size(rng) * 100, Symbol{}, price));
            live.push_back(next_id++);
        } else {
            const auto idx = std::uniform_int_distribution<std::size_t>(0, live.size() - 1)(rng);
            const OrderId id = live[idx];
            const Locate l =
                static_cast<Locate>(1 + id % 50);  // not the real locate; books ignore unknown ids
            if (p < 75) {
                Encoder::frame(
                    out, Encoder::order_delete(Header{MessageType::OrderDelete, l, 0, ts}, id));
                live[idx] = live.back();
                live.pop_back();
            } else if (p < 85) {
                Encoder::frame(out, Encoder::order_cancel(
                                        Header{MessageType::OrderCancel, l, 0, ts}, id, 100));
            } else if (p < 93) {
                Encoder::frame(
                    out, Encoder::order_replace(Header{MessageType::OrderReplace, l, 0, ts}, id,
                                                next_id, 300, 1000000));
                live[idx] = next_id++;
            } else {
                Encoder::frame(out, Encoder::order_executed(
                                        Header{MessageType::OrderExecuted, l, 0, ts}, id, 100, i));
            }
        }
    }
    return out;
}

struct CountHandler : NullHandler {
    std::uint64_t n = 0;
    void on_add_order(const AddOrder& m) { n += m.shares; }
    void on_order_delete(const OrderDelete& m) { n += m.order_id; }
    void on_order_cancel(const OrderCancel& m) { n += m.shares; }
    void on_order_replace(const OrderReplace& m) { n += m.price; }
    void on_order_executed(const OrderExecuted& m) { n += m.shares; }
    void on_stock_directory(const StockDirectory& m) { n += m.round_lot_size; }
};

// Framing + decoding only (no book).
void BM_DecodeOnly(benchmark::State& state) {
    const auto feed = synthetic_feed(200'000);
    std::uint64_t messages = 0;
    for (auto _ : state) {
        MemorySource source(feed, feed.size());
        FrameStream frames(source);
        CountHandler handler;
        for (auto payload = frames.next(); !payload.empty(); payload = frames.next()) {
            decode(payload, handler);
            ++messages;
        }
        benchmark::DoNotOptimize(handler.n);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(messages));
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * feed.size()));
}
BENCHMARK(BM_DecodeOnly)->Unit(benchmark::kMillisecond);

// The full hot path: framing + decoding + book maintenance + oracle.
void BM_DecodeAndBook(benchmark::State& state) {
    const auto feed = synthetic_feed(200'000);
    std::uint64_t messages = 0;
    for (auto _ : state) {
        MemorySource source(feed, feed.size());
        FrameStream frames(source);
        book::BookManager books;
        replay::ReplayHandler handler(books);
        for (auto payload = frames.next(); !payload.empty(); payload = frames.next()) {
            decode(payload, handler);
            ++messages;
        }
        benchmark::DoNotOptimize(books.live_orders());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(messages));
}
BENCHMARK(BM_DecodeAndBook)->Unit(benchmark::kMillisecond);

// Order book in isolation: add + delete pairs on a book with `range` live levels.
void BM_OrderBookAddDelete(benchmark::State& state) {
    book::OrderBook book;
    std::mt19937 rng(1);
    std::uniform_int_distribution<Price> tick(0, static_cast<Price>(state.range(0)));
    for (OrderId id = 1; id <= 1000; ++id) {
        book.add(id, id % 2 ? Side::Buy : Side::Sell, 1000000 + tick(rng) * 100, 100);
    }
    OrderId next = 1001;
    for (auto _ : state) {
        const Price price = 1000000 + tick(rng) * 100;
        book.add(next, Side::Buy, price, 100);
        book.remove(next);
        ++next;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 2);
}
BENCHMARK(BM_OrderBookAddDelete)->Arg(10)->Arg(100)->Arg(1000);

void BM_OrderBookExecuteAtTouch(benchmark::State& state) {
    book::OrderBook book;
    OrderId id = 1;
    for (; id <= 100'000; ++id) {
        book.add(id, Side::Sell, 1000100, 100);
    }
    OrderId victim = 1;
    for (auto _ : state) {
        if (victim >= id) {
            state.PauseTiming();
            for (OrderId k = 1; k < id; ++k)
                book.add(k, Side::Sell, 1000100, 100);
            victim = 1;
            state.ResumeTiming();
        }
        benchmark::DoNotOptimize(book.execute(victim++, 100));
    }
}
BENCHMARK(BM_OrderBookExecuteAtTouch);

void BM_RollingStats(benchmark::State& state) {
    signals::RollingStats stats(100);
    double x = 100.0;
    for (auto _ : state) {
        x += 0.01;
        stats.push(x);
        benchmark::DoNotOptimize(stats.stddev());
    }
}
BENCHMARK(BM_RollingStats);

void BM_RollingMedian(benchmark::State& state) {
    signals::RollingMedian median(static_cast<std::size_t>(state.range(0)));
    std::mt19937 rng(3);
    std::normal_distribution<double> dist(0.0, 1.0);
    for (auto _ : state) {
        median.push(dist(rng));
        benchmark::DoNotOptimize(median.median());
    }
}
BENCHMARK(BM_RollingMedian)->Arg(100)->Arg(1000);

}  // namespace

BENCHMARK_MAIN();
