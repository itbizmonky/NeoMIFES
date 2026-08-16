#include <gtest/gtest.h>

#include "neomifes/document/document.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"

namespace {

using neomifes::document::Document;
using neomifes::logmode::builtInLogPatterns;
using neomifes::logmode::LogLevel;
using neomifes::logmode::LogModel;
using neomifes::logmode::parseLevel;

TEST(LogPatternTest, BuiltInPatternsHaveExpectedIds) {
    const auto& patterns = builtInLogPatterns();
    ASSERT_EQ(patterns.size(), 4U);

    // Flattened rather than looped: a for-loop over EXPECT_FALSE here
    // previously tripped clang-tidy's readability-function-cognitive-complexity
    // (nesting penalty), and this project's established fix for that exact
    // shape is to unroll into flat statements (see TIMELINE.md).
    EXPECT_FALSE(patterns[0].id.empty());
    EXPECT_FALSE(patterns[0].displayName.empty());
    EXPECT_FALSE(patterns[0].pattern.empty());
    EXPECT_FALSE(patterns[0].timestampFormat.empty());
    EXPECT_FALSE(patterns[1].id.empty());
    EXPECT_FALSE(patterns[1].displayName.empty());
    EXPECT_FALSE(patterns[1].pattern.empty());
    EXPECT_FALSE(patterns[1].timestampFormat.empty());
    EXPECT_FALSE(patterns[2].id.empty());
    EXPECT_FALSE(patterns[2].displayName.empty());
    EXPECT_FALSE(patterns[2].pattern.empty());
    EXPECT_FALSE(patterns[2].timestampFormat.empty());
    EXPECT_FALSE(patterns[3].id.empty());
    EXPECT_FALSE(patterns[3].displayName.empty());
    EXPECT_FALSE(patterns[3].pattern.empty());
    EXPECT_FALSE(patterns[3].timestampFormat.empty());

    EXPECT_EQ(patterns[0].id, u"rfc5424_syslog");
    EXPECT_EQ(patterns[1].id, u"rfc3164_syslog");
    EXPECT_EQ(patterns[2].id, u"apache_nginx_clf");
    EXPECT_EQ(patterns[3].id, u"generic_iso8601_level");
}

// Regression guard against a typo in any built-in pattern's regex syntax:
// every rule must compile as valid RE2, independent of whether it actually
// matches anything. Goes through LogModel::build() (rather than touching
// re2::RE2 directly here) so this test file doesn't need its own RE2 link
// dependency - RE2 stays an implementation detail private to
// neomifes::logmode, mirroring neomifes::search's own PRIVATE re2::re2
// link (see src/logmode/CMakeLists.txt). A compile failure surfaces as
// LogPatternError::InvalidRegex; an empty document (0 lines to actually
// match) is enough to exercise compilation without needing real log text.
TEST(LogPatternTest, EveryBuiltInPatternCompilesAsValidRe2) {
    const Document doc;
    const auto& patterns = builtInLogPatterns();
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const auto result = LogModel::build(doc, patterns[i]);
        EXPECT_TRUE(result.has_value()) << "builtInLogPatterns()[" << i << "] failed to compile";
    }
}

TEST(LogPatternTest, ParseLevelRecognizesExactNames) {
    EXPECT_EQ(parseLevel(u"TRACE"), LogLevel::Trace);
    EXPECT_EQ(parseLevel(u"DEBUG"), LogLevel::Debug);
    EXPECT_EQ(parseLevel(u"INFO"), LogLevel::Info);
    EXPECT_EQ(parseLevel(u"WARN"), LogLevel::Warning);
    EXPECT_EQ(parseLevel(u"ERROR"), LogLevel::Error);
    EXPECT_EQ(parseLevel(u"FATAL"), LogLevel::Fatal);
}

TEST(LogPatternTest, ParseLevelRecognizesSynonymsCaseInsensitively) {
    EXPECT_EQ(parseLevel(u"warning"), LogLevel::Warning);
    EXPECT_EQ(parseLevel(u"Warning"), LogLevel::Warning);
    EXPECT_EQ(parseLevel(u"err"), LogLevel::Error);
    EXPECT_EQ(parseLevel(u"Err"), LogLevel::Error);
    EXPECT_EQ(parseLevel(u"dbg"), LogLevel::Debug);
    EXPECT_EQ(parseLevel(u"critical"), LogLevel::Fatal);
    EXPECT_EQ(parseLevel(u"CRITICAL"), LogLevel::Fatal);
    EXPECT_EQ(parseLevel(u"info"), LogLevel::Info);
    EXPECT_EQ(parseLevel(u"trace"), LogLevel::Trace);
}

TEST(LogPatternTest, ParseLevelReturnsUnknownForUnrecognizedOrEmptyText) {
    EXPECT_EQ(parseLevel(u""), LogLevel::Unknown);
    EXPECT_EQ(parseLevel(u"garbage"), LogLevel::Unknown);
    EXPECT_EQ(parseLevel(u"NOTICE"), LogLevel::Unknown);
}

}  // namespace
