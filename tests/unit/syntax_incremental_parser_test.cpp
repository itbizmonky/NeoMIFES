#include <gtest/gtest.h>

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

using neomifes::syntax::applyTokenPatch;
using neomifes::syntax::IncrementalParser;
using neomifes::syntax::Language;
using neomifes::syntax::parseCpp;
using neomifes::syntax::parsePython;
using neomifes::syntax::parseRust;
using neomifes::syntax::ReparseEdit;
using neomifes::syntax::Token;
using neomifes::syntax::TokenKind;
using neomifes::syntax::TokenPatch;

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

// Phase 7q: drives the new reparseDelta()+applyTokenPatch() contract the
// same way render::SyntaxWorker::workerLoop() does - keeps its own
// persisted token list across calls, folding each returned TokenPatch into
// it via applyTokenPatch(). Lets the existing tests below keep the same
// "call once per edit, compare the returned tokens against a full reparse"
// shape the old (Phase 7k-7m) reparse() oracle pattern used, without every
// call site having to repeat the merge boilerplate. This is a deliberate,
// minimal re-implementation of SyntaxWorker's own merge step (not a shared
// helper) - if the two ever drift, render_syntax_worker_test.cpp's
// integration tests are what actually catches it, since they exercise the
// real SyntaxWorker/workerLoop() path end-to-end.
class ReparsingSession {
public:
    explicit ReparsingSession(Language language) : m_parser(language) {}

    [[nodiscard]] std::vector<Token> reparse(std::u16string_view text, std::span<const ReparseEdit> edits) {
        const TokenPatch patch = m_parser.reparseDelta(text, edits);
        m_tokens                = applyTokenPatch(std::move(m_tokens), patch);
        return m_tokens;
    }

private:
    IncrementalParser  m_parser;
    std::vector<Token> m_tokens;
};

// The core correctness guarantee this whole class exists for: an
// incrementally reparsed (and merged) result must be byte-for-byte
// identical to what a full reparse of the final text produces. Any
// divergence here means ts_tree_edit()'s inputs were computed wrong
// somewhere upstream (Document or the caller building ReparseEdit), or the
// TokenPatch/applyTokenPatch() merge itself is wrong - exactly the class of
// bug that would otherwise show up as silently-wrong syntax highlighting
// after an edit.

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

// Two independent, non-overlapping edits describing the SAME reparseDelta()
// call - mirrors SyntaxWorker accumulating multiple requestParse() calls
// into one batch before the worker picks them up (Phase 7l). Phase 7q
// merges the two edits' affected ranges into a single covering span (see
// incremental_parser.h's TokenPatch comment) rather than splicing two
// independent patches, so this also confirms that simplification still
// produces a byte-identical result to a full reparse even though it
// necessarily re-walks the (small, unrelated) text between editA and editC
// too.
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
// from reparseDelta()'s covering-subtree walk, which is a SEPARATE code path
// from a full parseRust() call that syntax_syntax_test.cpp's Rust comment
// test already covers - both needed the same fix, but only a test that
// exercises reparseDelta() with edits proves the incremental one actually
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

// Phase 7q: applyTokenPatch() boundary-condition tests, independent of
// IncrementalParser/tree-sitter - exercises the merge function itself
// directly against hand-built TokenPatch values.

TEST(ApplyTokenPatchTest, FullReplacementAgainstEmptyListYieldsReplacementTokensVerbatim) {
    // Mirrors the very first reparseDelta() call's patch shape (see
    // IncrementalParser::reparseDelta()'s "no tree retained" branch):
    // invalidatedRange spans the whole (empty, so far) persisted list.
    const std::vector<Token> replacement = {
        Token{.range = {.start = 0, .end = 3}, .kind = TokenKind::Type},
        Token{.range = {.start = 4, .end = 5}, .kind = TokenKind::Variable},
    };
    const TokenPatch patch{
        .invalidatedRange = {.start = 0, .end = 100}, .shiftAmount = 0, .replacementTokens = replacement};
    EXPECT_EQ(applyTokenPatch({}, patch), replacement);
}

TEST(ApplyTokenPatchTest, InvalidatingAPrefixKeepsTheSuffixUnshifted) {
    const std::vector<Token> existing = {
        Token{.range = {.start = 0, .end = 3}, .kind = TokenKind::Type},
        Token{.range = {.start = 4, .end = 5}, .kind = TokenKind::Variable},
        Token{.range = {.start = 10, .end = 11}, .kind = TokenKind::Number},
    };
    // Invalidate [0, 5) (the first two tokens), replace with one new token,
    // shiftAmount=0 since the edit didn't change the text's length.
    const std::vector<Token> replacement = {Token{.range = {.start = 0, .end = 4}, .kind = TokenKind::Type}};
    const TokenPatch          patch{
        .invalidatedRange = {.start = 0, .end = 5}, .shiftAmount = 0, .replacementTokens = replacement};
    const std::vector<Token> expected = {
        Token{.range = {.start = 0, .end = 4}, .kind = TokenKind::Type},
        Token{.range = {.start = 10, .end = 11}, .kind = TokenKind::Number},
    };
    EXPECT_EQ(applyTokenPatch(existing, patch), expected);
}

