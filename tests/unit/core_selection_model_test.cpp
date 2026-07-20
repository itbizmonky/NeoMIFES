#include <gtest/gtest.h>

#include <vector>

#include "neomifes/core/cursor.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::Cursor;
using neomifes::core::MovementKind;
using neomifes::core::moveTextPos;
using neomifes::core::SelectionModel;
using neomifes::document::Document;

TEST(SelectionModelTest, StartsWithOnePrimaryCursorAtInitialPosition) {
    const SelectionModel model(5);
    ASSERT_EQ(model.cursors().size(), 1U);
    EXPECT_EQ(model.cursors()[0].position, 5U);
    EXPECT_EQ(model.cursors()[0].anchor, 5U);
    EXPECT_TRUE(model.cursors()[0].isPrimary);
}

TEST(SelectionModelTest, LeftRightClampAtDocumentBounds) {
    Document doc;
    doc.insertText(0, u"abc");
    SelectionModel model(0);

    model.moveAll(MovementKind::Left, doc, /*extendSelection=*/false);
    EXPECT_EQ(model.primaryCursor().position, 0U);  // clamped, already at start

    model.moveAll(MovementKind::Right, doc, false);
    model.moveAll(MovementKind::Right, doc, false);
    model.moveAll(MovementKind::Right, doc, false);
    model.moveAll(MovementKind::Right, doc, false);  // one extra past the end
    EXPECT_EQ(model.primaryCursor().position, 3U);   // clamped to length()
}

TEST(SelectionModelTest, LineStartAndLineEndMoveWithinLine) {
    Document doc;
    doc.insertText(0, u"hello\nworld");
    SelectionModel model(8);  // 'r' in "world" (line 1, column 2)

    model.moveAll(MovementKind::LineStart, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 6U);  // start of "world"

    model.moveAll(MovementKind::LineEnd, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 11U);  // end of document, no trailing newline
}

TEST(SelectionModelTest, PageUpPageDownJumpByPageSizeAndPreserveColumn) {
    Document doc;
    doc.insertText(0, u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9");  // 10 single-char lines, offset = 2*line
    SelectionModel model(10);                            // line 5

    model.moveAll(MovementKind::PageUp, doc, false, /*pageSize=*/3);
    EXPECT_EQ(model.primaryCursor().position, 4U);  // line 5-3=2 -> offset 4

    SelectionModel model2(10);
    model2.moveAll(MovementKind::PageDown, doc, false, /*pageSize=*/3);
    EXPECT_EQ(model2.primaryCursor().position, 16U);  // line 5+3=8 -> offset 16
}

TEST(SelectionModelTest, PageUpPageDownClampAtDocumentBoundaries) {
    Document doc;
    doc.insertText(0, u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
    SelectionModel model(10);  // line 5

    model.moveAll(MovementKind::PageUp, doc, false, /*pageSize=*/100);
    EXPECT_EQ(model.primaryCursor().position, 0U);  // clamped to line 0

    SelectionModel model2(10);
    model2.moveAll(MovementKind::PageDown, doc, false, /*pageSize=*/100);
    EXPECT_EQ(model2.primaryCursor().position, 18U);  // clamped to last line (offset of '9')
}

TEST(SelectionModelTest, PageDownWithZeroPageSizeIsNoOp) {
    Document doc;
    doc.insertText(0, u"0\n1\n2\n3\n4");
    SelectionModel model(2);  // line 1

    model.moveAll(MovementKind::PageDown, doc, false);  // pageSize defaults to 0
    EXPECT_EQ(model.primaryCursor().position, 2U);       // unchanged
}

TEST(SelectionModelTest, WordRightSkipsToStartOfNextWord) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);  // start of "hello"

    model.moveAll(MovementKind::WordRight, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 6U);  // start of "world"
}

TEST(SelectionModelTest, WordRightFromMidWhitespaceAlsoLandsAtNextWordStart) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(5);  // right after "hello", before the space

    model.moveAll(MovementKind::WordRight, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 6U);
}

