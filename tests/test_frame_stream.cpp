// FrameStream: the same stream must decode identically no matter where the
// chunk boundaries fall, and end-of-file / truncation must be handled.

#include "tapedeck/itch/decoder.hpp"
#include "tapedeck/itch/encoder.hpp"
#include "tapedeck/itch/frame_stream.hpp"
#include "tapedeck/itch/source.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace tapedeck;
using namespace tapedeck::itch;

namespace {

Header header_of(MessageType type) {
    return Header{type, 1234, 7, 34'200'000'000'123ull};
}

// 54 messages: a system event, a directory entry, 50 adds, one execute, one NOII.
std::vector<std::byte> sample_stream() {
    std::vector<std::byte> out;
    Encoder::frame(out, Encoder::system_event(header_of(MessageType::SystemEvent), 'O'));
    Encoder::frame(out, Encoder::stock_directory(header_of(MessageType::StockDirectory),
                                                 Symbol::from("AAPL"), 'Q', 100));
    for (OrderId id = 1; id <= 50; ++id) {
        Encoder::frame(out,
                       Encoder::add_order(header_of(MessageType::AddOrder), id, Side::Buy, 100,
                                          Symbol::from("AAPL"), 1000000 + static_cast<Price>(id)));
    }
    Encoder::frame(out, Encoder::order_executed(header_of(MessageType::OrderExecuted), 50, 100, 1));
    Encoder::frame(out, Encoder::other(header_of(MessageType::Noii), 50));
    return out;
}

struct CountingHandler : NullHandler {
    int adds = 0, executes = 0, others = 0, directories = 0, events = 0, malformed = 0;
    OrderId last_add_id = 0;
    void on_system_event(const SystemEvent&) { ++events; }
    void on_stock_directory(const StockDirectory&) { ++directories; }
    void on_add_order(const AddOrder& m) {
        ++adds;
        last_add_id = m.order_id;
    }
    void on_order_executed(const OrderExecuted&) { ++executes; }
    void on_other(MessageType, std::span<const std::byte>) { ++others; }
    void on_malformed(std::span<const std::byte>) { ++malformed; }
};

}  // namespace

TEST(FrameStream, SplitsFramesRegardlessOfChunkBoundaries) {
    const auto stream = sample_stream();
    for (std::size_t chunk = 1; chunk <= stream.size() + 1; chunk += (chunk < 64 ? 1 : 37)) {
        MemorySource source(stream, chunk);
        FrameStream frames(source);
        CountingHandler h;
        std::size_t count = 0;
        for (auto payload = frames.next(); !payload.empty(); payload = frames.next()) {
            decode(payload, h);
            ++count;
        }
        EXPECT_EQ(count, 54u) << "chunk size " << chunk;
        EXPECT_EQ(h.adds, 50) << "chunk size " << chunk;
        EXPECT_EQ(h.last_add_id, 50u) << "chunk size " << chunk;
        EXPECT_EQ(h.executes, 1);
        EXPECT_EQ(h.others, 1);
        EXPECT_EQ(h.directories, 1);
        EXPECT_EQ(h.events, 1);
        EXPECT_EQ(h.malformed, 0);
        EXPECT_EQ(frames.bytes_consumed(), stream.size());
    }
}

TEST(FrameStream, StopsAtZeroLengthMarker) {
    auto stream = sample_stream();
    const auto full_size = stream.size();
    stream.push_back(std::byte{0});
    stream.push_back(std::byte{0});
    Encoder::frame(
        stream, Encoder::order_delete(header_of(MessageType::OrderDelete), 1));  // after EOF marker
    MemorySource source(stream, 4096);
    FrameStream frames(source);
    std::size_t count = 0;
    for (auto payload = frames.next(); !payload.empty(); payload = frames.next()) {
        ++count;
    }
    EXPECT_EQ(count, 54u);
    EXPECT_EQ(frames.bytes_consumed(), full_size);
}

TEST(FrameStream, DropsTruncatedTail) {
    auto stream = sample_stream();
    stream.resize(stream.size() - 10);  // cut the last frame in half
    for (std::size_t chunk : {1u, 7u, 4096u}) {
        MemorySource source(stream, chunk);
        FrameStream frames(source);
        std::size_t count = 0;
        for (auto payload = frames.next(); !payload.empty(); payload = frames.next()) {
            ++count;
        }
        EXPECT_EQ(count, 53u) << "chunk size " << chunk;
    }
}

TEST(FrameStream, EmptySourceYieldsNothing) {
    MemorySource source({}, 16);
    FrameStream frames(source);
    EXPECT_TRUE(frames.next().empty());
    EXPECT_TRUE(frames.next().empty());
}
