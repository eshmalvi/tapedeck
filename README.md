# tapedeck

A low-latency NASDAQ market data engine in C++20. It decodes the exchange's raw
TotalView-ITCH 5.0 feed, rebuilds the full-depth limit order book for every
listed stock, computes short-horizon trading signals, and replays a real
trading day in your terminal like a video player.

Every number below was measured on real exchange data (NASDAQ, 30 Jan 2019),
and the reconstruction is validated against the exchange's own execution
reports: **8,513 of 8,513 executions (100%) occurred at the price our book
said was best.**

```
 tapedeck   AAPL  07:28:00.001010386  paused
╭ order book ──────────────────────────────────────────────────╮╭ signals ───────────────────────────────────╮
│163.3000   70        (1)    ▎                                 ││best bid          162.9100 x 168            │
│163.2500   100       (1)    ▍                                 ││best ask          163.0000 x 102            │
│163.2000   610       (1)    ██▉                               ││microprice        162.9660                  │
│163.1600   5,026     (2)    ████████████████████████          ││imbalance         0.244                     │
│163.1200   300       (1)    █▎                                ││ewma microprice   162.8911                  │
│163.1000   200       (1)    ▉                                 ││median spread     0.1000                    │
│163.0700   600       (2)    ██▊                               ││mid-return vol    2.92 bp  (n=100)          │
│163.0500   7         (1)                                      ││state             trading                   │
│163.0100   50        (1)    ▏                                 │╰────────────────────────────────────────────╯
│163.0000   102       (2)    ▍                                 │╭ trades ────────────────────────────────────╮
├──────────────── spread 0.0900   mid 162.9550 ────────────────┤│07:27:46.627  163.0000  48        trade     │
│▊                        162.9100   168       (1)             ││07:27:46.627  163.0000  52        trade     │
│▍                        162.9000   100       (1)             ││07:27:32.715  162.9500  211       trade     │
│                         162.7000   3         (1)             ││07:27:32.715  162.9500  89        trade     │
│▍                        162.6800   100       (1)             ││07:27:28.241  162.9000  200       hidden    │
│                         162.6700   44        (1)             ││07:27:28.241  162.9000  100       trade     │
│██▍                      162.5000   515       (4)             ││07:27:25.835  162.9000  370       hidden    │
│▉                        162.4600   200       (1)             ││07:27:25.835  162.9000  30        trade     │
│▏                        162.4200   55        (2)             ││07:27:25.835  162.9000  70        trade     │
│▉                        162.4100   200       (1)             ││07:27:25.835  162.9000  30        trade     │
│                         162.2700   2         (1)             │╰────────────────────────────────────────────╯
│                                                              │╭ engine ────────────────────────────────────╮
│                                                              ││messages          4,455,701                 │
│                                                              ││live orders       16,761                    │
│                                                              ││throughput        11.56 M msg/s             │
│                                                              ││latency p50/p99   42 / 167 ns               │
│                                                              ││latency p99.9     875 ns                    │
│                                                              ││oracle            100.000% at touch         │
╰──────────────────────────────────────────────────────────────╯╰────────────────────────────────────────────╯
 space play/pause   n step   +/- speed   f skip 1 min   q quit
```

*Apple's pre-market book on 30 Jan 2019 at 07:28, rebuilt from 4.46 million
messages. Real data, real trades.*

## Why this exists

An exchange never broadcasts the state of its order book. It broadcasts
*changes*: "order 9005 added, 600 shares at $107.26", "order 9005 executed 100
shares", "order 9005 deleted". Hundreds of millions of these per day, as
compact big-endian binary. To know the spread at 09:31:04.212 you must have
applied every preceding message, in order, without a single mistake, and in a
trading system you must do it in nanoseconds because the next message is
already arriving.

That is the job of a feed handler, and every systematic trading firm runs one.
This project is a feed handler built from scratch, with two things most
hobby order books skip: a **correctness oracle** that proves the book is right
using facts the exchange itself asserts, and **honest latency measurement**
on every message.

## Results

Measured on an Apple M3 Pro, single thread, Release build (`-O3`), on the
first 130 MB of NASDAQ's 30 Jan 2019 session (03:04 to 07:29 ET, 4,476,485
messages, 8,713 symbols). Figures are typical of three runs.

| stage | throughput | per message |
|---|---|---|
| frame + decode only (`tapedeck_stats`) | 105-108 M msg/s, 3.1 GB/s | ~9.4 ns |
| decode + all books + oracle + signals (`tapedeck_replay --no-latency`) | 11.4-12.9 M msg/s | ~80 ns |
| same, with per-message latency timestamping | 10.1-10.7 M msg/s | p50 42-83 ns, p99 250-292 ns, p99.9 ~1 µs |

Correctness oracle over the same run:

