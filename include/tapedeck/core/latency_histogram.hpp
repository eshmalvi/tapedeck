#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace tapedeck {

// Exact histogram of latencies with 1 ns resolution up to `max_ns`.
//
// Latency distributions are heavy-tailed, so the mean hides what matters.
// We keep every sample's bucket so percentiles (p50, p99, p99.9) are exact
// within range; samples above `max_ns` are counted separately and only their
// maximum is kept.
class LatencyHistogram {
 public:
    explicit LatencyHistogram(std::uint64_t max_ns = 100'000) : counts_(max_ns + 1, 0) {}

    void record(std::uint64_t ns) {
        ++total_;
        sum_ += ns;
        max_ = std::max(max_, ns);
        if (ns < counts_.size()) {
            ++counts_[ns];
        } else {
            ++overflow_;
        }
    }

    std::uint64_t count() const { return total_; }
    std::uint64_t max() const { return max_; }
    std::uint64_t overflow() const { return overflow_; }

    double mean() const { return total_ == 0 ? 0.0 : static_cast<double>(sum_) / total_; }

    // Smallest latency such that at least `fraction` of samples are <= it.
    // `fraction` is in [0, 1]. Returns max() if the answer lies in the overflow bucket.
    std::uint64_t percentile(double fraction) const {
        if (total_ == 0) {
            return 0;
        }
        const auto target = static_cast<std::uint64_t>(fraction * static_cast<double>(total_));
        std::uint64_t seen = 0;
        for (std::uint64_t ns = 0; ns < counts_.size(); ++ns) {
            seen += counts_[ns];
            if (seen >= target && seen > 0) {
                return ns;
            }
        }
        return max_;
    }

    void reset() {
        std::fill(counts_.begin(), counts_.end(), 0);
        total_ = sum_ = max_ = overflow_ = 0;
    }

 private:
    std::vector<std::uint32_t> counts_;
    std::uint64_t total_ = 0;
    std::uint64_t sum_ = 0;
    std::uint64_t max_ = 0;
    std::uint64_t overflow_ = 0;
};

}  // namespace tapedeck