TEST(SelectionModelTest, WordRightAtLineEndIsNoOp) {
    Document doc;
    doc.insertText(0, u"abc");
    SelectionModel model(3);  // already at line/document end

    model.moveAll(MovementKind::WordRight, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 3U);
}

TEST(SelectionModelTest, WordLeftSkipsToStartOfPreviousWord) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(11);  // end of "world"

    model.moveAll(MovementKind::WordLeft, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 6U);  // start of "world"

    model.moveAll(MovementKind::WordLeft, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 0U);  // start of "hello"
}

TEST(SelectionModelTest, WordLeftAtLineStartIsNoOp) {
    Document doc;
    doc.insertText(0, u"abc");
    SelectionModel model(0);

    model.moveAll(MovementKind::WordLeft, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 0U);
}

TEST(SelectionModelTest, WordRightCrossesLineBoundaryToNextWord) {
    // A lone line break between two words behaves like a single space would
    // within one line (WordRightFromMidWhitespaceAlsoLandsAtNextWordStart
    // above): there's no whitespace *character* to stop within, so one
    // press lands directly at the next word's start.
    Document doc;
    doc.insertText(0, u"foo\nbar");
    SelectionModel model(0);  // start of "foo"

    model.moveAll(MovementKind::WordRight, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 4U);  // crossed the line break to start of "bar"

    model.moveAll(MovementKind::WordRight, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 7U);  // end of "bar" / end of document
}

TEST(SelectionModelTest, WordLeftCrossesLineBoundaryToPreviousWord) {
    Document doc;
    doc.insertText(0, u"foo\nbar");
    SelectionModel model(7);  // end of "bar" (end of document)

    model.moveAll(MovementKind::WordLeft, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 4U);  // start of "bar"

    model.moveAll(MovementKind::WordLeft, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 0U);  // crossed the line break to start of "foo"
}

TEST(SelectionModelTest, WordRightSkipsOverAnEntireEmptyLine) {
    Document doc;
    doc.insertText(0, u"a\n\nb");  // line 1 (offset 2) is empty
    SelectionModel model(0);

    model.moveAll(MovementKind::WordRight, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 3U);  // start of "b", skipping the empty line entirely
}

TEST(SelectionModelTest, WordLeftSkipsOverAnEntireEmptyLine) {
    Document doc;
    doc.insertText(0, u"a\n\nb");
    SelectionModel model(3);  // start of "b"

    model.moveAll(MovementKind::WordLeft, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 0U);  // start of "a", skipping the empty line entirely
}

TEST(SelectionModelTest, WordRightAtDocumentEndOfMultiLineDocIsNoOp) {
    Document doc;
    doc.insertText(0, u"foo\nbar");
    SelectionModel model(7);  // end of document

    model.moveAll(MovementKind::WordRight, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 7U);
}

TEST(SelectionModelTest, WordLeftAtDocumentStartOfMultiLineDocIsNoOp) {
    Document doc;
    doc.insertText(0, u"foo\nbar");
    SelectionModel model(0);

    model.moveAll(MovementKind::WordLeft, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 0U);
}

TEST(SelectionModelTest, ShiftWordRightExtendsSelection) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);

    model.moveAll(MovementKind::WordRight, doc, /*extendSelection=*/true);
    EXPECT_EQ(model.primaryCursor().anchor, 0U);
    EXPECT_EQ(model.primaryCursor().position, 6U);
    EXPECT_TRUE(model.primaryCursor().hasSelection());
}

TEST(SelectionModelTest, UpDownPreserveColumnAndClampToShorterLines) {
    Document doc;
    doc.insertText(0, u"ab\nlonger line\nc");
    SelectionModel model(10);  // column 7 on "longer line" (line 1)

    model.moveAll(MovementKind::Up, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 2U);  // "ab" only has 2 columns -> clamped

    SelectionModel model2(10);
    model2.moveAll(MovementKind::Down, doc, false);
    // Line 2 is "c" (offset 15, length 1) -> column 7 clamps to end of line (offset 16).
    EXPECT_EQ(model2.primaryCursor().position, 16U);
}

