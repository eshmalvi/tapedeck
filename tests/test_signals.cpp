#include "tapedeck/book/order_book.hpp"
#include "tapedeck/signals/book_signals.hpp"
#include "tapedeck/signals/rolling.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace tapedeck;
using namespace tapedeck::signals;

namespace {

// Brute-force references the streaming classes must agree with.
double brute_mean(const std::vector<double>& w) {
    double sum = 0.0;
    for (double x : w)
        sum += x;
    return sum / static_cast<double>(w.size());
}

double brute_stddev(const std::vector<double>& w) {
    if (w.size() < 2)
        return 0.0;
    const double mean = brute_mean(w);
    double m2 = 0.0;
    for (double x : w)
        m2 += (x - mean) * (x - mean);
    return std::sqrt(m2 / static_cast<double>(w.size() - 1));
}

double brute_median(std::vector<double> w) {
    std::sort(w.begin(), w.end());
    const auto n = w.size();
    return n % 2 == 1 ? w[n / 2] : (w[n / 2 - 1] + w[n / 2]) / 2.0;
}

std::vector<double> random_series(std::size_t n, unsigned seed, bool integers = false) {
    std::mt19937 rng(seed);
    std::vector<double> out(n);
    if (integers) {
        std::uniform_int_distribution<int> dist(-5, 5);  // many duplicates
        for (double& x : out)
            x = dist(rng);
    } else {
        std::normal_distribution<double> dist(100.0, 3.0);
        for (double& x : out)
            x = dist(rng);
    }
    return out;
}

std::vector<double> last_window(const std::vector<double>& series, std::size_t end,
                                std::size_t window) {
    const std::size_t start = end + 1 >= window ? end + 1 - window : 0;
    return {series.begin() + static_cast<std::ptrdiff_t>(start),
            series.begin() + static_cast<std::ptrdiff_t>(end + 1)};
}

}  // namespace

TEST(RollingStats, MatchesBruteForceWhileFillingAndWhenFull) {
    const std::size_t window = 20;
    const auto series = random_series(500, 1);
    RollingStats stats(window);
    for (std::size_t i = 0; i < series.size(); ++i) {
        stats.push(series[i]);
        const auto w = last_window(series, i, window);
        EXPECT_EQ(stats.count(), w.size());
        EXPECT_EQ(stats.full(), w.size() == window);
        EXPECT_NEAR(stats.mean(), brute_mean(w), 1e-9) << "at " << i;
        EXPECT_NEAR(stats.stddev(), brute_stddev(w), 1e-9) << "at " << i;
    }
}

TEST(RollingStats, ConstantSeriesHasZeroVariance) {
    RollingStats stats(5);
    for (int i = 0; i < 100; ++i) {
        stats.push(42.0);
    }
    EXPECT_DOUBLE_EQ(stats.mean(), 42.0);
    EXPECT_DOUBLE_EQ(stats.variance(), 0.0);
}

TEST(RollingStats, StaysAccurateWithLargeOffset) {
    // The naive sum-of-squares formula loses everything here; the windowed Welford must not.
    RollingStats stats(10);
    for (int i = 0; i < 1000; ++i) {
        stats.push(1e9 + (i % 2 == 0 ? 0.5 : -0.5));
    }
    EXPECT_NEAR(stats.mean(), 1e9, 1e-6);
    EXPECT_NEAR(stats.stddev(), std::sqrt(10.0 * 0.25 / 9.0), 1e-6);
}

TEST(RollingStats, RejectsTinyWindow) {
    EXPECT_THROW(RollingStats(1), std::invalid_argument);
}

TEST(Ewma, FollowsRecurrence) {
    Ewma ewma(0.5);
    EXPECT_FALSE(ewma.initialised());
    ewma.push(10.0);
    EXPECT_TRUE(ewma.initialised());
    EXPECT_DOUBLE_EQ(ewma.value(), 10.0);
    ewma.push(20.0);
    EXPECT_DOUBLE_EQ(ewma.value(), 15.0);
    ewma.push(20.0);
    EXPECT_DOUBLE_EQ(ewma.value(), 17.5);
}

