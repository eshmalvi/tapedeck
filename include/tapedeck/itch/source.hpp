#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tapedeck::itch {

// A sequential supplier of raw ITCH bytes.
//
// `next_chunk` returns the next block of bytes, or an empty span at end of
// input. A chunk stays valid until the following call. Chunk boundaries fall
// anywhere, so callers must cope with a message straddling two chunks; see
// FrameStream.
class ByteSource {
 public:
    virtual ~ByteSource() = default;
    virtual std::span<const std::byte> next_chunk() = 0;
    // Total input size in bytes when known up front (0 otherwise). Used for progress display.
    virtual std::uint64_t size_hint() const { return 0; }
};

// Memory-maps an uncompressed ITCH file and hands it out as a single chunk.
// Zero-copy: the decoder reads straight from the page cache.
class MmapSource : public ByteSource {
 public:
    explicit MmapSource(const std::filesystem::path& path);
    ~MmapSource() override;
    MmapSource(const MmapSource&) = delete;
    MmapSource& operator=(const MmapSource&) = delete;

    std::span<const std::byte> next_chunk() override;
    std::uint64_t size_hint() const override { return size_; }

 private:
    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    bool delivered_ = false;
};

// Streams a gzip-compressed ITCH file through zlib in fixed-size chunks.
// A truncated archive (e.g. a byte-range prefix download) ends cleanly at the
// last complete block instead of failing.
class GzipSource : public ByteSource {
 public:
    explicit GzipSource(const std::filesystem::path& path, std::size_t chunk_bytes = 4u << 20);
    ~GzipSource() override;
    GzipSource(const GzipSource&) = delete;
    GzipSource& operator=(const GzipSource&) = delete;

    std::span<const std::byte> next_chunk() override;

 private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Serves bytes already in memory, in chunks of `chunk_bytes`. Used by tests to
// force message boundaries to land anywhere, and by the benchmarks.
class MemorySource : public ByteSource {
 public:
    MemorySource(std::vector<std::byte> data, std::size_t chunk_bytes);

    std::span<const std::byte> next_chunk() override;
    std::uint64_t size_hint() const override { return data_.size(); }
    void rewind() { offset_ = 0; }

 private:
    std::vector<std::byte> data_;
    std::size_t chunk_bytes_;
    std::size_t offset_ = 0;
};

// Picks MmapSource or GzipSource from the file extension (".gz" -> gzip).
std::unique_ptr<ByteSource> open_source(const std::filesystem::path& path);

}  // namespace tapedeck::itch
