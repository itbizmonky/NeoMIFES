#include <gtest/gtest.h>

#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::MovementKind;
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

}  // namespace