TEST(SelectionModelTest, DocumentStartAndEndJumpToBoundaries) {
    Document doc;
    doc.insertText(0, u"hello\nworld");
    SelectionModel model(6);

    model.moveAll(MovementKind::DocumentStart, doc, false);
    EXPECT_EQ(model.primaryCursor().position, 0U);

    model.moveAll(MovementKind::DocumentEnd, doc, false);
    EXPECT_EQ(model.primaryCursor().position, doc.length());
}

TEST(SelectionModelTest, ExtendSelectionKeepsAnchorFixed) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);

    model.moveAll(MovementKind::Right, doc, /*extendSelection=*/true);
    model.moveAll(MovementKind::Right, doc, true);
    model.moveAll(MovementKind::Right, doc, true);

    EXPECT_EQ(model.primaryCursor().anchor, 0U);
    EXPECT_EQ(model.primaryCursor().position, 3U);
    EXPECT_TRUE(model.primaryCursor().hasSelection());
}

TEST(SelectionModelTest, NonExtendingMoveCollapsesSelection) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);

    model.moveAll(MovementKind::Right, doc, true);
    model.moveAll(MovementKind::Right, doc, true);
    ASSERT_TRUE(model.primaryCursor().hasSelection());

    model.moveAll(MovementKind::Right, doc, /*extendSelection=*/false);
    EXPECT_FALSE(model.primaryCursor().hasSelection());
    EXPECT_EQ(model.primaryCursor().anchor, model.primaryCursor().position);
}

TEST(SelectionModelTest, AddCursorKeepsBothWhenFarApart) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);
    model.addCursor(6);

    ASSERT_EQ(model.cursors().size(), 2U);
    EXPECT_EQ(model.cursors()[0].position, 0U);
    EXPECT_EQ(model.cursors()[1].position, 6U);
}

TEST(SelectionModelTest, AddCursorAtSamePositionMerges) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(3);
    model.addCursor(3);

    EXPECT_EQ(model.cursors().size(), 1U);
}

TEST(SelectionModelTest, MoveAllAppliesToEveryCursor) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);
    model.addCursor(6);
    ASSERT_EQ(model.cursors().size(), 2U);

    model.moveAll(MovementKind::Right, doc, false);
    EXPECT_EQ(model.cursors()[0].position, 1U);
    EXPECT_EQ(model.cursors()[1].position, 7U);
}

TEST(SelectionModelTest, OverlappingCursorsMergeAfterMovement) {
    Document doc;
    doc.insertText(0, u"hello world");  // length 11
    SelectionModel model(10);
    model.addCursor(11);  // distinct: gap of 1, no merge yet

    ASSERT_EQ(model.cursors().size(), 2U);

    // Both cursors clamp to length() (11) on the same Right move, so they
    // land on the same position and merge.
    model.moveAll(MovementKind::Right, doc, false);
    EXPECT_EQ(model.cursors().size(), 1U);
    EXPECT_EQ(model.cursors()[0].position, 11U);
}

TEST(SelectionModelTest, CollapseToPrimaryDropsOtherCursors) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);
    model.addCursor(6);
    ASSERT_EQ(model.cursors().size(), 2U);

    model.collapseToPrimary();
    EXPECT_EQ(model.cursors().size(), 1U);
    EXPECT_TRUE(model.cursors()[0].isPrimary);
}

TEST(SelectionModelTest, MoveAllToSetsPositionAndAnchorClearingSelection) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);
    model.moveAll(MovementKind::Right, doc, /*extendSelection=*/true);  // create a selection
    ASSERT_TRUE(model.primaryCursor().hasSelection());

    model.moveAllTo(9);
    EXPECT_EQ(model.primaryCursor().position, 9U);
    EXPECT_EQ(model.primaryCursor().anchor, 9U);
    EXPECT_FALSE(model.primaryCursor().hasSelection());
}

