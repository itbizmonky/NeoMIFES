#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/search/search_service.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::search::Match;
using neomifes::search::Query;
using neomifes::search::SearchService;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

[[nodiscard]] std::vector<TextRange> ranges(const std::vector<Match>& matches) {
    std::vector<TextRange> out;
    out.reserve(matches.size());
    for (const Match& m : matches) {
        out.push_back(m.range);
    }
    return out;
}

TEST(SearchServiceTest, NoMatchReturnsEmptyVector) {
    const Document doc = makeDoc(u"hello world");
    const Query query{.pattern = u"xyz"};
    EXPECT_TRUE(SearchService::findAll(doc, query).empty());
}

TEST(SearchServiceTest, EmptyPatternReturnsEmptyVector) {
    const Document doc = makeDoc(u"hello world");
    const Query query{.pattern = u""};
    EXPECT_TRUE(SearchService::findAll(doc, query).empty());
}

TEST(SearchServiceTest, PlainTextCaseSensitiveFindsExactMatchesOnly) {
    const Document doc = makeDoc(u"Foo foo FOO");
    const Query query{.pattern = u"foo", .caseSensitive = true};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], (TextRange{.start = 4, .end = 7}));
}

TEST(SearchServiceTest, PlainTextCaseInsensitiveFindsAllVariants) {
    const Document doc = makeDoc(u"Foo foo FOO");
    const Query query{.pattern = u"foo", .caseSensitive = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 3U);
    EXPECT_EQ(result[0], (TextRange{.start = 0, .end = 3}));
    EXPECT_EQ(result[1], (TextRange{.start = 4, .end = 7}));
    EXPECT_EQ(result[2], (TextRange{.start = 8, .end = 11}));
}

TEST(SearchServiceTest, WholeWordOptionExcludesSubstringMatches) {
    const Document doc = makeDoc(u"cat catalog cat");
    const Query query{.pattern = u"cat", .caseSensitive = true, .wholeWord = true};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0], (TextRange{.start = 0, .end = 3}));
    EXPECT_EQ(result[1], (TextRange{.start = 12, .end = 15}));
}

TEST(SearchServiceTest, LiteralPatternIsNotInterpretedAsRegexSyntax) {
    // "." would match any char if treated as regex; as a literal query it
    // must only match an actual '.'.
    const Document doc = makeDoc(u"a.b axb");
    const Query query{.pattern = u"a.b", .regex = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], (TextRange{.start = 0, .end = 3}));
}

TEST(SearchServiceTest, RegexFindsAllPatternMatches) {
    const Document doc = makeDoc(u"a1 b22 c333");
    const Query query{.pattern = u"\\d+", .regex = true};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 3U);
    EXPECT_EQ(result[0], (TextRange{.start = 1, .end = 2}));
    EXPECT_EQ(result[1], (TextRange{.start = 4, .end = 6}));
    EXPECT_EQ(result[2], (TextRange{.start = 8, .end = 11}));
}

TEST(SearchServiceTest, InvalidRegexReturnsEmptyResultsWithoutThrowing) {
    const Document doc = makeDoc(u"hello world");
    const Query query{.pattern = u"(unclosed", .regex = true};

    EXPECT_NO_THROW({
        const std::vector<Match> result = SearchService::findAll(doc, query);
        EXPECT_TRUE(result.empty());
    });
}

TEST(SearchServiceTest, PathologicalRegexDoesNotHangThanksToRe2) {
    // RE2's linear-time guarantee (ADR-002) is exactly why this pattern,
    // which would blow up NFA backtracking engines against a long run of
    // 'a's with no trailing 'b', is safe to run synchronously here.
    const std::u16string longRun(2000, u'a');
    const Document doc = makeDoc(longRun);
    const Query query{.pattern = u"(a+)+b", .regex = true};

    EXPECT_TRUE(SearchService::findAll(doc, query).empty());
}

TEST(SearchServiceTest, JapaneseTextMatchPositionIsUtf16CodeUnitOffsetNotUtf8Byte) {
    // "あ"(0) "日"(1) "本"(2) "語"(3) "い"(4) - each 3 UTF-8 bytes, 1 UTF-16
    // code unit. A byte-offset bug would report positions far past index 5.
    const Document doc = makeDoc(u"あ日本語い");
    const Query query{.pattern = u"日本", .regex = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], (TextRange{.start = 1, .end = 3}));
}

TEST(SearchServiceTest, MatchesOnDifferentLinesGetCorrectAbsoluteOffsets) {
    // '\n' is at index 3, so line 1 ("foo") starts at index 4.
    const Document doc = makeDoc(u"foo\nfoo");
    const Query query{.pattern = u"foo", .regex = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0], (TextRange{.start = 0, .end = 3}));
    EXPECT_EQ(result[1], (TextRange{.start = 4, .end = 7}));
}

TEST(SearchServiceTest, LiteralQueryWithoutEmbeddedNewlineDoesNotSpanLines) {
    // A literal query with no '\n' in it can never match text that has a
    // '\n' in the middle, regardless of the Phase 5b1 whole-buffer scan -
    // "foobar" simply isn't a substring of "foo\nbar".
    const Document doc = makeDoc(u"foo\nbar");
    const Query query{.pattern = u"foobar", .regex = false};

    EXPECT_TRUE(SearchService::findAll(doc, query).empty());
}