TEST(ApplyTokenPatchTest, InvalidatingASuffixKeepsThePrefixAndDropsNothingAfter) {
    const std::vector<Token> existing = {
        Token{.range = {.start = 0, .end = 3}, .kind = TokenKind::Type},
        Token{.range = {.start = 4, .end = 5}, .kind = TokenKind::Variable},
    };
    // Invalidate everything from offset 4 onward, append one new token.
    const std::vector<Token> replacement = {Token{.range = {.start = 4, .end = 6}, .kind = TokenKind::Number}};
    const TokenPatch          patch{
        .invalidatedRange = {.start = 4, .end = 6}, .shiftAmount = 1, .replacementTokens = replacement};
    const std::vector<Token> expected = {
        Token{.range = {.start = 0, .end = 3}, .kind = TokenKind::Type},
        Token{.range = {.start = 4, .end = 6}, .kind = TokenKind::Number},
    };
    EXPECT_EQ(applyTokenPatch(existing, patch), expected);
}

TEST(ApplyTokenPatchTest, PositiveShiftAmountMovesTokensAfterTheInvalidatedRangeForward) {
    const std::vector<Token> existing = {
        Token{.range = {.start = 0, .end = 1}, .kind = TokenKind::Type},   // untouched, before invalidated range
        Token{.range = {.start = 2, .end = 3}, .kind = TokenKind::Number},  // inside invalidated range, discarded
        Token{.range = {.start = 5, .end = 6}, .kind = TokenKind::Punctuation},  // after, must shift by +2
    };
    // invalidatedRange in FINAL coordinates: [2, 5) (2 code units grew to
    // 3+2=5 wide after a 2-code-unit insertion, shiftAmount=+2). PRE-edit
    // end = 5 - 2 = 3, matching the discarded token's own end.
    const std::vector<Token> replacement = {Token{.range = {.start = 2, .end = 5}, .kind = TokenKind::Number}};
    const TokenPatch          patch{
        .invalidatedRange = {.start = 2, .end = 5}, .shiftAmount = 2, .replacementTokens = replacement};
    const std::vector<Token> expected = {
        Token{.range = {.start = 0, .end = 1}, .kind = TokenKind::Type},
        Token{.range = {.start = 2, .end = 5}, .kind = TokenKind::Number},
        Token{.range = {.start = 7, .end = 8}, .kind = TokenKind::Punctuation},
    };
    EXPECT_EQ(applyTokenPatch(existing, patch), expected);
}

TEST(ApplyTokenPatchTest, NegativeShiftAmountMovesTokensAfterTheInvalidatedRangeBackward) {
    const std::vector<Token> existing = {
        Token{.range = {.start = 0, .end = 1}, .kind = TokenKind::Type},
        Token{.range = {.start = 2, .end = 6}, .kind = TokenKind::Number},  // discarded (deletion shrank this span)
        Token{.range = {.start = 8, .end = 9}, .kind = TokenKind::Punctuation},  // after, must shift by -2
    };
    // A 2-code-unit deletion: PRE-edit invalidated span [2,6), FINAL
    // coordinates [2,4) (shiftAmount=-2, so 4-(-2)=6 recovers the PRE-edit
    // end).
    const std::vector<Token> replacement = {Token{.range = {.start = 2, .end = 4}, .kind = TokenKind::Number}};
    const TokenPatch          patch{
        .invalidatedRange = {.start = 2, .end = 4}, .shiftAmount = -2, .replacementTokens = replacement};
    const std::vector<Token> expected = {
        Token{.range = {.start = 0, .end = 1}, .kind = TokenKind::Type},
        Token{.range = {.start = 2, .end = 4}, .kind = TokenKind::Number},
        Token{.range = {.start = 6, .end = 7}, .kind = TokenKind::Punctuation},
    };
    EXPECT_EQ(applyTokenPatch(existing, patch), expected);
}

TEST(ApplyTokenPatchTest, EmptyReplacementTokensRemovesTokensWithoutInsertingAnything) {
    // A deletion that removes an entire token (e.g. a comment deleted
    // outright) with nothing replacing it - replacementTokens can
    // legitimately be empty.
    const std::vector<Token> existing = {
        Token{.range = {.start = 0, .end = 1}, .kind = TokenKind::Type},
        Token{.range = {.start = 2, .end = 6}, .kind = TokenKind::Comment},
        Token{.range = {.start = 8, .end = 9}, .kind = TokenKind::Punctuation},
    };
    const TokenPatch patch{.invalidatedRange = {.start = 2, .end = 2}, .shiftAmount = -4, .replacementTokens = {}};
    const std::vector<Token> expected = {
        Token{.range = {.start = 0, .end = 1}, .kind = TokenKind::Type},
        Token{.range = {.start = 4, .end = 5}, .kind = TokenKind::Punctuation},
    };
    EXPECT_EQ(applyTokenPatch(existing, patch), expected);
}

}  // namespace