TEST(Ewma, HalfLifeHalvesWeight) {
    Ewma ewma = Ewma::with_half_life(1.0);  // alpha = 0.5
    ewma.push(0.0);
    ewma.push(1.0);
    EXPECT_NEAR(ewma.value(), 0.5, 1e-12);
    EXPECT_THROW(Ewma(0.0), std::invalid_argument);
    EXPECT_THROW(Ewma(1.5), std::invalid_argument);
}

TEST(StreamingMedian, MatchesSortedMedian) {
    const auto series = random_series(301, 2);
    StreamingMedian median;
    std::vector<double> seen;
    for (double x : series) {
        median.push(x);
        seen.push_back(x);
        EXPECT_EQ(median.count(), seen.size());
        EXPECT_DOUBLE_EQ(median.median(), brute_median(seen));
    }
}

TEST(StreamingMedian, HandlesDuplicates) {
    StreamingMedian median;
    for (double x : {5.0, 5.0, 5.0, 1.0, 9.0, 5.0}) {
        median.push(x);
    }
    EXPECT_DOUBLE_EQ(median.median(), 5.0);
}

TEST(RollingMedian, MatchesBruteForceOddWindow) {
    const std::size_t window = 7;
    const auto series = random_series(400, 3);
    RollingMedian median(window);
    for (std::size_t i = 0; i < series.size(); ++i) {
        median.push(series[i]);
        const auto w = last_window(series, i, window);
        EXPECT_EQ(median.count(), w.size());
        EXPECT_DOUBLE_EQ(median.median(), brute_median(w)) << "at " << i;
    }
}

TEST(RollingMedian, MatchesBruteForceEvenWindow) {
    const std::size_t window = 10;
    const auto series = random_series(400, 4);
    RollingMedian median(window);
    for (std::size_t i = 0; i < series.size(); ++i) {
        median.push(series[i]);
        EXPECT_DOUBLE_EQ(median.median(), brute_median(last_window(series, i, window)))
            << "at " << i;
    }
}

TEST(RollingMedian, MatchesBruteForceWithManyDuplicates) {
    for (std::size_t window : {1u, 2u, 3u, 8u, 25u}) {
        const auto series =
            random_series(600, 5 + static_cast<unsigned>(window), /*integers=*/true);
        RollingMedian median(window);
        for (std::size_t i = 0; i < series.size(); ++i) {
            median.push(series[i]);
            EXPECT_DOUBLE_EQ(median.median(), brute_median(last_window(series, i, window)))
                << "window " << window << " at " << i;
        }
    }
}

TEST(RollingMedian, RejectsZeroWindow) {
    EXPECT_THROW(RollingMedian(0), std::invalid_argument);
}

TEST(BookSignals, MidMicropriceAndImbalance) {
    book::OrderBook book;
    book.add(1, Side::Buy, 1000000, 300);   // 100.00 x 300
    book.add(2, Side::Sell, 1000200, 100);  // 100.02 x 100
    const auto top = top_of_book(book);
    ASSERT_TRUE(top.two_sided());
    EXPECT_NEAR(top.spread(), 0.02, 1e-9);
    EXPECT_NEAR(top.mid(), 100.01, 1e-9);
    // More size on the bid pushes the microprice toward the ask.
    EXPECT_NEAR(top.microprice(), (100.00 * 100 + 100.02 * 300) / 400.0, 1e-9);
    EXPECT_NEAR(top.imbalance(), 0.5, 1e-9);
}

TEST(BookSignals, OneSidedBookIsNotTwoSided) {
    book::OrderBook book;
    book.add(1, Side::Buy, 1000000, 300);
    EXPECT_FALSE(top_of_book(book).two_sided());
}
