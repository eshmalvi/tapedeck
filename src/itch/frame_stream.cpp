#include "tapedeck/itch/frame_stream.hpp"

#include "tapedeck/core/byte_reader.hpp"

#include <algorithm>

namespace tapedeck::itch {

bool FrameStream::refill() {
    if (exhausted_) {
        return false;
    }
    chunk_ = source_.next_chunk();
    pos_ = 0;
    if (chunk_.empty()) {
        exhausted_ = true;
        return false;
    }
    return true;
}

std::span<const std::byte> FrameStream::next() {
    // Fast path: the whole frame is inside the current chunk.
    while (true) {
        const std::size_t available = chunk_.size() - pos_;
        if (available >= kLengthPrefix) {
            const std::size_t length = bytes::u16(chunk_, pos_);
            if (length == 0) {
                exhausted_ = true;  // end-of-file marker
                return {};
            }
            if (available >= kLengthPrefix + length) {
                auto payload = chunk_.subspan(pos_ + kLengthPrefix, length);
                pos_ += kLengthPrefix + length;
                bytes_consumed_ += kLengthPrefix + length;
                return payload;
            }
        }
        if (available > 0) {
            // A frame straddles the chunk boundary: stash its head and finish it below.
            carry_.assign(chunk_.begin() + static_cast<std::ptrdiff_t>(pos_), chunk_.end());
            pos_ = chunk_.size();
            return next_from_carry();
        }
        if (!refill()) {
            return {};
        }
    }
}

// Completes a frame whose first bytes are already in carry_, pulling the rest
// from as many further chunks as needed.
std::span<const std::byte> FrameStream::next_from_carry() {
    while (true) {
        // How many bytes does this frame need in total?
        std::size_t needed = kLengthPrefix;
        if (carry_.size() >= kLengthPrefix) {
            const std::size_t length = bytes::u16(carry_, 0);
            if (length == 0) {
                exhausted_ = true;
                return {};
            }
            needed = kLengthPrefix + length;
            if (carry_.size() >= needed) {
                bytes_consumed_ += needed;
                return std::span<const std::byte>(carry_).subspan(kLengthPrefix,
                                                                  needed - kLengthPrefix);
            }
        }
        if (pos_ >= chunk_.size() && !refill()) {
            return {};  // truncated tail: drop the partial frame
        }
        const std::size_t take = std::min(needed - carry_.size(), chunk_.size() - pos_);
        carry_.insert(carry_.end(), chunk_.begin() + static_cast<std::ptrdiff_t>(pos_),
                      chunk_.begin() + static_cast<std::ptrdiff_t>(pos_ + take));
        pos_ += take;
    }
}

}  // namespace tapedeck::itch