| check | result |
|---|---|
| `E` executions whose order was at our best price | **8,513 / 8,513 (100.0000%)** |
| execute / cancel / delete / replace naming an order we do not hold | 0 |
| duplicate order ids on add | 0 |
| executions larger than the resting size | 0 |
| best bid ≥ best ask while the stock was trading | 0 |
| best bid ≥ best ask while halted / quote-only (legitimate: no matching during halts) | 7, all in one halted symbol |

Both the raw and the gzip-streamed input paths produce the same books. The
test suite and the full real-data replay also run clean under AddressSanitizer
and UndefinedBehaviorSanitizer.

A caveat you should know before quoting the latency figures: macOS
`steady_clock` on Apple Silicon ticks at 24 MHz, so single-message timings are
quantised to 41.67 ns. The percentiles are correct to within one tick; the
throughput figures are not affected. See [docs/benchmarks.md](docs/benchmarks.md).

## What is inside

```
 ITCH file (.itch or .gz)
      │
      │  itch::MmapSource / GzipSource       memory-map, or stream through zlib in 4 MB chunks
      ▼
 itch::FrameStream                            2-byte length prefix → message payload, zero-copy
      │                                       (a frame cut by a chunk boundary is the only copy)
      ▼
 itch::decode<Handler>()                      switch on type byte, validate length, parse fields
      │                                       in place; handler bound at compile time, no virtual calls
      ▼
 replay::ReplayHandler                        count, route, update, measure
      │
      ├── book::BookManager                   stock locate → OrderBook (vector index, no hashing)
      │     └── book::OrderBook               orders by id + aggregated price levels, best at begin()
      │           └── OracleStats             at-touch / unknown-id / overfill / crossed checks
      │
      └── WatchState (one symbol)             TopOfBook → spread, mid, microprice, imbalance
            ├── signals::RollingStats          windowed Welford mean / stddev of mid returns
            ├── signals::RollingMedian         multiset + pinned median iterator, O(log n)
            ├── signals::Ewma                  microprice smoothing
            └── trade tape
      ▼
 replay::Replayer                             step / run_until / run_all, LatencyHistogram per message
      │
      ├── apps/tapedeck_replay                headless: oracle, throughput, latency, census
      ├── apps/tapedeck_stats                 decoder-only census
      └── apps/tapedeck_tui                   FTXUI player: engine thread + UI thread, paced replay
```

Directory layout:

| path | contents |
|---|---|
| `include/tapedeck/core` | `Price`/`Qty`/`Timestamp` types, big-endian byte reader, clock, latency histogram |
| `include/tapedeck/itch` | message structs, decoder, encoder (for tests/bench), byte sources, frame splitter |
| `include/tapedeck/book` | `OrderBook`, `BookManager`, `OracleStats` |
| `include/tapedeck/signals` | rolling statistics, top-of-book signals |
| `include/tapedeck/replay` | message handler, replayer with latency measurement |
| `src/` | the non-template implementations |
| `apps/` | the three executables |
| `tests/` | 56 GoogleTest cases, including an end-to-end synthetic session and a randomised shadow-model test |
| `bench/` | Google Benchmark micro-benchmarks |
| `scripts/fetch_sample.sh` | downloads a byte-range prefix of a real NASDAQ day |
| `docs/` | design notes, benchmark log, optimisation ladder |

## Build and run

Requirements: a C++20 compiler (Apple clang 15+, GCC 12+, Clang 16+), CMake
3.24+, zlib (ships with macOS and every Linux). Test, benchmark and UI
libraries are fetched automatically at configure time.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/tests/tapedeck_tests            # 56 tests, ~10 ms
```

Get real data. NASDAQ publishes full days for free (3-17 GB gzipped). gzip is a
stream format, so a prefix of the archive decodes to a valid prefix of the
session, and 50 MB compressed already covers the whole pre-market:

```bash
scripts/fetch_sample.sh 50             # ~124 MB of 30 Jan 2019, 03:04-07:29 ET
scripts/fetch_sample.sh 150            # reaches the 09:30 open
```

Then:

```bash
# Rebuild every book, print oracle + throughput + latency + a snapshot of one symbol
./build/apps/tapedeck_replay data/01302019.prefix.itch --symbol AAPL

# Decoder-only census
./build/apps/tapedeck_stats data/01302019.prefix.itch

# Watch the market: fast-forward to 07:00, play at 20x
./build/apps/tapedeck_tui data/01302019.prefix.itch --symbol AAPL --start 07:00:00 --speed 20

