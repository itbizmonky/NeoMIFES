#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/syntax/incremental_parser.h"
#include "neomifes/syntax/syntax.h"

namespace {

using neomifes::syntax::IncrementalParser;
using neomifes::syntax::Language;
using neomifes::syntax::parseCpp;
using neomifes::syntax::parseIni;
using neomifes::syntax::parsePython;
using neomifes::syntax::parseRust;
using neomifes::syntax::parseSql;
using neomifes::syntax::parseTypeScript;
using neomifes::syntax::parseYaml;
using neomifes::syntax::ReparseEdit;
using neomifes::syntax::Token;
using neomifes::syntax::TokenKind;

// Test-only helpers mirroring what document::Document computes internally
// (see document.cpp's insertText()/eraseRange()/replaceRange(), Phase 7k) -
// row is the 0-based line number containing `pos`, column is the UTF-16
// code-unit offset into that line * 2 (this module's byte-per-code-unit
// convention, see incremental_parser.h's ReparseEdit comment).
[[nodiscard]] std::uint32_t rowAt(std::u16string_view text, std::size_t pos) {
    std::uint32_t row = 0;
    for (std::size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i] == u'\n') {
            ++row;
        }
    }
    return row;
}

[[nodiscard]] std::uint32_t columnAt(std::u16string_view text, std::size_t pos) {
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i] == u'\n') {
            lineStart = i + 1;
        }
    }
    return static_cast<std::uint32_t>((pos - lineStart) * 2);
}

// Builds the edit describing "replace oldText[startPos, oldEndPos) with
// newText[startPos, newEndPos)" - both texts are assumed to share the same
// prefix up to startPos, matching how a real single Document mutation
// behaves.
[[nodiscard]] ReparseEdit buildEdit(std::u16string_view oldText, std::u16string_view newText, std::size_t startPos,
                                    std::size_t oldEndPos, std::size_t newEndPos) {
    return ReparseEdit{
        .startByte    = static_cast<std::uint32_t>(startPos * 2),
        .oldEndByte   = static_cast<std::uint32_t>(oldEndPos * 2),
        .newEndByte   = static_cast<std::uint32_t>(newEndPos * 2),
        .startRow     = rowAt(oldText, startPos),
        .startColumn  = columnAt(oldText, startPos),
        .oldEndRow    = rowAt(oldText, oldEndPos),
        .oldEndColumn = columnAt(oldText, oldEndPos),
        .newEndRow    = rowAt(newText, newEndPos),
        .newEndColumn = columnAt(newText, newEndPos),
    };
}

// Phase 7t: thin wrapper keeping the same "call once per edit, compare the
// returned tokens against a full reparse" shape the pre-7t tests already
// used, without every call site having to spell out a byte range. reparse()
// always requests the FULL text as its range - reparseRange()'s own
// contract ("returns AT LEAST the requested range") then reduces to
// "returns everything", so these tests continue to exercise the same
// ts_tree_edit()-accumulation correctness they always have. reparseRange()
// below exposes the underlying ranged contract directly for tests that
// specifically exercise partial-range requests.
class ReparsingSession {
public:
    explicit ReparsingSession(Language language) : m_parser(language) {}

    [[nodiscard]] std::vector<Token> reparse(std::u16string_view text, std::span<const ReparseEdit> edits) {
        return m_parser.reparseRange(text, edits, 0, static_cast<std::uint32_t>(text.size() * 2));
    }

    [[nodiscard]] std::vector<Token> reparseRange(std::u16string_view text, std::span<const ReparseEdit> edits,
                                                   std::uint32_t rangeStartByte, std::uint32_t rangeEndByte) {
        return m_parser.reparseRange(text, edits, rangeStartByte, rangeEndByte);
    }

private:
    IncrementalParser m_parser;
};

// The core correctness guarantee this whole class exists for: an
// incrementally reparsed result (requesting the FULL text as the range, via
// ReparsingSession::reparse() above) must be byte-for-byte identical to what
// a full reparse of the final text produces. Any divergence here means
// ts_tree_edit()'s inputs were computed wrong somewhere upstream (Document
// or the caller building ReparseEdit), or reparseRange()'s own tree-sitter
// bookkeeping is wrong - exactly the class of bug that would otherwise show
// up as silently-wrong syntax highlighting after an edit.

