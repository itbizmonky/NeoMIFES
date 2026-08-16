#include <gtest/gtest.h>

#include <chrono>
#include <optional>

#include "neomifes/logmode/timestamp_parser.h"

namespace {

using neomifes::logmode::parseTimestamp;
using neomifes::logmode::Timestamp;

struct Ymdhms {
    int  year;
    unsigned month;
    unsigned day;
    long hour;
    long minute;
    long second;
    long millis;
};

[[nodiscard]] Ymdhms decompose(Timestamp tp) {
    const auto dp  = std::chrono::floor<std::chrono::days>(tp);
    const std::chrono::year_month_day ymd{dp};
    const auto tod = std::chrono::hh_mm_ss{tp - dp};
    return Ymdhms{.year   = static_cast<int>(ymd.year()),
                  .month  = static_cast<unsigned>(ymd.month()),
                  .day    = static_cast<unsigned>(ymd.day()),
                  .hour   = tod.hours().count(),
                  .minute = tod.minutes().count(),
                  .second = static_cast<long>(tod.seconds().count()),
                  .millis = static_cast<long>(tod.subseconds().count())};
}

// RFC 5424: %FT%T%Ez, with the 'Z' (Zulu) normalization from
// parseTimestamp() itself, and the explicit-numeric-offset form.
TEST(TimestampParserTest, Rfc5424ZuluSuffixParsesAsUtc) {
    const auto result = parseTimestamp(u"2003-10-11T22:14:15.003Z", u"%FT%T%Ez");
    ASSERT_TRUE(result.has_value());
    const Ymdhms ymd = decompose(*result);
    EXPECT_EQ(ymd.year, 2003);
    EXPECT_EQ(ymd.month, 10U);
    EXPECT_EQ(ymd.day, 11U);
    EXPECT_EQ(ymd.hour, 22);
    EXPECT_EQ(ymd.minute, 14);
    EXPECT_EQ(ymd.second, 15);
    EXPECT_EQ(ymd.millis, 3);
}

TEST(TimestampParserTest, Rfc5424NumericOffsetParses) {
    const auto result = parseTimestamp(u"2003-10-11T22:14:15.003+02:00", u"%FT%T%Ez");
    ASSERT_TRUE(result.has_value());
    // +02:00 shifts the UTC instant 2 hours earlier than the Zulu case above.
    EXPECT_EQ(decompose(*result).hour, 20);
}

TEST(TimestampParserTest, Rfc5424WithoutFractionalSecondsParses) {
    const auto result = parseTimestamp(u"2003-10-11T22:14:15Z", u"%FT%T%Ez");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(decompose(*result).millis, 0);
}

// RFC 3164 (BSD syslog): no year field in the format itself - assumedYear
// must be supplied, and both single- and double-digit space-padded day
// forms must parse (the day field has no fixed width in the wire format).
TEST(TimestampParserTest, Rfc3164RequiresAssumedYearAndParsesDoubleDigitDay) {
    const auto result = parseTimestamp(u"Oct 11 22:14:15", u"%b %d %H:%M:%S", 2026);
    ASSERT_TRUE(result.has_value());
    const Ymdhms ymd = decompose(*result);
    EXPECT_EQ(ymd.year, 2026);
    EXPECT_EQ(ymd.month, 10U);
    EXPECT_EQ(ymd.day, 11U);
}

TEST(TimestampParserTest, Rfc3164ParsesSpacePaddedSingleDigitDay) {
    const auto result = parseTimestamp(u"Oct  1 22:14:15", u"%b %d %H:%M:%S", 2026);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(decompose(*result).day, 1U);
}

TEST(TimestampParserTest, Rfc3164WithoutAssumedYearFailsToResolve) {
    // sys_time cannot be constructed without a year - see this WI's
    // timestamp_parser.h header comment (finding 1).
    EXPECT_FALSE(parseTimestamp(u"Oct 11 22:14:15", u"%b %d %H:%M:%S").has_value());
}

// Apache/Nginx Common/Combined Log Format: %d/%b/%Y:%T %z
TEST(TimestampParserTest, ClfTimestampParses) {
    const auto result = parseTimestamp(u"10/Oct/2000:13:55:36 -0700", u"%d/%b/%Y:%T %z");
    ASSERT_TRUE(result.has_value());
    const Ymdhms ymd = decompose(*result);
    EXPECT_EQ(ymd.year, 2000);
    EXPECT_EQ(ymd.month, 10U);
    EXPECT_EQ(ymd.day, 10U);  // -0700 rolls 10/Oct 13:55 local back to 10/Oct 20:55 UTC
}

// Generic ISO-8601 + level: %Y-%m-%d %H:%M:%S, with millisecond fraction.
TEST(TimestampParserTest, GenericIso8601WithDotFractionParsesMilliseconds) {
    const auto result = parseTimestamp(u"2026-08-16 10:15:32.123", u"%Y-%m-%d %H:%M:%S");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(decompose(*result).millis, 123);
}

TEST(TimestampParserTest, GenericIso8601WithoutFractionParses) {
    const auto result = parseTimestamp(u"2026-08-16 10:15:32", u"%Y-%m-%d %H:%M:%S");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(decompose(*result).millis, 0);
}

// A comma decimal separator does not fail chrono::parse outright - it
// silently stops consuming at the comma, leaving ",123" in the stream.
// parseTimestamp()'s full-stream-consumption check must reject this
// (finding 3, timestamp_parser.h) rather than returning a silently
// truncated-to-whole-seconds timestamp.
TEST(TimestampParserTest, CommaFractionIsRejectedNotSilentlyTruncated) {
    EXPECT_FALSE(parseTimestamp(u"2026-08-16 10:15:32,123", u"%Y-%m-%d %H:%M:%S").has_value());
}

TEST(TimestampParserTest, TruncatedInputReturnsNullopt) {
    EXPECT_FALSE(parseTimestamp(u"2026-08-16 10:15", u"%Y-%m-%d %H:%M:%S").has_value());
}

TEST(TimestampParserTest, WrongSeparatorReturnsNullopt) {
    EXPECT_FALSE(parseTimestamp(u"2026/08/16 10:15:32", u"%Y-%m-%d %H:%M:%S").has_value());
}

TEST(TimestampParserTest, NonNumericInputReturnsNullopt) {
    EXPECT_FALSE(parseTimestamp(u"not-a-timestamp", u"%Y-%m-%d %H:%M:%S").has_value());
}

TEST(TimestampParserTest, EmptyTextReturnsNullopt) {
    EXPECT_FALSE(parseTimestamp(u"", u"%Y-%m-%d %H:%M:%S").has_value());
}

}  // namespace