TEST(SelectionModelTest, MoveAllToAppliesToEveryCursorAndMerges) {
    SelectionModel model(0);
    model.addCursor(6);
    ASSERT_EQ(model.cursors().size(), 2U);

    model.moveAllTo(3);
    EXPECT_EQ(model.cursors().size(), 1U);  // both cursors landed on the same position
    EXPECT_EQ(model.cursors()[0].position, 3U);
}

TEST(SelectionModelTest, MoveAllToWithExtendKeepsAnchor) {
    SelectionModel model(3);  // anchor and position both start at 3

    model.moveAllTo(9, /*extendSelection=*/true);
    EXPECT_EQ(model.primaryCursor().anchor, 3U);    // unchanged
    EXPECT_EQ(model.primaryCursor().position, 9U);  // moved
    EXPECT_TRUE(model.primaryCursor().hasSelection());
}

TEST(SelectionModelTest, MoveAllToWithoutExtendCollapsesExistingSelection) {
    SelectionModel model(0);
    model.moveAllTo(9, /*extendSelection=*/true);
    ASSERT_TRUE(model.primaryCursor().hasSelection());

    model.moveAllTo(2);  // extendSelection defaults to false
    EXPECT_EQ(model.primaryCursor().anchor, 2U);
    EXPECT_EQ(model.primaryCursor().position, 2U);
    EXPECT_FALSE(model.primaryCursor().hasSelection());
}

TEST(SelectionModelTest, SelectWordAtSelectsAsciiWordInMiddleOfLine) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel model(0);

    model.selectWordAt(2, doc);  // inside "hello"
    EXPECT_EQ(model.primaryCursor().anchor, 0U);
    EXPECT_EQ(model.primaryCursor().position, 5U);
}

TEST(SelectionModelTest, SelectWordAtClickAtVeryEndOfLineSelectsLastWord) {
    Document doc;
    doc.insertText(0, u"abc");
    SelectionModel model(0);

    model.selectWordAt(3, doc);  // == doc.length(), one past the last char
    EXPECT_EQ(model.primaryCursor().anchor, 0U);
    EXPECT_EQ(model.primaryCursor().position, 3U);
}

TEST(SelectionModelTest, SelectWordAtOnPunctuationSelectsSingleCharacter) {
    Document doc;
    doc.insertText(0, u"a.b");
    SelectionModel model(0);

    model.selectWordAt(1, doc);  // the '.'
    EXPECT_EQ(model.primaryCursor().anchor, 1U);
    EXPECT_EQ(model.primaryCursor().position, 2U);
}

TEST(SelectionModelTest, SelectWordAtOnWhitespaceSelectsWhitespaceRun) {
    Document doc;
    doc.insertText(0, u"a   b");  // 3 spaces
    SelectionModel model(0);

    model.selectWordAt(2, doc);  // middle space
    EXPECT_EQ(model.primaryCursor().anchor, 1U);
    EXPECT_EQ(model.primaryCursor().position, 4U);
}

TEST(SelectionModelTest, SelectWordAtGroupsContiguousCjkCharactersAsOneWord) {
    Document doc;
    doc.insertText(0, u"hello こんにちは world");  // "hello こんにちは world"
    SelectionModel model(0);

    model.selectWordAt(8, doc);  // inside the hiragana run (offset 6-11)
    EXPECT_EQ(model.primaryCursor().anchor, 6U);
    EXPECT_EQ(model.primaryCursor().position, 11U);
}

TEST(SelectionModelTest, SelectWordAtOnEmptyLineIsNoOp) {
    Document doc;
    doc.insertText(0, u"a\n\nb");  // line 1 (offset 2) is empty
    SelectionModel model(0);

    model.selectWordAt(2, doc);
    EXPECT_FALSE(model.primaryCursor().hasSelection());
    EXPECT_EQ(model.primaryCursor().position, 2U);
}

