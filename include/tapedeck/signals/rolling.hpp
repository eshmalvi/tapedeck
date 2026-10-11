#pragma once

#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <queue>
#include <set>
#include <stdexcept>
#include <vector>

// Streaming statistics that update in O(1) or O(log n) per observation
// without ever recomputing over the window.
namespace tapedeck::signals {

// Mean and standard deviation over the last `window` values.
//
// Uses Welford's algorithm while filling, then the windowed update
//     mean' = mean + (x_new - x_old) / W
//     M2'   = M2 + (x_new - x_old) * (x_new - mean' + x_old - mean)
// once full. Both avoid the catastrophic cancellation of the textbook
// "sum of squares minus mean squared" formula.
class RollingStats {
 public:
    explicit RollingStats(std::size_t window) : window_(window), buffer_(window, 0.0) {
        if (window < 2) {
            throw std::invalid_argument("RollingStats window must be >= 2");
        }
    }

    void push(double x) {
        if (count_ < window_) {
            // Standard Welford: window still filling.
            ++count_;
            const double delta = x - mean_;
            mean_ += delta / static_cast<double>(count_);
            m2_ += delta * (x - mean_);
        } else {
            // Replace the oldest value with x.
            const double x_old = buffer_[head_];
            const double old_mean = mean_;
            mean_ += (x - x_old) / static_cast<double>(window_);
            m2_ += (x - x_old) * (x - mean_ + x_old - old_mean);
            if (m2_ < 0.0) {
                m2_ =
                    0.0;  // floating-point noise can push a zero-variance window slightly negative
            }
        }
        buffer_[head_] = x;
        head_ = (head_ + 1) % window_;
    }

    std::size_t count() const { return count_; }
    bool full() const { return count_ == window_; }
    double mean() const { return mean_; }

    // Sample variance (ddof = 1), matching pandas.rolling().std() defaults.
    double variance() const { return count_ < 2 ? 0.0 : m2_ / static_cast<double>(count_ - 1); }
    double stddev() const { return std::sqrt(variance()); }

 private:
    std::size_t window_;
    std::vector<double> buffer_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

// Exponentially weighted moving average: value' = alpha * x + (1 - alpha) * value.
class Ewma {
 public:
    explicit Ewma(double alpha) : alpha_(alpha) {
        if (alpha <= 0.0 || alpha > 1.0) {
            throw std::invalid_argument("Ewma alpha must be in (0, 1]");
        }
    }

    // Alpha such that the weight halves every `half_life` observations.
    static Ewma with_half_life(double half_life) { return Ewma(1.0 - std::exp2(-1.0 / half_life)); }

    void push(double x) {
        value_ = initialised_ ? alpha_ * x + (1.0 - alpha_) * value_ : x;
        initialised_ = true;
    }

    bool initialised() const { return initialised_; }
    double value() const { return value_; }

 private:
    double alpha_;
    double value_ = 0.0;
    bool initialised_ = false;
};

// Median of an unbounded stream: two heaps, O(log n) per push, O(1) query.
//
// `low_` holds the smaller half (max-heap), `high_` the larger half (min-heap).
// We keep low_.size() == high_.size() or low_.size() == high_.size() + 1, so
// the median is low_.top() or the average of the two tops.
class StreamingMedian {
 public:
    void push(double x) {
        if (low_.empty() || x <= low_.top()) {
            low_.push(x);
        } else {
            high_.push(x);
        }
        rebalance();
    }

    std::size_t count() const { return low_.size() + high_.size(); }

    double median() const {
        if (low_.empty()) {
            return 0.0;
        }
        if (low_.size() > high_.size()) {
            return low_.top();
        }
        return (low_.top() + high_.top()) / 2.0;
    }

 private:
    void rebalance() {
        if (low_.size() > high_.size() + 1) {
            high_.push(low_.top());
            low_.pop();
        } else if (high_.size() > low_.size()) {
            low_.push(high_.top());
            high_.pop();
        }
    }

    std::priority_queue<double> low_;                                              // max-heap
    std::priority_queue<double, std::vector<double>, std::greater<double>> high_;  // min-heap
};

// Median over the last `window` values: an ordered multiset plus an iterator
// pinned to the median position, O(log n) per push.
//
// Heaps cannot remove arbitrary elements, which a sliding window needs. A
// balanced tree can, and nudging the median iterator left or right by one
// after each insert/erase keeps the query O(1).
class RollingMedian {
 public:
    explicit RollingMedian(std::size_t window) : window_(window) {
        if (window < 1) {
            throw std::invalid_argument("RollingMedian window must be >= 1");
        }
    }

    void push(double x) {
        history_.push_back(x);
        insert(x);
        if (history_.size() > window_) {
            erase(history_.front());
            history_.pop_front();
        }
    }

    std::size_t count() const { return values_.size(); }
    bool full() const { return values_.size() == window_; }

    double median() const {
        if (values_.empty()) {
            return 0.0;
        }
        // mid_ points at the lower median; for even counts average it with the next value.
        if (values_.size() % 2 == 1) {
            return *mid_;
        }
        return (*mid_ + *std::next(mid_)) / 2.0;
    }

 private:
    void insert(double x) {
        values_.insert(x);
        if (values_.size() == 1) {
            mid_ = values_.begin();
            return;
        }
        // Keep mid_ on the lower median: it shifts right when a value lands at/after it
        // and the count becomes odd, or left when a value lands before it and the count is even.
        if (x < *mid_) {
            if (values_.size() % 2 == 0) {
                --mid_;
            }
        } else {
            if (values_.size() % 2 == 1) {
                ++mid_;
            }
        }
    }

    void erase(double x) {
        // lower_bound returns the FIRST element equal to x, so when x equals the
        // median value but `it != mid_`, `it` sits before mid_ in the ordering.
        auto it = values_.lower_bound(x);
        const std::size_t n = values_.size();  // size before the erase
        if (it == mid_) {
            // Removing the median itself: step to the neighbour that becomes the new lower median.
            mid_ = n % 2 == 0 ? std::next(mid_) : std::prev(mid_);
        } else if (*it <= *mid_) {
            // An element before mid_ disappears, so mid_'s index drops by one.
            if (n % 2 == 0) {
                ++mid_;
            }
        } else {
            // An element after mid_ disappears; mid_'s index is unchanged.
            if (n % 2 == 1) {
                --mid_;
            }
        }
        values_.erase(it);
    }

    std::size_t window_;
    std::multiset<double> values_;
    std::multiset<double>::iterator mid_;
    std::deque<double> history_;  // insertion order, so we know which value expires
};

}  // namespace tapedeck::signals