TEST(SyntaxIncrementalParserTest, FirstReparseWithNoEditsMatchesFullParse) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string text = u"int x = 1;\n";
    EXPECT_EQ(parser.reparse(text, {}), parseCpp(text));
}

TEST(SyntaxIncrementalParserTest, SingleCharacterInsertMatchesFullReparseOfNewText) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText = u"int x = 1;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"int x = 12;\n";  // '2' inserted right after '1' (pos 9)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 9, 9, 10);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, SingleCharacterDeleteMatchesFullReparseOfNewText) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText = u"int x = 12;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"int x = 1;\n";  // '2' deleted at pos 9
    const ReparseEdit    edit    = buildEdit(oldText, newText, 9, 10, 9);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, MultiLineReplaceMatchesFullReparse) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText = u"int x = 1;\nint y = 2;\n";
    (void)parser.reparse(oldText, {});

    // Replace "int y = 2;" (the second line's body, pos 11..21) with
    // "int y = 200;" - the edit's old/new end lands on the same row it
    // started on, so this also exercises a same-line-but-not-single-char
    // edit shape, distinct from the single-character tests above.
    const std::u16string newText = u"int x = 1;\nint y = 200;\n";
    const ReparseEdit    edit    = buildEdit(oldText, newText, 11, 21, 23);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, InsertingNewlineMatchesFullReparse) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText = u"int x = 1; int y = 2;\n";
    (void)parser.reparse(oldText, {});

    // Split the single line into two by replacing the separating space
    // (pos 10) with '\n' - exercises an edit whose newEndRow differs from
    // its startRow despite touching only one character. (Not a pure
    // insert: the space at pos 10 is consumed, not kept alongside the
    // newline - oldEndPos must include it or the edit doesn't match the
    // actual oldText/newText diff.)
    const std::u16string newText = u"int x = 1;\nint y = 2;\n";
    const ReparseEdit    edit    = buildEdit(oldText, newText, 10, 11, 11);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, SequentialEditsAccumulateCorrectly) {
    // Three reparse() calls in a row on the SAME session - proves the
    // retained tree AND the persisted token list stay internally consistent
    // across repeated ts_tree_edit()+reparse+merge cycles, not just a
    // single edit.
    ReparsingSession parser(Language::Cpp);

    const std::u16string text1 = u"int x = 1;\n";
    (void)parser.reparse(text1, {});

    const std::u16string text2 = u"int x = 12;\n";
    (void)parser.reparse(text2, std::array{buildEdit(text1, text2, 9, 9, 10)});

    const std::u16string text3 = u"int x = 123;\n";
    const auto            incremental = parser.reparse(text3, std::array{buildEdit(text2, text3, 10, 10, 11)});
    EXPECT_EQ(incremental, parseCpp(text3));
}

TEST(SyntaxIncrementalParserTest, PythonSingleCharacterInsertMatchesFullReparse) {
    ReparsingSession      parser(Language::Python);
    const std::u16string oldText = u"x = 1\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"x = 12\n";  // '2' inserted right after '1' (pos 5)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 5, 5, 6);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parsePython(newText));
}

// Phase 7m/7q: the tests below specifically stress the changed-range subtree
// lookup (ts_node_descendant_for_byte_range(), Phase 7q) and its predecessor
// walkTreeIncremental() (Phase 7m) - same "result must equal an independent
// full reparse" oracle as above, just aimed at edit shapes chosen to
// exercise subtree-pruning/reuse logic rather than the tree-sitter edit
// bookkeeping the earlier tests cover.

