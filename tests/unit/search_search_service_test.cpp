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

TEST(SearchServiceTest, MatchDoesNotCrossLineBoundary) {
    // Phase 5a scope: the pattern spans what would be a '\n' in the
    // document, so it must not be found even though "foo" and "bar" each
    // individually appear.
    const Document doc = makeDoc(u"foo\nbar");
    const Query literalQuery{.pattern = u"foobar", .regex = false};
    const Query regexQuery{.pattern = u"foo.bar", .regex = true};

    EXPECT_TRUE(SearchService::findAll(doc, literalQuery).empty());
    EXPECT_TRUE(SearchService::findAll(doc, regexQuery).empty());
}

TEST(SearchServiceTest, DocumentWithEmptyLinesDoesNotCrash) {
    const Document doc = makeDoc(u"foo\n\n\nbar");
    const Query query{.pattern = u"o", .regex = false};

    const std::vector<TextRange> result = ranges(SearchService::findAll(doc, query));
    ASSERT_EQ(result.size(), 2U);  // both 'o' in "foo", empty lines skipped without error
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

}  // namespace
