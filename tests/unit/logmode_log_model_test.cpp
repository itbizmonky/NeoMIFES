#include <gtest/gtest.h>

#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/document/text_pos.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::logmode::builtInLogPatterns;
using neomifes::logmode::LogLevel;
using neomifes::logmode::LogModel;
using neomifes::logmode::LogPatternError;
using neomifes::logmode::LogPatternRule;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

[[nodiscard]] const LogPatternRule& ruleById(std::u16string_view id) {
    for (const LogPatternRule& rule : builtInLogPatterns()) {
        if (rule.id == id) {
            return rule;
        }
    }
    ADD_FAILURE() << "no built-in rule with that id";
    return builtInLogPatterns().front();
}

TEST(LogModelTest, EmptyDocumentProducesOneUnmatchedLine) {
    // Document::lineCount() is 1 (not 0) for an empty document (an empty
    // file has one empty line) - LogModel::build() must mirror that, not
    // silently special-case it away.
    const Document doc = makeDoc(u"");
    const auto     result = LogModel::build(doc, ruleById(u"generic_iso8601_level"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->lines().size(), 1U);
    EXPECT_FALSE(result->lines()[0].matched);
}

TEST(LogModelTest, LinesSizeAlwaysEqualsDocumentLineCount) {
    const Document doc = makeDoc(
        u"127.0.0.1 - - [10/Oct/2000:13:55:36 -0700] \"GET / HTTP/1.0\" 200 100\n"
        u"this line matches nothing\n"
        u"127.0.0.1 - - [10/Oct/2000:13:55:37 -0700] \"GET / HTTP/1.0\" 200 100\n");
    const auto result = LogModel::build(doc, ruleById(u"apache_nginx_clf"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->lines().size(), doc.lineCount());
    EXPECT_TRUE(result->lines()[0].matched);
    EXPECT_FALSE(result->lines()[1].matched);
    EXPECT_TRUE(result->lines()[2].matched);
}

TEST(LogModelTest, ApacheCombinedLogFormatMatchesWithUnknownLevel) {
    const Document doc = makeDoc(
        u"127.0.0.1 - - [10/Oct/2000:13:55:36 -0700] \"GET /apache_pb.gif HTTP/1.0\" 200 2326 "
        u"\"http://www.example.com/start.html\" \"Mozilla/4.08\"\n");
    const auto result = LogModel::build(doc, ruleById(u"apache_nginx_clf"));
    ASSERT_TRUE(result.has_value());
    // 2, not 1: a trailing '\n' produces an implicit empty final line (same
    // Document convention as the empty-document case above).
    ASSERT_EQ(result->lines().size(), 2U);
    EXPECT_TRUE(result->lines()[0].matched);
    // CLF has no textual severity field - status-code-to-level inference is
    // deliberately not implemented (see log_pattern.cpp's rule comment).
    EXPECT_EQ(result->lines()[0].level, LogLevel::Unknown);
    ASSERT_TRUE(result->lines()[0].timestamp.has_value());
    EXPECT_FALSE(result->lines()[1].matched);
}

TEST(LogModelTest, ApacheCommonLogFormatWithoutRefererMatches) {
    const Document doc = makeDoc(u"127.0.0.1 - - [10/Oct/2000:13:55:36 -0700] \"GET /x HTTP/1.0\" 200 100\n");
    const auto     result = LogModel::build(doc, ruleById(u"apache_nginx_clf"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->lines()[0].matched);
}

TEST(LogModelTest, Rfc5424SyslogMatchesTimestampWithUnknownLevel) {
    const Document doc = makeDoc(u"<34>1 2003-10-11T22:14:15.003Z mymachine.example.com su 1234 ID47 - hello\n");
    const auto     result = LogModel::build(doc, ruleById(u"rfc5424_syslog"));
    ASSERT_TRUE(result.has_value());
    // 2, not 1: a trailing '\n' produces an implicit empty final line.
    ASSERT_EQ(result->lines().size(), 2U);
    EXPECT_TRUE(result->lines()[0].matched);
    // Severity is numeric (inside <PRI>), not a textual "level" field.
    EXPECT_EQ(result->lines()[0].level, LogLevel::Unknown);
    ASSERT_TRUE(result->lines()[0].timestamp.has_value());
    EXPECT_FALSE(result->lines()[1].matched);
}

TEST(LogModelTest, Rfc5424SyslogWithNilStructuredDataAndNoMessageMatches) {
    // No message after the "-" (NILVALUE) structured-data field, only a
    // trailing '\r' (CRLF line ending) - see CrlfLineEndingDoesNotPreventMatch
    // below for why this specific shape is the one that actually proves the
    // trailing-\r trim matters for this rule.
    const Document doc = makeDoc(u"<34>1 2003-10-11T22:14:15.003Z host app 1234 ID47 -\r\n");
    const auto     result = LogModel::build(doc, ruleById(u"rfc5424_syslog"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->lines()[0].matched);
}

TEST(LogModelTest, Rfc3164SyslogMatchesWithAssumedYear) {
    const Document doc = makeDoc(u"<34>Oct 11 22:14:15 mymachine su: something happened\n");
    const auto     result = LogModel::build(doc, ruleById(u"rfc3164_syslog"), /*assumedYear=*/2026);
    ASSERT_TRUE(result.has_value());
    // 2, not 1: a trailing '\n' produces an implicit empty final line.
    ASSERT_EQ(result->lines().size(), 2U);
    EXPECT_TRUE(result->lines()[0].matched);
    EXPECT_EQ(result->lines()[0].level, LogLevel::Unknown);
    ASSERT_TRUE(result->lines()[0].timestamp.has_value());
    EXPECT_FALSE(result->lines()[1].matched);
}

TEST(LogModelTest, Rfc3164SyslogWithoutAssumedYearMatchesButLeavesTimestampNullopt) {
    // The regex still matches (the timestamp field's *shape* is present);
    // only the timestamp *parse* fails without a year - matched and
    // timestamp are independent signals.
    const Document doc = makeDoc(u"<34>Oct 11 22:14:15 mymachine su: something happened\n");
    const auto     result = LogModel::build(doc, ruleById(u"rfc3164_syslog"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->lines()[0].matched);
    EXPECT_FALSE(result->lines()[0].timestamp.has_value());
}

TEST(LogModelTest, GenericIso8601LevelDetectsErrorWarnInfo) {
    const Document doc = makeDoc(
        u"2026-08-16 10:15:32.123 ERROR Something broke\n"
        u"2026-08-16 10:15:33 WARN Config missing\n"
        u"2026-08-16 10:15:34 INFO App started\n");
    const auto result = LogModel::build(doc, ruleById(u"generic_iso8601_level"));
    ASSERT_TRUE(result.has_value());
    // 4, not 3: a trailing '\n' produces an implicit empty final line.
    ASSERT_EQ(result->lines().size(), 4U);
    EXPECT_EQ(result->lines()[0].level, LogLevel::Error);
    EXPECT_EQ(result->lines()[1].level, LogLevel::Warning);
    EXPECT_EQ(result->lines()[2].level, LogLevel::Info);
    // Flattened rather than looped: this project's established fix for
    // readability-function-cognitive-complexity triggered by a small
    // fixed-count loop of EXPECT_* calls is to unroll it (see TIMELINE.md).
    EXPECT_TRUE(result->lines()[0].matched);
    EXPECT_TRUE(result->lines()[0].timestamp.has_value());
    EXPECT_TRUE(result->lines()[1].matched);
    EXPECT_TRUE(result->lines()[1].timestamp.has_value());
    EXPECT_TRUE(result->lines()[2].matched);
    EXPECT_TRUE(result->lines()[2].timestamp.has_value());
    EXPECT_FALSE(result->lines()[3].matched);
}

TEST(LogModelTest, CrlfLineEndingDoesNotPreventMatch) {
    // Document::lineText() keeps the trailing '\r' as line content for a
    // CRLF-terminated line - LogModel::build() must trim it before
    // matching. The Rfc5424...NoMessageMatches case above is the one where
    // an untrimmed '\r' would actually break the match (it sits directly
    // after the "-" NILVALUE with no intervening space, so an unconsumed
    // '\r' would prevent '$' from aligning) - this test exercises the same
    // shape via a full document round trip (real CRLF insertion, not a
    // hand-built u16string) as an end-to-end confirmation.
    Document doc;
    doc.insertText(0, u"<34>1 2003-10-11T22:14:15.003Z host app 1234 ID47 -\r\n");
    const auto result = LogModel::build(doc, ruleById(u"rfc5424_syslog"));
    ASSERT_TRUE(result.has_value());
    ASSERT_GE(result->lines().size(), 1U);
    EXPECT_TRUE(result->lines()[0].matched);
}

TEST(LogModelTest, InvalidRegexRuleReturnsInvalidRegexError) {
    const Document doc = makeDoc(u"anything\n");
    const LogPatternRule broken{.id = u"broken", .displayName = u"broken", .pattern = u"(unclosed",
                          .timestampFormat = u"%Y-%m-%d"};
    const auto     result = LogModel::build(doc, broken);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), LogPatternError::InvalidRegex);
}

TEST(LogModelTest, EmptyLineDoesNotCrashAndIsUnmatched) {
    const Document doc = makeDoc(u"\n\n");
    const auto     result = LogModel::build(doc, ruleById(u"generic_iso8601_level"));
    ASSERT_TRUE(result.has_value());
    for (const auto& line : result->lines()) {
        EXPECT_FALSE(line.matched);
    }
}

TEST(LogModelTest, VeryLongSingleLineDoesNotCrash) {
    const std::u16string longLine(200000, u'x');
    const Document doc = makeDoc(longLine);
    const auto     result = LogModel::build(doc, ruleById(u"generic_iso8601_level"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->lines().size(), 1U);
    EXPECT_FALSE(result->lines()[0].matched);
}

TEST(LogModelTest, LineContentSpanningMultiplePiecesMatchesCorrectly) {
    // WI-14b: LogModel::build()'s piece-streaming rewrite accumulates each
    // line into a buffer across piece boundaries - this test forces the
    // Apache/Nginx CLF line below to actually span 3+ pieces (not just 1,
    // which every other test in this file exercises via makeDoc()'s single
    // insertText() call) and verifies matching still succeeds.
    const std::u16string line =
        u"127.0.0.1 - - [10/Oct/2000:13:55:36 -0700] \"GET /apache_pb.gif HTTP/1.0\" 200 2326 "
        u"\"http://www.example.com/start.html\" \"Mozilla/4.08\"\n";
    Document doc;
    doc.insertText(0, line);
    // Inserting then erasing inside the line splits the original single
    // piece into fragments - PieceTable::eraseRange() does not recombine
    // them, so the net text is unchanged but now spans multiple pieces.
    constexpr std::uint64_t kSplitPos = 50;  // inside "GET /apache_pb.gif..."
    doc.insertText(kSplitPos, u"XXXX");
    doc.eraseRange(TextRange{.start = kSplitPos, .end = kSplitPos + 4});

    // Precondition check: confirm this test actually exercises the
    // multi-piece path rather than silently degrading to a single piece.
    ASSERT_GT(doc.snapshot()->pieces().size(), 1U);

    const auto result = LogModel::build(doc, ruleById(u"apache_nginx_clf"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->lines().size(), doc.lineCount());
    EXPECT_TRUE(result->lines()[0].matched);
    EXPECT_EQ(result->lines()[0].level, LogLevel::Unknown);
    ASSERT_TRUE(result->lines()[0].timestamp.has_value());
}

TEST(LogModelTest, BuildFromSnapshotOverloadMatchesDocumentOverload) {
    // The Document-taking build() is a one-line delegation to the
    // BufferSnapshot-taking one (WI-14b 設計方針2) - confirm both entry
    // points produce identical results for the same document.
    const Document doc = makeDoc(
        u"2026-08-16 10:15:32.123 ERROR Something broke\n"
        u"this line matches nothing\n"
        u"2026-08-16 10:15:34 INFO App started\n");
    const LogPatternRule& rule = ruleById(u"generic_iso8601_level");

    const auto viaDocument = LogModel::build(doc, rule);
    const auto viaSnapshot = LogModel::build(*doc.snapshot(), rule);
    ASSERT_TRUE(viaDocument.has_value());
    ASSERT_TRUE(viaSnapshot.has_value());
    ASSERT_EQ(viaDocument->lines().size(), viaSnapshot->lines().size());
    for (std::size_t i = 0; i < viaDocument->lines().size(); ++i) {
        EXPECT_EQ(viaDocument->lines()[i].matched, viaSnapshot->lines()[i].matched);
        EXPECT_EQ(viaDocument->lines()[i].level, viaSnapshot->lines()[i].level);
        EXPECT_EQ(viaDocument->lines()[i].timestamp.has_value(), viaSnapshot->lines()[i].timestamp.has_value());
    }
}

}  // namespace
