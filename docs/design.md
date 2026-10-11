# Design notes

Every non-obvious decision in the codebase, with the alternative that was
rejected and why. Written so that each one can be defended in an interview.

## 1. The data model

### Prices are integers

ITCH encodes a price as a 32-bit unsigned integer with four implied decimals:
`$187.25` arrives as `1872500`. We keep it that way (`Price = std::uint32_t`)
through the whole pipeline.

Rejected: `double`. Price levels are keyed by price. `0.0001` has no exact
binary representation, so two prices that are economically identical could
hash or compare as different and silently split one level into two. With
integers, equality is exact and comparison is a single instruction. Division
by 10,000 happens exactly once, in `format_price`, for display.

### Timestamps are nanoseconds since midnight

ITCH timestamps are 48-bit nanoseconds since midnight, US/Eastern. We widen to
`std::uint64_t` and never convert to a calendar type on the hot path.

### Symbols are fixed 8-byte arrays

`Symbol` is `std::array<char, 8>`, space-padded as on the wire. No
`std::string`, so copying a message struct never allocates. `view()` trims the
padding for display and comparison.

## 2. Reading the wire format

### Big-endian fields are assembled with shifts

```cpp
inline std::uint32_t u32(std::span<const std::byte> b, std::size_t at) {
    return (u16(b, at) << 16) | u16(b, at + 2);
}
```

Rejected: `*reinterpret_cast<const std::uint32_t*>(ptr)` followed by
`__builtin_bswap32`. ITCH fields are not naturally aligned (a `u64` order id
sits at offset 11), so that load is undefined behaviour on alignment grounds
and a strict-aliasing violation on top. On ARM it also happens to work most of
the time, which is the worst kind of bug. Assembling from bytes is defined
everywhere, and Clang and GCC both recognise the pattern and emit one load
plus one `rev` / `bswap`. The decoder benchmark (9 ns per message including
framing) confirms nothing is lost.

`std::bit_cast` would be the modern tool for a *correctly aligned* reinterpret;
it does not help with unaligned reads.

### The decoder validates length first, then reads at fixed offsets

Each message type has exactly one valid length in ITCH 5.0. `decode()` checks
`payload.size()` against the constant before touching a field. Afterwards the
field parsers index without further bounds checks. This keeps a single
validation point and no possibility of reading past the payload.

### Framing is zero-copy except at chunk boundaries

`FrameStream` returns a `std::span` into the source's buffer. The only copy is
when a frame straddles two chunks from a streaming source: the head goes into
a small carry buffer and the tail is appended. For an mmap'd file there is one
chunk, so there is never a copy.

### Handlers are a template parameter, not an interface

```cpp
template <typename Handler>
void decode(std::span<const std::byte> payload, Handler& handler);
```

Rejected: an abstract `IHandler` with virtual `on_add_order(...)` etc. A
virtual call per message is an indirect branch the predictor must learn, it
blocks inlining, and the handler's body cannot be specialised on the caller's
types. With a template the dispatch is resolved at compile time and the
`ReplayHandler` methods inline straight into the `switch`. `NullHandler`
supplies empty defaults so a handler only writes the callbacks it needs.

The `switch` orders cases by real-world frequency (add, delete, cancel,
replace, execute first). On a real feed those five are ~95% of messages.

## 3. The order book

### Two containers

```
orders_:  unordered_map<OrderId, Order{price, qty, side}>
bids_:    map<Price, Level{qty, order_count}, greater<Price>>   // best bid at begin()
asks_:    map<Price, Level{qty, order_count}, less<Price>>      // best ask at begin()
```

Every message that modifies an order arrives *by id* (execute, cancel, delete,
replace), so we need id → order. Every quote query needs the best price per
side, so we need levels sorted with the best first. Keeping only the
aggregated quantity per level (not the per-order queue) is enough for a
displayed L2 book and for the oracle; the per-order FIFO queue would only
matter for queue-position modelling, which is out of scope for v1.

### Why `std::map` and `std::unordered_map` in v1

Because they are correct and obvious, and the whole point of the optimisation
ladder is to replace them with measured justification. Baseline numbers
(bench/bench_pipeline.cpp): add+delete pair 54 ns with 10 live levels, 91 ns
with 1,000; execute at touch 17 ns.

What will replace them, and why:

- `std::unordered_map<OrderId, Order>`: every insert heap-allocates a node;
  the chain walk on lookup touches two or three cache lines. Replacement: an
  open-addressing table whose slots live in one contiguous pool, so a lookup
  is one hash and typically one cache line.
- `std::map<Price, Level>`: a red-black tree; `begin()` is cached but every
  insert/erase walks pointers across cache lines and rebalances. Replacement:
  a sorted `std::vector<PriceLevel>` with the best price at the back. The
  active part of a book is a few dozen levels, and `memmove` of a few hundred
  bytes beats pointer chasing. New levels near the touch (the common case) go
  near the end, so the shift is short.

### Empty levels are erased immediately

`reduce_level` erases a level when its order count reaches zero. If an empty
level lingered, `best_bid()` could report a price with no size behind it and
the oracle's at-touch check would be meaningless.

### Replace is delete + add

A `U` message gives an order a new id, price and size and, per the spec, moves
it to the back of the queue at the new price. Since we keep aggregated levels
only, `remove(old) + add(new, same side)` is exactly right. The side is read
from the old order before it is removed because the `U` message does not carry
one.

### Why `cancel` to zero removes the order

An `X` message cancels part of an order. The spec says the remaining shares
stay on the book, but if the remainder is zero there is nothing left to rest.
Treating it as a delete keeps `order_count` consistent with reality and lets
the level be erased. Real feeds do contain cancel-to-zero messages.

## 4. The correctness oracle

