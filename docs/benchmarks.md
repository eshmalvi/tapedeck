# Benchmark log

One row per optimisation step. Nothing is added to this table without a
measured before/after on the same machine and the same input.

## Environment

- Apple M3 Pro (12 cores), 36 GB, macOS 26.7
- Apple clang 21, `-O3 -DNDEBUG`, C++20, single thread
- Real input: `data/01302019.prefix.itch`, 130,351,104 bytes, 4,476,485
  messages, NASDAQ 30 Jan 2019 03:04 to 07:29 ET
- Synthetic input (micro-benchmarks): 200,000 messages, 50 symbols, mix 40%
  add / 35% delete / 10% cancel / 8% replace / 5% execute

## Real-data pipeline

Command: `tapedeck_replay data/01302019.prefix.itch --symbol AAPL` (typical of
3 runs; `--no-latency` removes the two clock reads per message).

| step | decode only | decode + books, no timing | decode + books, timed | p50 | p99 | p99.9 |
|---|---|---|---|---|---|---|
| 1. baseline (`std::map` + `std::unordered_map`) | 105-108 M msg/s | 11.4-12.9 M msg/s | 10.1-10.7 M msg/s | 42-83 ns | 250-292 ns | ~1 µs |

Oracle at every step: 8,513 / 8,513 executions at touch, 0 unknown ids, 0
overfills, 0 crossed while trading.

## Micro-benchmarks

Command: `tapedeck_bench --benchmark_min_time=0.5s`

| step | decode only | decode + book | book add+delete (10 / 100 / 1000 levels) | execute at touch | RollingStats push | RollingMedian push (100 / 1000) |
|---|---|---|---|---|---|---|
| 1. baseline | 7.3 ns/msg, 137.6 M/s | 63 ns/msg, 15.8 M/s | 54 / 64 / 91 ns | 17.3 ns | 2.2 ns | 151 / 176 ns |

Raw output: `docs/bench_baseline_raw.txt`.

## Reading the numbers

- **Decode-only vs decode+book.** Framing and decoding are ~7-9 ns per message;
  the book is ~55-70 ns. The order book is where the time goes, which is why
  the ladder starts there.
- **Clock quantisation.** `std::chrono::steady_clock` on Apple Silicon ticks at
  24 MHz, so single samples are multiples of 41.67 ns. That is why p50 reads
  "42 or 83 ns" rather than a smooth value. Throughput is unaffected. Step 2
  of the ladder switches to `cntvct_el0` (ARM) / `rdtsc` (x86) for
  sub-tick resolution.
- **Measurement overhead.** Two `steady_clock::now()` calls cost ~15-20 ns on
  this machine, visible as the gap between the timed and untimed columns.
- **Synthetic vs real.** The synthetic feed is faster per message (63 vs ~80
  ns) because it has 50 symbols, so the books stay hot in cache; the real feed
  touches 8,713 symbols.

## Planned steps

Each step lands as one commit and one row above.

2. Cycle-counter clock.
3. Pool-backed open-addressing hash map for `OrderId → Order` (no per-order allocation).
4. Sorted-vector price levels, best at the back.
5. Hot/cold `Order` split, 16-byte order record, prefetch on id lookup.
6. Decode thread → book thread over a hand-written SPSC ring buffer; cores pinned; compared against single-threaded.
7. Linux: huge pages, `perf` flame graphs, `isolcpus`.
8. MoldUDP64 replayer process; handler receives over UDP with sequence/gap handling.