TEST(SyntaxIncrementalParserTest, EditInMiddleOfLargerDocumentReusesSurroundingTokens) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText =
        u"int a = 1;\n"
        u"int b = 2;\n"
        u"int c = 3;\n"
        u"int d = 4;\n"
        u"int e = 5;\n";
    (void)parser.reparse(oldText, {});

    // Only the middle statement's literal changes - the surrounding
    // sibling statements are exactly the "unaffected subtree, reuse old
    // tokens" case the changed-range subtree lookup is meant to prune.
    const std::u16string newText =
        u"int a = 1;\n"
        u"int b = 2;\n"
        u"int c = 300;\n"
        u"int d = 4;\n"
        u"int e = 5;\n";
    const std::size_t startPos  = oldText.find(u"3;");
    const std::size_t oldEndPos = startPos + 1;  // just the '3'
    const std::size_t newEndPos = startPos + 3;  // '300'
    const ReparseEdit edit      = buildEdit(oldText, newText, startPos, oldEndPos, newEndPos);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, EditAtStartOfDocumentMatchesFullReparse) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText = u"int x = 1;\nint y = 2;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"long x = 1;\nint y = 2;\n";  // "int" -> "long" at byte 0
    const ReparseEdit    edit    = buildEdit(oldText, newText, 0, 3, 4);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, EditAtEndOfDocumentMatchesFullReparse) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText = u"int x = 1;\nint y = 2;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"int x = 1;\nint y = 200;\n";  // change right before EOF
    const std::size_t    startPos  = oldText.find(u"2;");
    const std::size_t    oldEndPos = startPos + 1;
    const std::size_t    newEndPos = startPos + 3;
    const ReparseEdit     edit      = buildEdit(oldText, newText, startPos, oldEndPos, newEndPos);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

