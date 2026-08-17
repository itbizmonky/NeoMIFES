#include <gtest/gtest.h>

#include <vector>

#include "neomifes/logmode/log_navigation.h"
#include "neomifes/logmode/log_pattern.h"

namespace {

using neomifes::document::LineNumber;
using neomifes::logmode::kAllLogLevelsVisible;
using neomifes::logmode::LogLevel;
using neomifes::logmode::LogLine;
using neomifes::logmode::logLevelFilterBit;
using neomifes::logmode::nextVisibleLogLine;
using neomifes::logmode::previousVisibleLogLine;

// line/level/matched only - timestamp is irrelevant to navigation, left at
// its default (nullopt).
[[nodiscard]] LogLine makeLine(LineNumber line, LogLevel level, bool matched) {
    LogLine result;
    result.line    = line;
    result.level   = level;
    result.matched = matched;
    return result;
}

TEST(LogNavigationTest, NextVisibleLogLineFindsNearestMatchingLineAfterFrom) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Info, true),
        makeLine(2, LogLevel::Error, true),
        makeLine(3, LogLevel::Info, true),
    };
    const auto result = nextVisibleLogLine(lines, 0, kAllLogLevelsVisible);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1U);
}

TEST(LogNavigationTest, NextVisibleLogLineWrapsAroundWhenNoneFoundAfterFrom) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Error, true),
        makeLine(1, LogLevel::Info, true),
        makeLine(2, LogLevel::Info, true),
    };
    // Nothing qualifies at index 1 or 2 under an errors-only filter, so the
    // search wraps around and finds index 0 again.
    const auto result = nextVisibleLogLine(lines, 1, logLevelFilterBit(LogLevel::Error));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0U);
}

TEST(LogNavigationTest, NextVisibleLogLineSkipsUnmatchedLines) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Unknown, false),  // continuation line - never a jump target
        makeLine(2, LogLevel::Info, true),
    };
    const auto result = nextVisibleLogLine(lines, 0, kAllLogLevelsVisible);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2U);
}

TEST(LogNavigationTest, NextVisibleLogLineSkipsLevelsExcludedByFilter) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Warning, true),
        makeLine(2, LogLevel::Error, true),
    };
    const auto result = nextVisibleLogLine(lines, 0, logLevelFilterBit(LogLevel::Error));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2U);
}

TEST(LogNavigationTest, NextVisibleLogLineReturnsNulloptWhenNothingQualifies) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Info, true),
    };
    EXPECT_FALSE(nextVisibleLogLine(lines, 0, logLevelFilterBit(LogLevel::Error)).has_value());
}

TEST(LogNavigationTest, NextVisibleLogLineReturnsNulloptForEmptyLines) {
    EXPECT_FALSE(nextVisibleLogLine({}, 0, kAllLogLevelsVisible).has_value());
}

TEST(LogNavigationTest, PreviousVisibleLogLineFindsNearestMatchingLineBeforeFrom) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Error, true),
        makeLine(2, LogLevel::Info, true),
        makeLine(3, LogLevel::Info, true),
    };
    const auto result = previousVisibleLogLine(lines, 3, kAllLogLevelsVisible);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2U);
}

TEST(LogNavigationTest, PreviousVisibleLogLineWrapsAroundWhenNoneFoundBeforeFrom) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Info, true),
        makeLine(2, LogLevel::Error, true),
    };
    const auto result = previousVisibleLogLine(lines, 1, logLevelFilterBit(LogLevel::Error));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2U);
}

TEST(LogNavigationTest, PreviousVisibleLogLineSkipsUnmatchedLines) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Unknown, false),
        makeLine(2, LogLevel::Info, true),
    };
    const auto result = previousVisibleLogLine(lines, 2, kAllLogLevelsVisible);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0U);
}

TEST(LogNavigationTest, PreviousVisibleLogLineReturnsNulloptWhenNothingQualifies) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Info, true),
        makeLine(1, LogLevel::Info, true),
    };
    EXPECT_FALSE(previousVisibleLogLine(lines, 1, logLevelFilterBit(LogLevel::Error)).has_value());
}

TEST(LogNavigationTest, PreviousVisibleLogLineReturnsNulloptForEmptyLines) {
    EXPECT_FALSE(previousVisibleLogLine({}, 0, kAllLogLevelsVisible).has_value());
}

}  // namespace
