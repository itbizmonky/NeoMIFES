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
using neomifes::syntax::Token;

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

}  // namespace