// Inserting "/*" makes everything until EOF (no matching "*/") part of a
// single comment node in the new tree - ts_tree_get_changed_ranges() must
// flag that whole trailing region as changed, not just the two inserted
// characters, or the covering-node lookup would wrongly stop short and
// reuse old (non-comment) tokens for text that is now inside the comment.
// This is the core test that the CHANGED-RANGES output, not the literal
// edit window, is what actually decides how much gets re-walked/discarded.
TEST(SyntaxIncrementalParserTest, UnterminatedCommentInsertionExpandsChangedRangeBeyondLiteralEdit) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText = u"int a = 1;\nint b = 2;\nint c = 3;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"int a = 1;\n/*int b = 2;\nint c = 3;\n";
    const std::size_t    startPos = oldText.find(u"int b");
    const ReparseEdit    edit     = buildEdit(oldText, newText, startPos, startPos, startPos + 2);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

// Two independent, non-overlapping edits describing the SAME reparseRange()
// call - mirrors SyntaxWorker accumulating multiple requestParse() calls
// into one batch before the worker picks them up (Phase 7l). Confirms
// ts_tree_edit() is applied correctly for EACH edit in the batch, in order,
// before reparsing - a single incorrectly-applied edit here would desync
// the retained tree's byte offsets and show up as a divergence from the
// full-reparse oracle.
TEST(SyntaxIncrementalParserTest, MultipleEditsInOneBatchAllApplyCorrectly) {
    ReparsingSession      parser(Language::Cpp);
    const std::u16string oldText =
        u"int a = 1;\n"
        u"int b = 2;\n"
        u"int c = 3;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string midText =
        u"int a = 100;\n"
        u"int b = 2;\n"
        u"int c = 3;\n";
    const std::size_t aPos  = oldText.find(u"1;");
    const ReparseEdit editA = buildEdit(oldText, midText, aPos, aPos + 1, aPos + 3);

    const std::u16string newText =
        u"int a = 100;\n"
        u"int b = 2;\n"
        u"int c = 300;\n";
    const std::size_t cPos  = midText.find(u"3;");
    const ReparseEdit editC = buildEdit(midText, newText, cPos, cPos + 1, cPos + 3);

    EXPECT_EQ(parser.reparse(newText, std::array{editA, editC}), parseCpp(newText));
}

// Extends SequentialEditsAccumulateCorrectly (3 calls) to a 4th, specifically
// to prove a persisted token list produced by an INCREMENTAL merge (not just
// the first call's full-parse patch) remains valid input for the NEXT
// incremental merge - the 2nd, 3rd, and 4th calls below all take the
// incremental path.
TEST(SyntaxIncrementalParserTest, FourSequentialIncrementalReparsesStayConsistent) {
    ReparsingSession parser(Language::Cpp);

    const std::u16string text1 = u"int x = 1;\nint y = 2;\n";
    (void)parser.reparse(text1, {});

    const std::u16string text2 = u"int x = 10;\nint y = 2;\n";
    const std::size_t    p2    = text1.find(u"1;");
    (void)parser.reparse(text2, std::array{buildEdit(text1, text2, p2, p2 + 1, p2 + 2)});  // "1" -> "10"

    const std::u16string text3 = u"int x = 10;\nint y = 20;\n";
    const std::size_t    p3    = text2.find(u"2;");
    (void)parser.reparse(text3, std::array{buildEdit(text2, text3, p3, p3 + 1, p3 + 2)});  // "2" -> "20"

    const std::u16string text4 = u"int x = 100;\nint y = 20;\n";
    const std::size_t    p4    = text3.find(u"10;");
    const auto incremental = parser.reparse(text4, std::array{buildEdit(text3, text4, p4, p4 + 2, p4 + 3)});
    EXPECT_EQ(incremental, parseCpp(text4));
}

TEST(SyntaxIncrementalParserTest, PythonEditInMiddleOfLargerDocumentReusesSurroundingTokens) {
    ReparsingSession      parser(Language::Python);
    const std::u16string oldText =
        u"a = 1\n"
        u"b = 2\n"
        u"c = 3\n"
        u"d = 4\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText =
        u"a = 1\n"
        u"b = 200\n"
        u"c = 3\n"
        u"d = 4\n";
    const std::size_t startPos = oldText.find(u"2\n");
    const ReparseEdit  edit     = buildEdit(oldText, newText, startPos, startPos + 1, startPos + 3);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parsePython(newText));
}

// Phase 7n1: proves the incremental path is genuinely generic across
// languages added in this batch, not just Cpp/Python - Rust specifically
// exercises isAtomicNode()'s branch (line_comment is a non-leaf atomic node,
// see namedLeafKindsForRust()'s comment) inside detail::walkTree() as called
// from reparseRange()'s covering-node walk, which is a SEPARATE code path
// from a full parseRust() call that syntax_syntax_test.cpp's Rust comment
// test already covers - both needed the same fix, but only a test that
// exercises reparseRange() with edits proves the incremental one actually
// got it too.
TEST(SyntaxIncrementalParserTest, RustEditNearACommentReusesSurroundingTokensAndKeepsCommentAtomic) {
    ReparsingSession      parser(Language::Rust);
    const std::u16string oldText =
        u"// leading comment\n"
        u"fn main() {\n"
        u"    let x = 1;\n"
        u"}\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText =
        u"// leading comment\n"
        u"fn main() {\n"
        u"    let x = 100;\n"
        u"}\n";
    const std::size_t startPos = oldText.find(u"1;");
    const ReparseEdit edit     = buildEdit(oldText, newText, startPos, startPos + 1, startPos + 3);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseRust(newText));
}

// Phase 7r: proves the incremental path also works for one of this batch's
// new languages, not just Cpp/Python/Rust (Phase 7n1 precedent above) -
// reuses the same ReparsingSession/oracle pattern.
TEST(SyntaxIncrementalParserTest, YamlSingleCharacterInsertMatchesFullReparse) {
    ReparsingSession      parser(Language::Yaml);
    const std::u16string oldText = u"num: 42\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"num: 420\n";  // '0' inserted right after '2' (pos 7)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 7, 7, 8);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseYaml(newText));
}

// Phase 7s: proves the incremental path also works for one of this batch's
// new languages, not just Cpp/Python/Rust/Yaml (Phase 7n1/7r precedent
// above) - reuses the same ReparsingSession/oracle pattern.
TEST(SyntaxIncrementalParserTest, TypeScriptSingleCharacterInsertMatchesFullReparse) {
    ReparsingSession      parser(Language::TypeScript);
    const std::u16string oldText = u"const x = 1;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"const x = 12;\n";  // '2' inserted right after '1' (pos 11)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 11, 11, 12);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseTypeScript(newText));
}

// Phase 7x: proves the incremental path also works for one of this batch's
// new languages, not just Cpp/Python/Rust/Yaml/TypeScript (Phase 7n1/7r/7s
// precedent above) - reuses the same ReparsingSession/oracle pattern.
TEST(SyntaxIncrementalParserTest, IniSingleCharacterInsertMatchesFullReparse) {
    ReparsingSession      parser(Language::Ini);
    const std::u16string oldText = u"key=1\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"key=12\n";  // '2' inserted right after '1' (pos 5)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 5, 5, 6);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseIni(newText));
}

// Phase 7y: proves the incremental path also works for Sql, whose grammar is
// vendored rather than FetchContent'd (see third_party/tree-sitter-sql-
// generated/NOTICE.md) - reuses the same ReparsingSession/oracle pattern.
TEST(SyntaxIncrementalParserTest, SqlSingleCharacterInsertMatchesFullReparse) {
    ReparsingSession      parser(Language::Sql);
    const std::u16string oldText = u"SELECT id FROM t WHERE id = 1;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"SELECT id FROM t WHERE id = 12;\n";  // '2' inserted right after '1' (pos 29)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 29, 29, 30);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseSql(newText));
}

// Phase 7t: reparseRange()'s partial-range contract ("returns a sorted
// token list covering AT LEAST the requested [rangeStart, rangeEnd)") -
// exercised directly (no edits involved, just the range-narrowing behavior
// itself), independent of the ts_tree_edit()-accumulation tests above.

TEST(SyntaxIncrementalParserTest, NarrowRangeRequestReturnsASubsetOfTheFullParseCoveringTheRequestedSpan) {
    ReparsingSession      session(Language::Cpp);
    const std::u16string text =
        u"int a = 1;\n"
        u"int b = 2;\n"
        u"int c = 3;\n";
    const std::vector<Token> full = parseCpp(text);

    const std::size_t lineStart = text.find(u"int b");
    const std::size_t lineEnd   = text.find(u"int c");  // one past "int b = 2;\n"
    const auto         rangeStartByte = static_cast<std::uint32_t>(lineStart * 2);
    const auto         rangeEndByte   = static_cast<std::uint32_t>(lineEnd * 2);

    const std::vector<Token> narrow = session.reparseRange(text, {}, rangeStartByte, rangeEndByte);

    // Correctness: a narrower request must never fabricate or misclassify a
    // token relative to what a full reparse would produce for the same text.
    for (const Token& token : narrow) {
        EXPECT_NE(std::find(full.begin(), full.end(), token), full.end())
            << "narrow-range token not found in full parse: [" << token.range.start << "," << token.range.end
            << ")";
    }
    // Completeness ("at least covers the requested range"): every token that
    // a full reparse places entirely inside the requested line must be
    // present in the narrow result too.
    for (const Token& expected : full) {
        if (expected.range.start >= lineStart && expected.range.end <= lineEnd) {
            EXPECT_NE(std::find(narrow.begin(), narrow.end(), expected), narrow.end())
                << "expected token from the requested line missing: [" << expected.range.start << ","
                << expected.range.end << ")";
        }
    }
}

TEST(SyntaxIncrementalParserTest, RangeLandingInsideALeafStillReturnsThatLeafsFullToken) {
    ReparsingSession      session(Language::Cpp);
    const std::u16string text = u"int value = 100;\n";
    const std::vector<Token> full = parseCpp(text);

    // A zero-width range landing entirely INSIDE the "100" number literal -
    // ts_node_descendant_for_byte_range() must still resolve to (at least)
    // that whole leaf, not return nothing or a truncated fragment.
    const std::size_t numPos         = text.find(u"100");
    const auto         rangeStartByte = static_cast<std::uint32_t>((numPos + 1) * 2);
    const auto         rangeEndByte   = rangeStartByte;

    const std::vector<Token> narrow = session.reparseRange(text, {}, rangeStartByte, rangeEndByte);

    const auto numberToken =
        std::find_if(full.begin(), full.end(), [](const Token& t) { return t.kind == TokenKind::Number; });
    ASSERT_NE(numberToken, full.end());
    EXPECT_NE(std::find(narrow.begin(), narrow.end(), *numberToken), narrow.end());
}

}  // namespace
