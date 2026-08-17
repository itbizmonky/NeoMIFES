#include <gtest/gtest.h>

#include <array>
#include <string>

#include "neomifes/document/document.h"
#include "neomifes/logmode/format_detection.h"

namespace {

using neomifes::document::Document;
using neomifes::logmode::detectLogPatternRule;
using neomifes::logmode::LogPatternRule;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

[[nodiscard]] Document makeRepeatedLineDoc(std::u16string_view line, int count) {
    std::u16string text;
    text.reserve(line.size() * static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        text += line;
    }
    return makeDoc(text);
}

TEST(FormatDetectionTest, DetectsRfc5424Syslog) {
    const Document doc =
        makeRepeatedLineDoc(u"<34>1 2003-10-11T22:14:15.003Z mymachine.example.com su 1234 ID47 - hello\n", 50);
    const auto rule = detectLogPatternRule(doc);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"rfc5424_syslog");
}

TEST(FormatDetectionTest, DetectsRfc3164Syslog) {
    const Document doc = makeRepeatedLineDoc(u"<34>Oct 11 22:14:15 mymachine su: something happened\n", 50);
    const auto     rule = detectLogPatternRule(doc);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"rfc3164_syslog");
}

TEST(FormatDetectionTest, DetectsApacheCombinedLogFormat) {
    const Document doc = makeRepeatedLineDoc(
        u"127.0.0.1 - - [10/Oct/2000:13:55:36 -0700] \"GET /apache_pb.gif HTTP/1.0\" 200 2326 "
        u"\"http://www.example.com/start.html\" \"Mozilla/4.08\"\n",
        50);
    const auto rule = detectLogPatternRule(doc);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"apache_nginx_clf");
}

TEST(FormatDetectionTest, DetectsGenericIso8601Level) {
    const Document doc = makeRepeatedLineDoc(u"2026-08-16 10:15:32.123 ERROR Something broke\n", 50);
    const auto     rule = detectLogPatternRule(doc);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"generic_iso8601_level");
}

TEST(FormatDetectionTest, ReturnsNulloptForUnrelatedText) {
    const Document doc = makeRepeatedLineDoc(u"the quick brown fox jumps over the lazy dog\n", 50);
    EXPECT_FALSE(detectLogPatternRule(doc).has_value());
}

TEST(FormatDetectionTest, ReturnsNulloptWhenMatchRatioIsBelowThreshold) {
    // 10 matching lines out of 40 total (25%) - below the 50% confidence
    // threshold, so this must not be misdetected as generic_iso8601_level.
    std::u16string text;
    for (int i = 0; i < 10; ++i) {
        text += u"2026-08-16 10:15:32.123 ERROR Something broke\n";
    }
    for (int i = 0; i < 30; ++i) {
        text += u"this line matches nothing\n";
    }
    const Document doc = makeDoc(text);
    EXPECT_FALSE(detectLogPatternRule(doc).has_value());
}

TEST(FormatDetectionTest, ReturnsNulloptForEmptyDocument) {
    const Document doc = makeDoc(u"");
    EXPECT_FALSE(detectLogPatternRule(doc).has_value());
}

TEST(FormatDetectionTest, WorksWhenDocumentIsShorterThanSampleLines) {
    // sampleLines defaults to 100, but the document only has 3 lines -
    // detectLogPatternRule() must clamp to the document's actual line
    // count rather than reading out of range.
    const Document doc = makeRepeatedLineDoc(u"2026-08-16 10:15:32.123 ERROR Something broke\n", 3);
    const auto     rule = detectLogPatternRule(doc);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"generic_iso8601_level");
}

TEST(FormatDetectionTest, RespectsExplicitSmallerSampleLines) {
    const Document doc = makeRepeatedLineDoc(u"2026-08-16 10:15:32.123 ERROR Something broke\n", 50);
    const auto     rule = detectLogPatternRule(doc, /*sampleLines=*/5);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"generic_iso8601_level");
}

// WI-14d: `candidates` lets a caller detect against a rule list that
// doesn't come from builtInLogPatterns() at all - e.g. a user-editable
// pattern file (log_pattern_file.h) combined into the auto-detect
// candidate list by normal_mode_wiring.cpp's "Log: Enable (Auto-Detect)"
// command.
TEST(FormatDetectionTest, DetectsAgainstAnExplicitCandidateListWithNoBuiltInRules) {
    const Document doc = makeRepeatedLineDoc(u"[app] level=ERROR msg=boom\n", 50);
    const LogPatternRule customRule{
        .id              = u"custom_app_format",
        .displayName     = u"Custom App Format",
        .pattern         = uR"(^\[app\] level=(?P<level>\w+) msg=.*$)",
        .timestampFormat = u"",
    };
    const std::array<LogPatternRule, 1> candidates{customRule};
    const auto rule = detectLogPatternRule(doc, 100, candidates);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"custom_app_format");
}

// A candidate list that omits the rule which would otherwise win under the
// default builtInLogPatterns() list must not fall back to the built-ins -
// `candidates` REPLACES the search space, it does not supplement it.
TEST(FormatDetectionTest, ExplicitCandidateListDoesNotFallBackToBuiltIns) {
    const Document doc = makeRepeatedLineDoc(u"2026-08-16 10:15:32.123 ERROR Something broke\n", 50);
    const LogPatternRule unrelatedRule{
        .id              = u"unrelated",
        .displayName     = u"Unrelated",
        .pattern         = u"^NEVER_MATCHES_ANYTHING_XYZ$",
        .timestampFormat = u"",
    };
    const std::array<LogPatternRule, 1> candidates{unrelatedRule};
    EXPECT_FALSE(detectLogPatternRule(doc, 100, candidates).has_value());
}

}  // namespace
