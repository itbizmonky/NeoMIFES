#include <gtest/gtest.h>

#include "neomifes/document/document.h"

namespace {

using neomifes::document::Document;

TEST(LineIndexTest, EmptyDocumentHasOneLine) {
    Document doc;
    EXPECT_EQ(doc.lineCount(),         1u);
    EXPECT_EQ(doc.offsetToLine(0),     0u);
    EXPECT_EQ(doc.lineToOffset(0),     0u);
}

TEST(LineIndexTest, SingleLine) {
    Document doc;
    doc.insertText(0, u"hello world");
    EXPECT_EQ(doc.lineCount(),          1u);
    EXPECT_EQ(doc.offsetToLine(5),      0u);
    EXPECT_EQ(doc.offsetToLine(11),     0u);
    EXPECT_EQ(doc.lineToOffset(0),      0u);
    // Out-of-range line clamps to last line start.
    EXPECT_EQ(doc.lineToOffset(999),    0u);
}

TEST(LineIndexTest, MultipleLines) {
    Document doc;
    doc.insertText(0, u"aa\nbbb\ncccc");
    // Offsets: 0 'a' 1 'a' 2 '\n' 3 'b' 4 'b' 5 'b' 6 '\n' 7 'c' 8 'c' 9 'c' 10 'c'
    EXPECT_EQ(doc.lineCount(),      3u);
    EXPECT_EQ(doc.lineToOffset(0),  0u);
    EXPECT_EQ(doc.lineToOffset(1),  3u);
    EXPECT_EQ(doc.lineToOffset(2),  7u);
    EXPECT_EQ(doc.offsetToLine(0),  0u);
    EXPECT_EQ(doc.offsetToLine(2),  0u);
    EXPECT_EQ(doc.offsetToLine(3),  1u);
    EXPECT_EQ(doc.offsetToLine(6),  1u);
    EXPECT_EQ(doc.offsetToLine(7),  2u);
    EXPECT_EQ(doc.offsetToLine(10), 2u);
}

TEST(LineIndexTest, TrailingNewlineAddsEmptyLine) {
    Document doc;
    doc.insertText(0, u"abc\n");
    EXPECT_EQ(doc.lineCount(),     2u);
    EXPECT_EQ(doc.lineToOffset(1), 4u);
}

TEST(LineIndexTest, RebuildAfterMutation) {
    Document doc;
    doc.insertText(0, u"one\ntwo");
    EXPECT_EQ(doc.lineCount(), 2u);
    doc.insertText(doc.length(), u"\nthree");
    EXPECT_EQ(doc.lineCount(), 3u);
    EXPECT_EQ(doc.lineToOffset(2), 8u);  // "one\ntwo\n" == 8 chars
}

// Phase 7p: applyInsert()/applyErase() incremental-update regression tests.
// Each of these pins a boundary case that a naive shift/splice could get
// wrong, using build()-equivalent expectations as the oracle (Document has
// no way to force a full rebuild mid-sequence, so "what a fresh build()
// against the final text would produce" is computed by hand per case).

TEST(LineIndexTest, InsertAtDocumentEndWithoutNewlineTouchesNoExistingLineStarts) {
    Document doc;
    doc.insertText(0, u"aaa\nbbb");
    ASSERT_EQ(doc.lineCount(), 2u);
    // Repeated single-char appends at the end - the exact shape of
    // BM_UndoStack_PushOneMillion, which is what exposed the O(document
    // length)-per-call regression this fix addresses.
    for (int i = 0; i < 50; ++i) {
        doc.insertText(doc.length(), u"x");
    }
    EXPECT_EQ(doc.lineCount(), 2u);
    EXPECT_EQ(doc.lineToOffset(0), 0u);
    EXPECT_EQ(doc.lineToOffset(1), 4u);  // "aaa\n" is still 4 chars, unaffected by end-appends
    EXPECT_EQ(doc.offsetToLine(doc.length() - 1), 1u);
}

TEST(LineIndexTest, InsertAtDocumentStartShiftsEveryLaterLineStart) {
    Document doc;
    doc.insertText(0, u"one\ntwo\nthree");
    ASSERT_EQ(doc.lineCount(), 3u);
    doc.insertText(0, u"XY");
    EXPECT_EQ(doc.lineCount(), 3u);
    EXPECT_EQ(doc.lineToOffset(0), 0u);
    EXPECT_EQ(doc.lineToOffset(1), 6u);  // was 4 ("one\n"), shifted by +2
    EXPECT_EQ(doc.lineToOffset(2), 10u); // was 8 ("one\ntwo\n"), shifted by +2
}

TEST(LineIndexTest, InsertAtExistingLineStartKeepsInsertedTextOnThatLine) {
    Document doc;
    doc.insertText(0, u"aaa\nbbb");
    // Insert right at line 1's start offset (4) - the split-point boundary
    // case in applyInsert()'s upper_bound.
    doc.insertText(4, u"Z");
    EXPECT_EQ(doc.lineCount(), 2u);
    EXPECT_EQ(doc.lineToOffset(1), 4u);       // line 1 still starts at 4
    EXPECT_EQ(doc.offsetToLine(4), 1u);       // the new 'Z' belongs to line 1
    EXPECT_EQ(doc.offsetToLine(8), 1u);       // "aaa\nZbbb" - offset 8 is document end, still line 1
}

TEST(LineIndexTest, InsertWithMultipleNewlinesSplicesAllNewLineStarts) {
    Document doc;
    doc.insertText(0, u"aaa\nzzz");
    ASSERT_EQ(doc.lineCount(), 2u);
    // Insert a 3-newline chunk in the middle of line 0.
    doc.insertText(1, u"\n\n\n");
    // "a" + "\n\n\n" + "aa\nzzz" -> 5 lines total.
    EXPECT_EQ(doc.lineCount(), 5u);
    EXPECT_EQ(doc.lineToOffset(0), 0u);
    EXPECT_EQ(doc.lineToOffset(1), 2u);
    EXPECT_EQ(doc.lineToOffset(2), 3u);
    EXPECT_EQ(doc.lineToOffset(3), 4u);
    EXPECT_EQ(doc.lineToOffset(4), 7u);  // "a\n\n\naa\nzzz" -> line 4 ("zzz") starts at 7
}

TEST(LineIndexTest, EraseRemovesLineStartsWhollyWithinRangeAndShiftsTheRest) {
    Document doc;
    doc.insertText(0, u"aa\nbb\ncc\ndd");
    ASSERT_EQ(doc.lineCount(), 4u);
    // Erase "bb\ncc\n" (offsets 3..9) - removes lines 1 and 2 entirely,
    // merging what's left into line 0 / renumbering line 3 -> line 1.
    doc.eraseRange(neomifes::document::TextRange{.start = 3, .end = 9});
    EXPECT_EQ(doc.lineCount(), 2u);
    EXPECT_EQ(doc.lineToOffset(0), 0u);
    EXPECT_EQ(doc.lineToOffset(1), 3u);  // "aa\ndd" -> line 1 ("dd") starts at 3
}

TEST(LineIndexTest, EraseRangeEndingExactlyAtLineStartBoundary) {
    Document doc;
    doc.insertText(0, u"aa\nbb\ncc");
    ASSERT_EQ(doc.lineCount(), 3u);
    // Erase exactly "aa\n" (offsets 0..3) - range.end lands precisely on
    // line 1's start offset, the upper_bound boundary in applyErase().
    doc.eraseRange(neomifes::document::TextRange{.start = 0, .end = 3});
    EXPECT_EQ(doc.lineCount(), 2u);
    EXPECT_EQ(doc.lineToOffset(0), 0u);
    EXPECT_EQ(doc.lineToOffset(1), 3u);  // "bb\ncc" -> line 1 ("cc") starts at 3
}

TEST(LineIndexTest, ReplaceRangeSpanningLinesChangesLineCountCorrectly) {
    Document doc;
    doc.insertText(0, u"aa\nbb\ncc");
    ASSERT_EQ(doc.lineCount(), 3u);
    // Replace "bb\n" (one line-worth) with a 2-newline string - net +1 line.
    doc.replaceRange(neomifes::document::TextRange{.start = 3, .end = 6}, u"X\nY\n");
    EXPECT_EQ(doc.lineCount(), 4u);
    EXPECT_EQ(doc.lineToOffset(0), 0u);
    EXPECT_EQ(doc.lineToOffset(1), 3u);  // "aa\nX\nY\ncc" -> line 1 ("X") starts at 3
    EXPECT_EQ(doc.lineToOffset(2), 5u);  // line 2 ("Y") starts at 5
    EXPECT_EQ(doc.lineToOffset(3), 7u);  // line 3 ("cc") starts at 7
}

}  // namespace
