#include <gtest/gtest.h>

#include <vector>

#include "neomifes/logmode/log_grouping.h"
#include "neomifes/logmode/log_pattern.h"

namespace {

using neomifes::document::LineNumber;
using neomifes::logmode::computeGroupedLogLevels;
using neomifes::logmode::LogLevel;
using neomifes::logmode::LogLine;

// line/level/matched only - timestamp is irrelevant to grouping, left at its
// default (nullopt). Same helper shape as logmode_log_navigation_test.cpp.
[[nodiscard]] LogLine makeLine(LineNumber line, LogLevel level, bool matched) {
    LogLine result;
    result.line    = line;
    result.level   = level;
    result.matched = matched;
    return result;
}

TEST(LogGroupingTest, ContinuationLinesInheritTheGroupLeadersLevel) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Error, true),
        makeLine(1, LogLevel::Unknown, false),  // "java.lang.RuntimeException: boom"
        makeLine(2, LogLevel::Unknown, false),  // "\tat com.example.Foo.bar(Foo.java:42)"
    };
    const auto result = computeGroupedLogLevels(lines);
    ASSERT_EQ(result.size(), 3U);
    EXPECT_EQ(result[0], LogLevel::Error);
    EXPECT_EQ(result[1], LogLevel::Error);
    EXPECT_EQ(result[2], LogLevel::Error);
}

TEST(LogGroupingTest, LinesBeforeTheFirstMatchedLineStayUnknown) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Unknown, false),  // log file banner/preamble
        makeLine(1, LogLevel::Unknown, false),
        makeLine(2, LogLevel::Info, true),
    };
    const auto result = computeGroupedLogLevels(lines);
    ASSERT_EQ(result.size(), 3U);
    EXPECT_EQ(result[0], LogLevel::Unknown);
    EXPECT_EQ(result[1], LogLevel::Unknown);
    EXPECT_EQ(result[2], LogLevel::Info);
}

TEST(LogGroupingTest, AdjacentGroupsDoNotBleedIntoEachOther) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Warning, true),
        makeLine(1, LogLevel::Unknown, false),  // belongs to the Warning group
        makeLine(2, LogLevel::Error, true),
        makeLine(3, LogLevel::Unknown, false),  // belongs to the Error group, not Warning
    };
    const auto result = computeGroupedLogLevels(lines);
    ASSERT_EQ(result.size(), 4U);
    EXPECT_EQ(result[0], LogLevel::Warning);
    EXPECT_EQ(result[1], LogLevel::Warning);
    EXPECT_EQ(result[2], LogLevel::Error);
    EXPECT_EQ(result[3], LogLevel::Error);
}

TEST(LogGroupingTest, DocumentWithNoMatchedLinesStaysEntirelyUnknown) {
    const std::vector<LogLine> lines = {
        makeLine(0, LogLevel::Unknown, false),
        makeLine(1, LogLevel::Unknown, false),
    };
    const auto result = computeGroupedLogLevels(lines);
    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0], LogLevel::Unknown);
    EXPECT_EQ(result[1], LogLevel::Unknown);
}

TEST(LogGroupingTest, EmptySpanReturnsEmptyResult) {
    EXPECT_TRUE(computeGroupedLogLevels({}).empty());
}

}  // namespace