TEST(SelectionModelTest, SetCursorsReplacesEntireCursorSet) {
    SelectionModel model(0);
    model.addCursor(6);
    ASSERT_EQ(model.cursors().size(), 2U);

    model.setCursors(std::vector<Cursor>{Cursor{.position = 2, .anchor = 1, .isPrimary = true},
                                         Cursor{.position = 9, .anchor = 9, .isPrimary = false}});
    ASSERT_EQ(model.cursors().size(), 2U);
    EXPECT_EQ(model.cursors()[0].anchor, 1U);
    EXPECT_EQ(model.cursors()[0].position, 2U);
    EXPECT_EQ(model.cursors()[1].position, 9U);
    EXPECT_TRUE(model.primaryCursor().position == 2U);
}

TEST(SelectionModelTest, SetCursorsMergesCursorsThatLandOnTheSamePosition) {
    SelectionModel model(0);

    model.setCursors(std::vector<Cursor>{Cursor{.position = 3, .anchor = 3, .isPrimary = true},
                                         Cursor{.position = 3, .anchor = 3, .isPrimary = false}});
    EXPECT_EQ(model.cursors().size(), 1U);
}

TEST(SelectionModelTest, SelectLineAtIncludesTrailingNewlineExceptOnLastLine) {
    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    SelectionModel model(0);

    model.selectLineAt(8, doc);  // inside "line1" (offset 6-11)
    EXPECT_EQ(model.primaryCursor().anchor, 6U);
    EXPECT_EQ(model.primaryCursor().position, 12U);  // includes the '\n' before "line2"

    model.selectLineAt(15, doc);  // inside "line2", the last line
    EXPECT_EQ(model.primaryCursor().anchor, 12U);
    EXPECT_EQ(model.primaryCursor().position, doc.length());  // no trailing '\n' to include
}

TEST(SelectionModelTest, MoveCursorMatchingExtendsOnlyTheIdentifiedCursor) {
    SelectionModel model(0);
    model.addCursor(6);  // primary at 0, secondary at 6 (its anchor is 6)
    ASSERT_EQ(model.cursors().size(), 2U);

    model.moveCursorMatching(/*identifyingAnchor=*/6, /*newPos=*/9);
    ASSERT_EQ(model.cursors().size(), 2U);
    EXPECT_EQ(model.cursors()[0].position, 0U);  // untouched
    EXPECT_EQ(model.cursors()[0].anchor, 0U);
    EXPECT_EQ(model.cursors()[1].anchor, 6U);   // unchanged - identity for further calls
    EXPECT_EQ(model.cursors()[1].position, 9U);  // extended
    EXPECT_TRUE(model.cursors()[1].hasSelection());
}

TEST(SelectionModelTest, MoveCursorMatchingCanBeCalledRepeatedlyWithTheSameAnchor) {
    SelectionModel model(0);
    model.addCursor(6);

    model.moveCursorMatching(6, 9);
    model.moveCursorMatching(6, 3);  // simulates a drag moving back past the anchor
    ASSERT_EQ(model.cursors().size(), 2U);
    EXPECT_EQ(model.cursors()[1].anchor, 6U);
    EXPECT_EQ(model.cursors()[1].position, 3U);
}

TEST(SelectionModelTest, MoveCursorMatchingIsNoOpWhenNoAnchorMatches) {
    SelectionModel model(0);
    model.addCursor(6);

    model.moveCursorMatching(/*identifyingAnchor=*/999, /*newPos=*/3);
    ASSERT_EQ(model.cursors().size(), 2U);
    EXPECT_EQ(model.cursors()[0].position, 0U);
    EXPECT_EQ(model.cursors()[1].position, 6U);
}

// "abcde" (0-4) '\n'(5) "fg" (6-7) '\n'(8) "hijkl" (9-13). Line 1 ("fg") is
// deliberately shorter than the other two, for clamping tests below.
Document threeLineDoc() {
    Document doc;
    doc.insertText(0, u"abcde\nfg\nhijkl");
    return doc;
}

