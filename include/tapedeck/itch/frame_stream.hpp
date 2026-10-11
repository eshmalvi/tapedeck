#pragma once

#include "tapedeck/itch/source.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tapedeck::itch {

// Splits a byte stream into ITCH messages.
//
// Nasdaq's "BinaryFILE" layout is a sequence of frames, each a 2-byte
// big-endian length followed by that many payload bytes. The payload's first
// byte is the message type. A zero length marks the end of the file.
//
// Frames normally come straight out of the source chunk (zero-copy). Only a
// frame cut in half by a chunk boundary is reassembled into a small carry
// buffer, which costs one tiny copy per chunk, not per message.
class FrameStream {
 public:
    explicit FrameStream(ByteSource& source) : source_(source) {}

    // Returns the next message payload, or an empty span at end of stream.
    // The span is valid until the next call.
    std::span<const std::byte> next();

    std::uint64_t bytes_consumed() const { return bytes_consumed_; }

 private:
    static constexpr std::size_t kLengthPrefix = 2;

    bool refill();
    std::span<const std::byte> next_from_carry();

    ByteSource& source_;
    std::span<const std::byte> chunk_;
    std::size_t pos_ = 0;
    std::vector<std::byte> carry_;
    std::uint64_t bytes_consumed_ = 0;
    bool exhausted_ = false;
};

}  // namespace tapedeck::itch
