#include "tapedeck/core/byte_reader.hpp"
#include "tapedeck/core/latency_histogram.hpp"
#include "tapedeck/core/types.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace tapedeck;

TEST(ByteReader, ReadsBigEndianIntegers) {
    const std::array<std::byte, 8> raw{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                       std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                                       std::byte{0x07}, std::byte{0x08}};
    EXPECT_EQ(bytes::u8(raw, 0), 0x01u);
    EXPECT_EQ(bytes::u16(raw, 0), 0x0102u);
    EXPECT_EQ(bytes::u32(raw, 0), 0x01020304u);
    EXPECT_EQ(bytes::u48(raw, 0), 0x010203040506ull);
    EXPECT_EQ(bytes::u64(raw, 0), 0x0102030405060708ull);
    EXPECT_EQ(bytes::u16(raw, 6), 0x0708u);
}

TEST(ByteReader, ReadsMaximumValues) {
    std::array<std::byte, 8> raw;
    raw.fill(std::byte{0xFF});
    EXPECT_EQ(bytes::u16(raw, 0), 0xFFFFu);
    EXPECT_EQ(bytes::u32(raw, 0), 0xFFFFFFFFu);
    EXPECT_EQ(bytes::u48(raw, 0), 0xFFFFFFFFFFFFull);
    EXPECT_EQ(bytes::u64(raw, 0), 0xFFFFFFFFFFFFFFFFull);
}

TEST(Symbol, TrimsTrailingPadding) {
    const Symbol aapl = Symbol::from("AAPL");
    EXPECT_EQ(aapl.view(), "AAPL");
    EXPECT_EQ(aapl.chars[4], ' ');
    EXPECT_EQ(Symbol::from("").view(), "");
    EXPECT_EQ(Symbol::from("ABCDEFGHIJ").view(), "ABCDEFGH");  // truncated to 8
    EXPECT_EQ(Symbol{}.view(), "");                            // NUL-filled default is "no symbol"
}

TEST(Format, PriceHasFourImpliedDecimals) {
    EXPECT_EQ(format_price(1872500), "187.2500");
    EXPECT_EQ(format_price(5), "0.0005");
    EXPECT_EQ(format_price(0), "0.0000");
    EXPECT_DOUBLE_EQ(price_to_double(1872500), 187.25);
}

TEST(Format, TimestampIsNanosSinceMidnight) {
    EXPECT_EQ(format_timestamp(0), "00:00:00.000000000");
    EXPECT_EQ(
        format_timestamp(9ull * 3600 * kNanosPerSecond + 30 * 60 * kNanosPerSecond + 123456789),
        "09:30:00.123456789");
    EXPECT_EQ(format_time_hms(16ull * 3600 * kNanosPerSecond), "16:00:00");
}

TEST(Format, ThousandsSeparators) {
    EXPECT_EQ(format_count(0), "0");
    EXPECT_EQ(format_count(999), "999");
    EXPECT_EQ(format_count(1000), "1,000");
    EXPECT_EQ(format_count(12554295), "12,554,295");
}

TEST(Format, ParsesTimeOfDay) {
    Timestamp ts = 0;
    ASSERT_TRUE(parse_time_of_day("09:30:00", ts));
    EXPECT_EQ(ts, 9ull * 3600 * kNanosPerSecond + 30 * 60 * kNanosPerSecond);
    ASSERT_TRUE(parse_time_of_day("16:00", ts));
    EXPECT_EQ(ts, 16ull * 3600 * kNanosPerSecond);
    EXPECT_FALSE(parse_time_of_day("25:00:00", ts));
    EXPECT_FALSE(parse_time_of_day("abc", ts));
    EXPECT_FALSE(parse_time_of_day("9", ts));
}

TEST(LatencyHistogram, ExactPercentilesWithinRange) {
    LatencyHistogram hist(1000);
    for (std::uint64_t ns = 1; ns <= 100; ++ns) {
        hist.record(ns);
    }
    EXPECT_EQ(hist.count(), 100u);
    EXPECT_EQ(hist.percentile(0.50), 50u);
    EXPECT_EQ(hist.percentile(0.99), 99u);
    EXPECT_EQ(hist.percentile(1.0), 100u);
    EXPECT_EQ(hist.max(), 100u);
    EXPECT_DOUBLE_EQ(hist.mean(), 50.5);
}

TEST(LatencyHistogram, OverflowSamplesReportMax) {
    LatencyHistogram hist(10);
    hist.record(5);
    hist.record(5'000'000);
    EXPECT_EQ(hist.overflow(), 1u);
    EXPECT_EQ(hist.percentile(0.5), 5u);
    EXPECT_EQ(hist.percentile(1.0), 5'000'000u);
    hist.reset();
    EXPECT_EQ(hist.count(), 0u);
    EXPECT_EQ(hist.percentile(0.5), 0u);
}
