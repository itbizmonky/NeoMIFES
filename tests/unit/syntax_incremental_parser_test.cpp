#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/syntax/incremental_parser.h"
#include "neomifes/syntax/syntax.h"

namespace {

using neomifes::syntax::IncrementalParser;
using neomifes::syntax::Language;
using neomifes::syntax::parseCpp;
using neomifes::syntax::parsePython;
using neomifes::syntax::ReparseEdit;

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

// The core correctness guarantee this whole class exists for: an
// incrementally reparsed result must be byte-for-byte identical to what a
// full reparse of the final text produces. Any divergence here means
// ts_tree_edit()'s inputs were computed wrong somewhere upstream (Document
// or the caller building ReparseEdit) - exactly the class of bug that would
// otherwise show up as silently-wrong syntax highlighting after an edit.

TEST(SyntaxIncrementalParserTest, FirstReparseWithNoEditsMatchesFullParse) {
    IncrementalParser  parser(Language::Cpp);
    const std::u16string text = u"int x = 1;\n";
    EXPECT_EQ(parser.reparse(text, {}), parseCpp(text));
}

TEST(SyntaxIncrementalParserTest, SingleCharacterInsertMatchesFullReparseOfNewText) {
    IncrementalParser  parser(Language::Cpp);
    const std::u16string oldText = u"int x = 1;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"int x = 12;\n";  // '2' inserted right after '1' (pos 9)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 9, 9, 10);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, SingleCharacterDeleteMatchesFullReparseOfNewText) {
    IncrementalParser  parser(Language::Cpp);
    const std::u16string oldText = u"int x = 12;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"int x = 1;\n";  // '2' deleted at pos 9
    const ReparseEdit    edit    = buildEdit(oldText, newText, 9, 10, 9);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, MultiLineReplaceMatchesFullReparse) {
    IncrementalParser  parser(Language::Cpp);
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
    IncrementalParser  parser(Language::Cpp);
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
    // Three reparse() calls in a row on the SAME parser instance - proves
    // the retained tree stays internally consistent across repeated
    // ts_tree_edit() + reparse cycles, not just a single edit.
    IncrementalParser parser(Language::Cpp);

    const std::u16string text1 = u"int x = 1;\n";
    (void)parser.reparse(text1, {});

    const std::u16string text2 = u"int x = 12;\n";
    (void)parser.reparse(text2, std::array{buildEdit(text1, text2, 9, 9, 10)});

    const std::u16string text3 = u"int x = 123;\n";
    const auto            incremental = parser.reparse(text3, std::array{buildEdit(text2, text3, 10, 10, 11)});
    EXPECT_EQ(incremental, parseCpp(text3));
}

TEST(SyntaxIncrementalParserTest, PythonSingleCharacterInsertMatchesFullReparse) {
    IncrementalParser  parser(Language::Python);
    const std::u16string oldText = u"x = 1\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"x = 12\n";  // '2' inserted right after '1' (pos 5)
    const ReparseEdit    edit    = buildEdit(oldText, newText, 5, 5, 6);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parsePython(newText));
}

// Phase 7m: the tests below specifically stress the new
// ts_tree_get_changed_ranges()-based token splice (walkTreeIncremental()) -
// same "result must equal an independent full reparse" oracle as above, just
// aimed at edit shapes chosen to exercise its subtree-pruning/reuse logic
// rather than the tree-sitter edit bookkeeping the earlier tests cover.

TEST(SyntaxIncrementalParserTest, EditInMiddleOfLargerDocumentReusesSurroundingTokens) {
    IncrementalParser parser(Language::Cpp);
    const std::u16string oldText =
        u"int a = 1;\n"
        u"int b = 2;\n"
        u"int c = 3;\n"
        u"int d = 4;\n"
        u"int e = 5;\n";
    (void)parser.reparse(oldText, {});

    // Only the middle statement's literal changes - the surrounding
    // sibling statements are exactly the "unaffected subtree, reuse old
    // tokens" case walkTreeIncremental() is meant to prune.
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
    IncrementalParser parser(Language::Cpp);
    const std::u16string oldText = u"int x = 1;\nint y = 2;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"long x = 1;\nint y = 2;\n";  // "int" -> "long" at byte 0
    const ReparseEdit    edit    = buildEdit(oldText, newText, 0, 3, 4);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

TEST(SyntaxIncrementalParserTest, EditAtEndOfDocumentMatchesFullReparse) {
    IncrementalParser parser(Language::Cpp);
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
// characters, or walkTreeIncremental() would wrongly reuse the old (non-
// comment) tokens for text that is now inside the comment. This is the
// core test that the CHANGED-RANGES output, not the literal edit window, is
// what actually decides which old tokens get discarded.
TEST(SyntaxIncrementalParserTest, UnterminatedCommentInsertionExpandsChangedRangeBeyondLiteralEdit) {
    IncrementalParser parser(Language::Cpp);
    const std::u16string oldText = u"int a = 1;\nint b = 2;\nint c = 3;\n";
    (void)parser.reparse(oldText, {});

    const std::u16string newText = u"int a = 1;\n/*int b = 2;\nint c = 3;\n";
    const std::size_t    startPos = oldText.find(u"int b");
    const ReparseEdit    edit     = buildEdit(oldText, newText, startPos, startPos, startPos + 2);
    EXPECT_EQ(parser.reparse(newText, std::array{edit}), parseCpp(newText));
}

// Two independent, non-overlapping edits describing the SAME reparse() call
// - mirrors SyntaxWorker accumulating multiple requestParse() calls into
// one batch before the worker picks them up (Phase 7l).
TEST(SyntaxIncrementalParserTest, MultipleEditsInOneBatchAllApplyCorrectly) {
    IncrementalParser parser(Language::Cpp);
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
// to prove lastTokens produced by an INCREMENTAL splice (not just a full
// walkTree()) remains valid input for the NEXT incremental splice - the 2nd,
// 3rd, and 4th calls below all take the incremental path.
TEST(SyntaxIncrementalParserTest, FourSequentialIncrementalReparsesStayConsistent) {
    IncrementalParser parser(Language::Cpp);

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
    IncrementalParser parser(Language::Python);
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

}  // namespace
