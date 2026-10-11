#include "tapedeck/replay/replayer.hpp"

#include "tapedeck/core/clock.hpp"
#include "tapedeck/itch/decoder.hpp"

namespace tapedeck::replay {

Replayer::Replayer(itch::ByteSource& source, ReplayHandler& handler, bool measure_latency)
    : frames_(source), handler_(handler), measure_latency_(measure_latency) {}

bool Replayer::process(std::span<const std::byte> payload) {
    if (measure_latency_) {
        const auto start = now_ns();
        itch::decode(payload, handler_);
        stats_.latency.record(now_ns() - start);
    } else {
        itch::decode(payload, handler_);
    }
    ++stats_.messages;
    stats_.bytes = frames_.bytes_consumed();
    stats_.last_timestamp = handler_.last_timestamp();
    if (stats_.first_timestamp == 0) {
        stats_.first_timestamp = stats_.last_timestamp;
    }
    return true;
}

bool Replayer::step() {
    if (finished_) {
        return false;
    }
    const auto start = now_ns();
    const auto payload = frames_.next();
    if (payload.empty()) {
        finished_ = true;
        stats_.wall_ns += now_ns() - start;
        return false;
    }
    process(payload);
    stats_.wall_ns += now_ns() - start;
    return true;
}

bool Replayer::run_until(Timestamp until) {
    const auto start = now_ns();
    bool reached = false;
    while (!finished_) {
        const auto payload = frames_.next();
        if (payload.empty()) {
            finished_ = true;
            break;
        }
        process(payload);
        if (stats_.last_timestamp >= until) {
            reached = true;
            break;
        }
    }
    stats_.wall_ns += now_ns() - start;
    return reached;
}

void Replayer::run_all() {
    const auto start = now_ns();
    while (!finished_) {
        const auto payload = frames_.next();
        if (payload.empty()) {
            finished_ = true;
            break;
        }
        process(payload);
    }
    stats_.wall_ns += now_ns() - start;
}

}  // namespace tapedeck::replay