# The .gz works everywhere a .itch does (streamed through zlib, never fully inflated)
./build/apps/tapedeck_replay data/01302019.prefix.gz --symbol MSFT
```

Player keys: `space` play/pause, `n` step one message, `+`/`-` double/halve
speed, `f` skip ahead one minute, `q` quit. `--snapshot` prints a single frame
to stdout instead of starting the UI.

Optional builds:

```bash
cmake -S . -B build-asan -DTAPEDECK_SANITIZE=address,undefined -DTAPEDECK_BUILD_TUI=OFF
./build/bench/tapedeck_bench --benchmark_min_time=0.5s
```

## Design decisions

Short versions; the reasoning and the alternatives considered are in
[docs/design.md](docs/design.md).

- **Integer prices, never floats.** ITCH prices are fixed-point with four
  implied decimals. Two economically identical prices must land on the same
  level; `0.0001` is not representable in binary floating point. Division by
  10,000 happens once, at display.
- **Read bytes in place with shifts, not `reinterpret_cast`.** ITCH fields are
  big-endian and unaligned. Assembling the value from bytes is well-defined
  and the compiler emits a single load + byte-swap.
- **Compile-time dispatch.** The handler is a template parameter of
  `decode()`, so each callback is a direct (usually inlined) call. The
  `switch` lists the five order-book messages first because they are ~95% of
  the feed.
- **Route books by stock locate.** The exchange assigns each symbol a small
  integer for the day; a `std::vector<OrderBook>` index beats hashing a
  symbol string on every message.
- **Validate the message length before reading fields.** A payload whose
  length does not match its type is reported as malformed, not decoded. There
  is no path that reads past a payload.
- **Prove correctness with the exchange's own statements.** NASDAQ matches with
  strict price priority, so an `E` execution of a displayed order proves that
  no better-priced displayed order existed on that side at that instant. For
  every execution we check that the executed order was at our best price.
- **The v1 order book is the simple one on purpose.** `std::map` for price
  levels, `std::unordered_map` for orders. It is clear, it is correct (the
  oracle says so), and it gives the optimisation ladder below an honest
  baseline to beat.

## Optimisation ladder

The project is structured as a measured sequence of latency improvements. Each
step is one commit with a before/after row in
[docs/benchmarks.md](docs/benchmarks.md). Done so far:

1. ✅ Baseline: zero-copy framing and decoding, compile-time dispatch,
   `std::map` levels + `std::unordered_map` orders. 15.8 M msg/s synthetic,
   ~12 M msg/s real data.

Planned, in order:

2. Higher-resolution clock (`cntvct_el0` / `rdtsc`) so sub-50 ns changes are visible.
3. Replace `std::unordered_map<OrderId, Order>` with a pool-backed open-addressing hash map: no per-order heap allocation.
4. Replace `std::map` price levels with a sorted contiguous array (best price at the end): cache locality over big-O.
5. Hot/cold split of `Order`, 16-byte orders, prefetch the order slot on execute/cancel/delete.
6. Two threads: decode thread → book thread over a hand-written lock-free SPSC ring buffer, cache-line aligned, cores pinned. Compared honestly against single-threaded.
7. Linux: huge pages for the order pool, `perf` flame graphs, `isolcpus`.
8. UDP/MoldUDP64 replayer process with sequence numbers and gap detection, so the handler receives the feed over the network.

## Testing

```bash
./build/tests/tapedeck_tests
```

- **Decoder**: every decoded message type round-trips through an independent
  encoder written from the spec's field tables, so a layout mistake in either
  shows up as a disagreement, not a tautology. Wrong lengths and empty
  payloads are rejected.
- **Framing**: the same stream is fed through every chunk size from 1 byte
  upward and must decode identically. End-of-file marker and truncated tails
  are handled.
- **Order book**: add, partial/full execute, cancel to zero, delete, replace,
  crossed/locked detection, unknown ids, duplicate ids, overfills.
- **Signals**: rolling mean/stddev/median compared against brute-force
  recomputation on every push, including even and odd windows and heavy
  duplicates; the large-offset case that breaks the naive variance formula.
- **End to end**: a hand-built synthetic session with a known final book, and a
  5,000-message randomised session checked against an independent shadow model
  (live order count, best bid price and size, 100% oracle agreement).

## What is not claimed

- This is one venue's displayed book, not the NBBO, and not hidden liquidity.
- The data used so far is a prefix of one day (pre-market through 07:29). The
  pipeline streams and scales to a full day; download more with
  `scripts/fetch_sample.sh 150` or the full 4.7 GB archive.
- No network input yet. The BinaryFILE format is a complete, ordered capture; a
  live MoldUDP64 multicast feed adds sequence gaps and retransmission, which is
  step 8 above.
- Latency is measured in software with `steady_clock`; see the caveat under
  Results.

## License

MIT. See [LICENSE](LICENSE).

NASDAQ TotalView-ITCH data is the property of Nasdaq, Inc. It is downloaded by
the user from Nasdaq's public server and is never committed to this repository.