TEST(SearchServiceTest, DotDoesNotMatchNewlineByDefault) {
    // RE2's dot_nl option is left at its default (false, see
    // search_service.h's Scope comment): "." does not implicitly cross a
    // line boundary even now that the whole document is one search buffer.
    // Cross-line matching requires an explicit '\n' or a char class like
    // "[\s\S]" - see the tests below.
    const Document doc = makeDoc(u"foo\nbar");
    const Query query{.pattern = u"foo.bar", .regex = true};

    EXPECT_TRUE(SearchService::findAll(doc, query).empty());
}

TEST(SearchServiceTest, LiteralQueryWithEmbeddedNewlineMatchesAcrossLines) {
    // Phase 5b1: findAll() now scans the whole document as one buffer, so a
    // query that itself contains '\n' can match text spanning a line break.
    const Document doc = makeDoc(u"foo\nbar\nbaz");
    const Query query{.pattern = u"bar\nbaz", .regex = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], (TextRange{.start = 4, .end = 11}));  // "bar\nbaz", offsets 4..11
}

TEST(SearchServiceTest, CharacterClassCanMatchAcrossLines) {
    // "[\s\S]" is the conventional RE2/PCRE idiom for "any character
    // including newline" without relying on the dot_nl option.
    const Document doc = makeDoc(u"foo\nbar");
    const Query query{.pattern = u"foo[\\s\\S]bar", .regex = true};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], (TextRange{.start = 0, .end = 7}));
}

TEST(SearchServiceTest, CaretAndDollarStillAnchorToLineNotWholeDocument) {
    // The (?m) prefix findAll() adds internally (search_service.cpp's
    // buildPattern()) must keep ^/$ meaning "start/end of line" even though
    // the whole document is now a single RE2 search buffer - this is the
    // exact behaviour Phase 5a's line-per-buffer scan gave for free.
    const Document doc = makeDoc(u"foo\nbar\nbaz");
    const Query startQuery{.pattern = u"^bar", .regex = true};
    const Query endQuery{.pattern = u"foo$", .regex = true};

    const std::vector<TextRange> startResult = ranges(SearchService::findAll(doc, startQuery));
    ASSERT_EQ(startResult.size(), 1U);
    EXPECT_EQ(startResult[0], (TextRange{.start = 4, .end = 7}));

    const std::vector<TextRange> endResult = ranges(SearchService::findAll(doc, endQuery));
    ASSERT_EQ(endResult.size(), 1U);
    EXPECT_EQ(endResult[0], (TextRange{.start = 0, .end = 3}));
}

TEST(SearchServiceTest, DocumentAnchorsStillTargetWholeDocumentDespiteMultilineFlag) {
    // \A/\z are unaffected by (?m) (RE2 syntax reference: "\A at beginning
    // of text", "\z at end of text") - the documented escape hatch for
    // whole-document anchoring now that ^/$ mean "line".
    const Document doc = makeDoc(u"foo\nbar\nbaz");
    const Query query{.pattern = u"\\Afoo", .regex = true};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], (TextRange{.start = 0, .end = 3}));

    // \Abar (bar is not at the true start of the document) must not match.
    const Query noMatchQuery{.pattern = u"\\Abar", .regex = true};
    EXPECT_TRUE(SearchService::findAll(doc, noMatchQuery).empty());
}

TEST(SearchServiceTest, DocumentWithEmptyLinesDoesNotCrash) {
    const Document doc = makeDoc(u"foo\n\n\nbar");
    const Query query{.pattern = u"o", .regex = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 2U);  // both 'o' in "foo"
    EXPECT_EQ(result[0], (TextRange{.start = 1, .end = 2}));
    EXPECT_EQ(result[1], (TextRange{.start = 2, .end = 3}));
}

TEST(SearchServiceTest, ZeroWidthRegexMatchDoesNotInfiniteLoop) {
    const Document doc = makeDoc(u"bbb");
    const Query query{.pattern = u"a*", .regex = true};

    // Zero-width match at every position (0..3 inclusive) since there is no 'a'.
    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 4U);
    for (const TextRange& r : result) {
        EXPECT_TRUE(r.empty());
    }
}

TEST(SearchServiceTest, ZeroWidthRegexMatchNearMultiByteCharacterDoesNotDuplicate) {
    // "あ"(UTF-16 offset 0, 3 UTF-8 bytes) + "b"(offset 1). A byte-granularity
    // (rather than codepoint-granularity) advance past a zero-length match
    // would land mid-sequence inside "あ"'s encoding and report offset 0
    // three times instead of once.
    const Document doc = makeDoc(u"あb");
    const Query query{.pattern = u"a*", .regex = true};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 3U);
    EXPECT_EQ(result[0], (TextRange{.start = 0, .end = 0}));
    EXPECT_EQ(result[1], (TextRange{.start = 1, .end = 1}));
    EXPECT_EQ(result[2], (TextRange{.start = 2, .end = 2}));
}

TEST(SearchServiceTest, EmptyLineMatchesZeroWidthPattern) {
    // "foo" (line 0, offsets 0-3) + '\n' (offset 3) + "" (line 1, empty,
    // starts at offset 4) + '\n' (offset 4) + "bar" (line 2, offsets 5-8).
    const Document doc = makeDoc(u"foo\n\nbar");
    const Query query{.pattern = u"^$", .regex = true};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], (TextRange{.start = 4, .end = 4}));
}

TEST(SearchServiceTest, EmptyLineDoesNotMatchNonEmptyPattern) {
    const Document doc = makeDoc(u"foo\n\nbar");
    const Query query{.pattern = u"o", .regex = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0], (TextRange{.start = 1, .end = 2}));
    EXPECT_EQ(result[1], (TextRange{.start = 2, .end = 3}));
}

}  // namespace