The exchange never sends the book, so there is nothing to diff against. There
are, however, statements the exchange makes that are only consistent with a
correct book:

1. **Every `E`, `C`, `X`, `D`, `U` names an order we hold.** If we ever see an
   unknown id, we lost or mis-parsed an earlier message. Count: 0 on 4.48 M
   real messages.
2. **An `E` execution happens at the touch.** NASDAQ matches displayed orders
   with strict price priority, so if order 9005 (a bid at $107.26) executes,
   no displayed bid above $107.26 existed at that instant. We check that the
   executed order's price equals our best price on its side, before applying
   the message. 8,513 / 8,513 agreed. Had we been wrong about one level, one
   ordering or one quantity anywhere in that symbol's history, the touch would
   have disagreed.
3. **Executions never exceed the resting size.** An overfill means a cancel or
   partial execution was missed.
4. **The displayed book is never crossed while trading.** A bid at or above the
   best ask would have matched. During a halt or quote-only period, though,
   orders accumulate without matching, so a crossed book is legitimate. We
   track each stock's `H` trading state and count the two cases separately.
   The 7 crossings in the real data were all in one halted symbol (EYEN,
   state `Q`).

`C` (executed with price) is counted but not touch-checked: it is used for
cross executions at a price that differs from the displayed one.

This is a check against the exchange, not against another part of our own
code, which is what makes it evidence rather than a tautology.

## 5. Signals

### Rolling mean and variance: windowed Welford

The textbook `var = E[x²] - E[x]²` is numerically catastrophic when the mean
is large relative to the spread (prices are ~100 with changes of ~0.01). We
use Welford's recurrence while the window fills and the windowed form
afterwards:

```
mean' = mean + (x_new - x_old) / W
M2'   = M2 + (x_new - x_old) * (x_new - mean' + x_old - mean)
```

The test `StaysAccurateWithLargeOffset` pushes values around 1e9 and checks
the standard deviation to 1e-6.

### Rolling median: multiset with a pinned iterator

The classic two-heap streaming median cannot remove an arbitrary element,
which a sliding window needs. A `std::multiset` can; we keep an iterator on
the lower median and nudge it by one after each insert or erase based on
which side of it the element fell. O(log n) per push, O(1) query. The
duplicate-value case is subtle (`lower_bound` returns the first equal
element, which may precede the median) and is covered by a dedicated test.

`StreamingMedian` (two heaps) is also provided: it is the version asked in
interviews and the right choice when the window is unbounded.

### Microprice and imbalance

```
microprice = (bid_px * ask_qty + ask_px * bid_qty) / (bid_qty + ask_qty)
imbalance  = (bid_qty - ask_qty) / (bid_qty + ask_qty)      in [-1, 1]
```

The microprice leans toward the side with *less* resting size, which is the
side more likely to be consumed next; it is a better short-horizon fair price
than the mid. Both are computed from the top of book after each update to the
watched symbol, so they cost a few flops per message.

## 6. The replay loop and measurement

`Replayer` does not sleep or pace. It exposes `step()`, `run_until(ts)` and
`run_all()`. Pacing (play at 1x, 20x) lives in the TUI's engine thread, which
keeps the hot loop tight and the replayer trivially testable.

Per-message latency is measured around `decode()` only: from "payload bytes in
hand" to "books, oracle and signals updated". I/O and decompression are
excluded on purpose; they are a property of the source, not the engine.

The histogram is exact at 1 ns resolution up to 100 µs, with an overflow
bucket. Percentiles are read by walking the buckets. We report p50, p90, p99,
p99.9 and max; the mean is printed but is the least informative number on a
heavy-tailed distribution.

Known limitation: macOS `steady_clock` on Apple Silicon has a 41.67 ns tick
(24 MHz), so individual samples are quantised. Step 2 of the ladder replaces
it with the cycle counter.

## 7. The terminal player

Two threads. The engine thread runs the replay pipeline and, about 25 times a
second, publishes a `Snapshot` (top levels, signals, tape, stats) under a
mutex. The UI thread copies the latest snapshot and renders it with FTXUI. The
hot path never waits for the UI, and the UI never touches the books.

Controls are `std::atomic`s read by the engine between messages: pause,
speed, step requests, a skip-to timestamp. Pacing anchors (wall time, message
time) are reset whenever the speed changes or playback resumes, so the replay
never tries to "catch up" after a pause.

## 8. Testing strategy

- **Independent encoder.** `itch::Encoder` writes fields at the spec's
  offsets using its own `put_*` helpers, not the decoder's tables. A round-trip
  test therefore checks two independent readings of the spec against each
  other.
- **Boundary sweep.** `FrameStream` is fed the same stream with every chunk
  size from 1 byte up, because off-by-one bugs in reassembly only appear at
  specific boundaries.
- **Brute-force references.** Every rolling statistic is compared against
  recomputing over the window on every push.
- **Shadow model.** A randomised 5,000-message session is applied both to the
  real `BookManager` and to a naive vector-of-orders model; live order counts
  and best bid must agree, and the oracle must be 100%.
- **Sanitizers.** `-DTAPEDECK_SANITIZE=address,undefined` builds run the test
  suite and the full real-data replay clean.

## 9. Interview questions this project answers

- *Why `std::map` vs `std::unordered_map` for X?* Section 3, with numbers.
- *How do you parse unaligned big-endian data safely and fast?* Section 2.
- *Virtual dispatch vs templates on a hot path?* Section 2.
- *How would you know your order book is right?* Section 4.
- *Implement a streaming median.* `StreamingMedian`; and why it is the wrong
  tool for a sliding window (`RollingMedian`).
- *Why not `E[x²] - E[x]²`?* Section 5.
- *What does p99 tell you that the mean does not?* Section 6.
- *How do you keep a UI from slowing down a trading engine?* Section 7.
