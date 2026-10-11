#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tapedeck {

// Reads big-endian (network order) integers out of a byte span.
//
// ITCH is big-endian and fields are not naturally aligned, so we never
// reinterpret the bytes as an integer in place (that would be undefined
// behaviour on alignment and aliasing grounds). Assembling the value with
// shifts is well-defined, and the compiler turns it into a single load+bswap.
//
// Callers are responsible for bounds: the decoder validates each message's
// length once, then reads fields at fixed offsets.
namespace bytes {

inline std::uint8_t u8(std::span<const std::byte> buffer, std::size_t offset) {
    return std::to_integer<std::uint8_t>(buffer[offset]);
}

inline char ch(std::span<const std::byte> buffer, std::size_t offset) {
    return static_cast<char>(u8(buffer, offset));
}

inline std::uint16_t u16(std::span<const std::byte> buffer, std::size_t offset) {
    return static_cast<std::uint16_t>((u8(buffer, offset) << 8) | u8(buffer, offset + 1));
}

inline std::uint32_t u32(std::span<const std::byte> buffer, std::size_t offset) {
    return (static_cast<std::uint32_t>(u16(buffer, offset)) << 16) | u16(buffer, offset + 2);
}

// ITCH timestamps are 48-bit nanoseconds since midnight.
inline std::uint64_t u48(std::span<const std::byte> buffer, std::size_t offset) {
    return (static_cast<std::uint64_t>(u16(buffer, offset)) << 32) | u32(buffer, offset + 2);
}

inline std::uint64_t u64(std::span<const std::byte> buffer, std::size_t offset) {
    return (static_cast<std::uint64_t>(u32(buffer, offset)) << 32) | u32(buffer, offset + 4);
}

template <std::size_t N>
std::array<char, N> chars(std::span<const std::byte> buffer, std::size_t offset) {
    std::array<char, N> out;
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = ch(buffer, offset + i);
    }
    return out;
}

}  // namespace bytes
}  // namespace tapedeck