TEST(SelectionModelTest, SetRectangularSelectionCreatesOneCursorPerLine) {
    const Document doc = threeLineDoc();
    SelectionModel model(0);

    model.setRectangularSelection(/*anchor=*/1, /*active=*/12, doc);  // line0 col1 -> line2 col3

    ASSERT_EQ(model.cursors().size(), 3U);
    EXPECT_EQ(model.cursors()[0].anchor, 1U);
    EXPECT_EQ(model.cursors()[0].position, 3U);
    EXPECT_EQ(model.cursors()[2].anchor, 10U);
    EXPECT_EQ(model.cursors()[2].position, 12U);
}

TEST(SelectionModelTest, SetRectangularSelectionClampsColumnToShortLineLength) {
    const Document doc = threeLineDoc();
    SelectionModel model(0);

    model.setRectangularSelection(/*anchor=*/1, /*active=*/12, doc);  // col range [1,3), line1 "fg" has length 2

    ASSERT_EQ(model.cursors().size(), 3U);
    const Cursor& middleRow = model.cursors()[1];
    EXPECT_EQ(middleRow.anchor, 7U);    // lineStart(6) + min(1,2)
    EXPECT_EQ(middleRow.position, 8U);  // lineStart(6) + min(3,2) - clamped, not 9
}

TEST(SelectionModelTest, SetRectangularSelectionPositionTracksActiveEndRegardlessOfDragDirection) {
    const Document doc = threeLineDoc();

    SelectionModel downRight(0);
    downRight.setRectangularSelection(/*anchor=*/1, /*active=*/12, doc);  // line0->line2, col1->col3

    SelectionModel upLeft(0);
    upLeft.setRectangularSelection(/*anchor=*/12, /*active=*/1, doc);  // reversed: line2->line0, col3->col1

    // Same set of rows/columns covered, but which field (anchor vs position)
    // holds which column must flip with the reversed call - this is the
    // round-1 regression case (position must never jump back to the
    // anchor's column mid-drag).
    ASSERT_EQ(downRight.cursors().size(), 3U);
    ASSERT_EQ(upLeft.cursors().size(), 3U);
    EXPECT_EQ(downRight.cursors()[0].anchor, 1U);
    EXPECT_EQ(downRight.cursors()[0].position, 3U);
    EXPECT_EQ(upLeft.cursors()[0].anchor, 3U);
    EXPECT_EQ(upLeft.cursors()[0].position, 1U);
}

TEST(SelectionModelTest, SetRectangularSelectionOnSingleLineIsEquivalentToNormalSelection) {
    const Document doc = threeLineDoc();
    SelectionModel model(0);

    model.setRectangularSelection(/*anchor=*/1, /*active=*/3, doc);  // both on line0

    ASSERT_EQ(model.cursors().size(), 1U);
    EXPECT_EQ(model.cursors()[0].anchor, 1U);
    EXPECT_EQ(model.cursors()[0].position, 3U);
    EXPECT_TRUE(model.cursors()[0].isPrimary);
}

TEST(SelectionModelTest, SetRectangularSelectionSetsIsPrimaryOnActiveLineOnly) {
    const Document doc = threeLineDoc();

    SelectionModel downRight(0);
    downRight.setRectangularSelection(/*anchor=*/1, /*active=*/12, doc);  // active on line2 (last row)
    ASSERT_EQ(downRight.cursors().size(), 3U);
    EXPECT_FALSE(downRight.cursors()[0].isPrimary);
    EXPECT_FALSE(downRight.cursors()[1].isPrimary);
    EXPECT_TRUE(downRight.cursors()[2].isPrimary);

    SelectionModel upLeft(0);
    upLeft.setRectangularSelection(/*anchor=*/12, /*active=*/1, doc);  // active on line0 (first row)
    ASSERT_EQ(upLeft.cursors().size(), 3U);
    EXPECT_TRUE(upLeft.cursors()[0].isPrimary);
    EXPECT_FALSE(upLeft.cursors()[1].isPrimary);
    EXPECT_FALSE(upLeft.cursors()[2].isPrimary);
}

