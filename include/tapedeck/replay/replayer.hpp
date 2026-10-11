#pragma once

#include "tapedeck/core/latency_histogram.hpp"
#include "tapedeck/core/types.hpp"
#include "tapedeck/itch/frame_stream.hpp"
#include "tapedeck/itch/source.hpp"
#include "tapedeck/replay/replay_handler.hpp"

#include <cstdint>

namespace tapedeck::replay {

struct ReplayStats {
    std::uint64_t messages = 0;
    std::uint64_t bytes = 0;
    std::uint64_t wall_ns = 0;  // time spent inside step()/run_* calls
    Timestamp first_timestamp = 0;
    Timestamp last_timestamp = 0;

    // Per-message processing latency: from "payload bytes in hand" to
    // "book, oracle and signals updated". Excludes I/O and decompression.
    LatencyHistogram latency;

    double messages_per_second() const {
        return wall_ns == 0 ? 0.0
                            : static_cast<double>(messages) * 1e9 / static_cast<double>(wall_ns);
    }
};

// Drives the pipeline: source -> frames -> decode -> handler.
//
// The replayer never sleeps. Pacing (play at 1x, 10x, ...) is the caller's
// job, which keeps this class trivially testable and the hot loop tight.
class Replayer {
 public:
    Replayer(itch::ByteSource& source, ReplayHandler& handler, bool measure_latency = true);

    // Processes one message. Returns false when the stream is exhausted.
    bool step();

    // Processes messages until one with timestamp >= `until` has been applied.
    // Returns false if the stream ended first.
    bool run_until(Timestamp until);

    // Processes everything.
    void run_all();

    bool finished() const { return finished_; }
    const ReplayStats& stats() const { return stats_; }
    const ReplayHandler& handler() const { return handler_; }

 private:
    bool process(std::span<const std::byte> payload);

    itch::FrameStream frames_;
    ReplayHandler& handler_;
    ReplayStats stats_;
    bool measure_latency_;
    bool finished_ = false;
};

}  // namespace tapedeck::replay
