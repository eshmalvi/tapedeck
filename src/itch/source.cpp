#include "tapedeck/itch/source.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <cstdio>
#include <stdexcept>
#include <system_error>

namespace tapedeck::itch {

namespace {

[[noreturn]] void throw_errno(const std::string& what, const std::filesystem::path& path) {
    throw std::system_error(errno, std::generic_category(), what + ": " + path.string());
}

}  // namespace

// ---------------------------------------------------------------------------
// MmapSource
// ---------------------------------------------------------------------------
MmapSource::MmapSource(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw_errno("open", path);
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw_errno("fstat", path);
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ > 0) {
        void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd);
            throw_errno("mmap", path);
        }
        // We read front to back exactly once: let the kernel read ahead aggressively.
        ::madvise(mapped, size_, MADV_SEQUENTIAL);
        data_ = static_cast<const std::byte*>(mapped);
    }
    ::close(fd);
}

MmapSource::~MmapSource() {
    if (data_ != nullptr) {
        ::munmap(const_cast<std::byte*>(data_), size_);
    }
}

std::span<const std::byte> MmapSource::next_chunk() {
    if (delivered_) {
        return {};
    }
    delivered_ = true;
    return {data_, size_};
}

// ---------------------------------------------------------------------------
// GzipSource
// ---------------------------------------------------------------------------
struct GzipSource::Impl {
    std::FILE* file = nullptr;
    z_stream stream{};
    std::vector<std::byte> in;
    std::vector<std::byte> out;
    bool finished = false;

    ~Impl() {
        inflateEnd(&stream);
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

GzipSource::GzipSource(const std::filesystem::path& path, std::size_t chunk_bytes)
    : impl_(std::make_unique<Impl>()) {
    impl_->file = std::fopen(path.c_str(), "rb");
    if (impl_->file == nullptr) {
        throw_errno("fopen", path);
    }
    impl_->in.resize(1u << 20);
    impl_->out.resize(chunk_bytes);
    // 16 + MAX_WBITS tells zlib to expect a gzip header rather than a raw deflate stream.
    if (inflateInit2(&impl_->stream, 16 + MAX_WBITS) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }
}

GzipSource::~GzipSource() = default;

std::span<const std::byte> GzipSource::next_chunk() {
    Impl& s = *impl_;
    if (s.finished) {
        return {};
    }

    s.stream.next_out = reinterpret_cast<Bytef*>(s.out.data());
    s.stream.avail_out = static_cast<uInt>(s.out.size());

    // Keep inflating until the output chunk is full or input is exhausted.
    while (s.stream.avail_out > 0) {
        if (s.stream.avail_in == 0) {
            const auto read = std::fread(s.in.data(), 1, s.in.size(), s.file);
            if (read == 0) {
                // Truncated archive (a prefix download): stop at what we have.
                s.finished = true;
                break;
            }
            s.stream.next_in = reinterpret_cast<Bytef*>(s.in.data());
            s.stream.avail_in = static_cast<uInt>(read);
        }
        const int rc = inflate(&s.stream, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) {
            s.finished = true;
            break;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
            throw std::runtime_error(std::string("gzip decode error: ") +
                                     (s.stream.msg ? s.stream.msg : "unknown"));
        }
    }

    const std::size_t produced = s.out.size() - s.stream.avail_out;
    return {s.out.data(), produced};
}

// ---------------------------------------------------------------------------
// MemorySource
// ---------------------------------------------------------------------------
MemorySource::MemorySource(std::vector<std::byte> data, std::size_t chunk_bytes)
    : data_(std::move(data)), chunk_bytes_(chunk_bytes == 0 ? 1 : chunk_bytes) {}

std::span<const std::byte> MemorySource::next_chunk() {
    if (offset_ >= data_.size()) {
        return {};
    }
    const auto n = std::min(chunk_bytes_, data_.size() - offset_);
    std::span<const std::byte> chunk(data_.data() + offset_, n);
    offset_ += n;
    return chunk;
}

// ---------------------------------------------------------------------------
std::unique_ptr<ByteSource> open_source(const std::filesystem::path& path) {
    if (path.extension() == ".gz") {
        return std::make_unique<GzipSource>(path);
    }
    return std::make_unique<MmapSource>(path);
}

}  // namespace tapedeck::itch