TEST(SelectionModelTest, SetRectangularSelectionHandlesEmptyLinesWithoutCrash) {
    Document doc;
    doc.insertText(0, u"ab\n\ncd");  // line1 is empty
    SelectionModel model(0);

    model.setRectangularSelection(/*anchor=*/1, /*active=*/5, doc);  // line0 col1 -> line2 col1

    ASSERT_EQ(model.cursors().size(), 3U);
    const Cursor& emptyRow = model.cursors()[1];
    EXPECT_EQ(emptyRow.anchor, emptyRow.position);  // clamped to 0-length line, no selection on this row
}

// Phase 4b8g: moveTextPos() is moveAll()'s per-cursor movement math,
// promoted to a public free function so main.cpp's Shift+Alt+arrow handler
// can reuse it directly. These mirror a couple of moveAll()'s own existing
// cases above, just calling the free function without a SelectionModel.
TEST(SelectionModelTest, MoveTextPosComputesRightWithoutAnySelectionModelState) {
    const Document doc = threeLineDoc();
    EXPECT_EQ(moveTextPos(MovementKind::Right, doc, 0), 1U);
}

TEST(SelectionModelTest, MoveTextPosComputesLineEndAndVerticalMovement) {
    const Document doc = threeLineDoc();
    EXPECT_EQ(moveTextPos(MovementKind::LineEnd, doc, 0), 5U);  // end of "abcde"
    EXPECT_EQ(moveTextPos(MovementKind::Down, doc, 0), 6U);     // line1 col0 = 'f'
}

// Phase 4b8g (Shift+Alt+I): convertToLineEndCursors().
TEST(SelectionModelTest, ConvertToLineEndCursorsWithSingleCursorNoSelection) {
    const Document doc = threeLineDoc();
    SelectionModel model(1);  // line0, col1, no selection

    model.convertToLineEndCursors(doc);

    ASSERT_EQ(model.cursors().size(), 1U);
    EXPECT_EQ(model.cursors()[0].position, 5U);  // end of "abcde"
    EXPECT_EQ(model.cursors()[0].anchor, 5U);
    EXPECT_TRUE(model.cursors()[0].isPrimary);
}

TEST(SelectionModelTest, ConvertToLineEndCursorsSpansSelectionAcrossMultipleLines) {
    const Document doc = threeLineDoc();
    SelectionModel model(0);
    model.moveAllTo(1);                              // line0 col1
    model.moveAllTo(12, /*extendSelection=*/true);    // extends to line2 col3 (anchor stays at 1)

    model.convertToLineEndCursors(doc);

    ASSERT_EQ(model.cursors().size(), 3U);
    EXPECT_EQ(model.cursors()[0].position, 5U);   // line0 end
    EXPECT_EQ(model.cursors()[1].position, 8U);   // line1 end ("fg")
    EXPECT_EQ(model.cursors()[2].position, 14U);  // line2 end (last line == doc.length())
    // No selection remains on any row.
    EXPECT_EQ(model.cursors()[0].position, model.cursors()[0].anchor);
    EXPECT_EQ(model.cursors()[1].position, model.cursors()[1].anchor);
    EXPECT_EQ(model.cursors()[2].position, model.cursors()[2].anchor);
    EXPECT_TRUE(model.cursors()[2].isPrimary);  // last spanned line becomes primary
}

TEST(SelectionModelTest, ConvertToLineEndCursorsUsesBothAnchorAndPositionToFindSpannedLines) {
    // An upward selection: position ends up on line0, anchor stays on
    // line2 - the spanned range must still be [line0, line2], not just
    // whichever line `position` alone happens to be on.
    const Document doc = threeLineDoc();
    SelectionModel model(0);
    model.moveAllTo(12);                          // anchor+position both at line2 col3
    model.moveAllTo(1, /*extendSelection=*/true);  // position moves to line0 col1, anchor stays at 12

    model.convertToLineEndCursors(doc);

    ASSERT_EQ(model.cursors().size(), 3U);
    EXPECT_EQ(model.cursors()[0].position, 5U);
    EXPECT_EQ(model.cursors()[1].position, 8U);
    EXPECT_EQ(model.cursors()[2].position, 14U);
}

}  // namespace
