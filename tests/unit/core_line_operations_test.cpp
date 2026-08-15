#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "neomifes/core/command.h"
#include "neomifes/core/cursor.h"
#include "neomifes/core/line_operation_command.h"
#include "neomifes/core/line_operations.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::computeDeleteLineEdits;
using neomifes::core::computeDuplicateLineEdits;
using neomifes::core::computeMoveLineEdits;
using neomifes::core::Cursor;
using neomifes::core::ExecutionContext;
using neomifes::core::LineOperationCommand;
using neomifes::core::LineOperationPlan;
using neomifes::document::Document;

Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// Executes `plan` via a real LineOperationCommand and returns the resulting
// document text - the black-box way this whole test file verifies
// correctness (rather than hand-checking PerCursorEdit internals).
std::u16string apply(Document& doc, LineOperationPlan plan) {
    neomifes::core::SelectionModel selection;
    ExecutionContext                ctx(doc, selection);
    std::vector<Cursor>             before(plan.cursorMappings.size(), Cursor{});
    LineOperationCommand            cmd(std::move(plan.edits), before, std::move(plan.cursorMappings),
                                        "edit.testLineOperation");
    cmd.execute(ctx);
    return doc.toU16String();
}

TEST(DuplicateLineTest, SingleCursorDuplicatesLineImmediatelyBelow) {
    Document doc = makeDoc(u"abc\ndef\nghi");
    const std::vector<Cursor> cursors{Cursor{.position = 5, .anchor = 5}};  // line1 ("def"), col1

    LineOperationPlan plan = computeDuplicateLineEdits(doc, cursors);
    ASSERT_EQ(plan.edits.size(), 1U);
    ASSERT_EQ(plan.cursorMappings.size(), 1U);

    const std::u16string result = apply(doc, plan);
    EXPECT_EQ(result, u"abc\ndef\ndef\nghi");
}

TEST(DuplicateLineTest, CursorLandsAtSameColumnWithinTheDuplicate) {
    Document doc = makeDoc(u"abc\ndefgh\nxyz");
    const std::vector<Cursor> cursors{Cursor{.position = 6, .anchor = 6}};  // line1, col2 ("de|fgh")

    LineOperationPlan   plan = computeDuplicateLineEdits(doc, cursors);
    neomifes::core::SelectionModel selection;
    ExecutionContext    ctx(doc, selection);
    LineOperationCommand cmd(std::move(plan.edits), {Cursor{.position = 6, .anchor = 6}},
                             std::move(plan.cursorMappings), "edit.duplicateLine");
    cmd.execute(ctx);

    EXPECT_EQ(doc.toU16String(), u"abc\ndefgh\ndefgh\nxyz");
    ASSERT_EQ(cmd.cursorsAfterExecute().size(), 1U);
    // "abc\n" (4) + "defgh\n" (6, the untouched original line1) + 2 (column
    // within the duplicate) = 12.
    EXPECT_EQ(cmd.cursorsAfterExecute()[0].position, 12U);
}

TEST(DuplicateLineTest, TwoCursorsOnTheSameLineCollapseToOneDuplicate) {
    Document doc = makeDoc(u"abc\ndef");
    const std::vector<Cursor> cursors{Cursor{.position = 4, .anchor = 4},   // line1, col0
                                      Cursor{.position = 6, .anchor = 6}};  // line1, col2

    LineOperationPlan plan = computeDuplicateLineEdits(doc, cursors);
    EXPECT_EQ(plan.edits.size(), 1U);  // one distinct line -> one edit
    ASSERT_EQ(plan.cursorMappings.size(), 2U);

    EXPECT_EQ(apply(doc, plan), u"abc\ndef\ndef");
}

TEST(DuplicateLineTest, LastLineWithNoTrailingNewlineDuplicatesCorrectly) {
    Document doc = makeDoc(u"abc");
    const std::vector<Cursor> cursors{Cursor{.position = 1, .anchor = 1}};

    EXPECT_EQ(apply(doc, computeDuplicateLineEdits(doc, cursors)), u"abc\nabc");
}

TEST(DuplicateLineTest, UndoRestoresOriginalTextAndCursor) {
    Document doc = makeDoc(u"abc\ndef");
    const std::vector<Cursor> cursorsBefore{Cursor{.position = 4, .anchor = 4, .isPrimary = true}};
    LineOperationPlan          plan = computeDuplicateLineEdits(doc, cursorsBefore);

    neomifes::core::SelectionModel selection;
    ExecutionContext    ctx(doc, selection);
    LineOperationCommand cmd(std::move(plan.edits), cursorsBefore, std::move(plan.cursorMappings),
                             "edit.duplicateLine");
    cmd.execute(ctx);
    ASSERT_EQ(doc.toU16String(), u"abc\ndef\ndef");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"abc\ndef");
    ASSERT_EQ(cmd.cursorsAfterUndo().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterUndo()[0].position, 4U);
    EXPECT_TRUE(cmd.cursorsAfterUndo()[0].isPrimary);
}

TEST(DeleteLineTest, SingleCursorDeletesWholeLineIncludingItsNewline) {
    Document doc = makeDoc(u"abc\ndef\nghi");
    const std::vector<Cursor> cursors{Cursor{.position = 5, .anchor = 5}};  // line1 ("def")

    LineOperationPlan plan = computeDeleteLineEdits(doc, cursors);
    EXPECT_EQ(apply(doc, plan), u"abc\nghi");
}

TEST(DeleteLineTest, DeletingTheTrueLastLineAloneAlsoRemovesThePrecedingNewline) {
    Document doc = makeDoc(u"abc\ndef");
    const std::vector<Cursor> cursors{Cursor{.position = 4, .anchor = 4}};  // line1 ("def"), the last line

    EXPECT_EQ(apply(doc, computeDeleteLineEdits(doc, cursors)), u"abc");
}

TEST(DeleteLineTest, DeletingEveryLineLeavesAnEmptyDocument) {
    Document doc = makeDoc(u"abc\ndef");
    const std::vector<Cursor> cursors{Cursor{.position = 1, .anchor = 1}, Cursor{.position = 5, .anchor = 5}};

    EXPECT_EQ(apply(doc, computeDeleteLineEdits(doc, cursors)), u"");
}

TEST(DeleteLineTest, DeletingTwoAdjacentSelectedLinesDoesNotOverlapOrCorrupt) {
    Document doc = makeDoc(u"abc\ndef\nghi");
    // Selects the last two lines (adjacent), including the document's true
    // last line - exercises the "previousAlsoDeleted" branch.
    const std::vector<Cursor> cursors{Cursor{.position = 5, .anchor = 5}, Cursor{.position = 9, .anchor = 9}};

    EXPECT_EQ(apply(doc, computeDeleteLineEdits(doc, cursors)), u"abc");
}

TEST(DeleteLineTest, CursorLandsAtStartOfWhateverLineNowOccupiesThatPosition) {
    Document doc = makeDoc(u"abc\ndef\nghi");
    const std::vector<Cursor> cursorsBefore{Cursor{.position = 6, .anchor = 6, .isPrimary = true}};  // line1
    LineOperationPlan          plan = computeDeleteLineEdits(doc, cursorsBefore);

    neomifes::core::SelectionModel selection;
    ExecutionContext    ctx(doc, selection);
    LineOperationCommand cmd(std::move(plan.edits), cursorsBefore, std::move(plan.cursorMappings),
                             "edit.deleteLine");
    cmd.execute(ctx);

    ASSERT_EQ(doc.toU16String(), u"abc\nghi");
    ASSERT_EQ(cmd.cursorsAfterExecute().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterExecute()[0].position, 4U);  // start of "ghi", the line that shifted up
}

TEST(MoveLineTest, MoveDownSwapsWithTheFollowingLine) {
    Document doc = makeDoc(u"abc\ndef\nghi");
    const std::vector<Cursor> cursors{Cursor{.position = 1, .anchor = 1}};  // line0 ("abc")

    LineOperationPlan plan = computeMoveLineEdits(doc, cursors, /*moveDown=*/true);
    ASSERT_FALSE(plan.edits.empty());
    EXPECT_EQ(apply(doc, plan), u"def\nabc\nghi");
}

TEST(MoveLineTest, MoveUpSwapsWithThePrecedingLine) {
    Document doc = makeDoc(u"abc\ndef\nghi");
    const std::vector<Cursor> cursors{Cursor{.position = 5, .anchor = 5}};  // line1 ("def")

    EXPECT_EQ(apply(doc, computeMoveLineEdits(doc, cursors, /*moveDown=*/false)), u"def\nabc\nghi");
}

TEST(MoveLineTest, MoveUpAtDocumentTopIsANoOp) {
    Document doc = makeDoc(u"abc\ndef");
    const std::vector<Cursor> cursors{Cursor{.position = 1, .anchor = 1}};  // line0

    const LineOperationPlan plan = computeMoveLineEdits(doc, cursors, /*moveDown=*/false);
    EXPECT_TRUE(plan.edits.empty());
    EXPECT_TRUE(plan.cursorMappings.empty());
}

TEST(MoveLineTest, MoveDownAtDocumentBottomIsANoOp) {
    Document doc = makeDoc(u"abc\ndef");
    const std::vector<Cursor> cursors{Cursor{.position = 5, .anchor = 5}};  // line1, the last line

    const LineOperationPlan plan = computeMoveLineEdits(doc, cursors, /*moveDown=*/true);
    EXPECT_TRUE(plan.edits.empty());
}

TEST(MoveLineTest, MoveDownWhenTheBlockIncludesTheLastLineDropsItsTrailingNewlineCorrectly) {
    Document doc = makeDoc(u"abc\ndef");  // "def" is the true last line, no trailing '\n'
    const std::vector<Cursor> cursors{Cursor{.position = 1, .anchor = 1}};  // line0 ("abc")

    // "def" (now first) gains a '\n'; "abc" (now last) must NOT gain one.
    EXPECT_EQ(apply(doc, computeMoveLineEdits(doc, cursors, /*moveDown=*/true)), u"def\nabc");
}

TEST(MoveLineTest, ContiguousBlockOfTwoLinesMovesAsOneUnit) {
    Document doc = makeDoc(u"abc\ndef\nghi\njkl");
    const std::vector<Cursor> cursors{Cursor{.position = 1, .anchor = 1},   // line0
                                      Cursor{.position = 5, .anchor = 5}};  // line1 (contiguous with line0)

    EXPECT_EQ(apply(doc, computeMoveLineEdits(doc, cursors, /*moveDown=*/true)), u"ghi\nabc\ndef\njkl");
}

TEST(MoveLineTest, NonContiguousGroupsMoveIndependentlyInOneCall) {
    Document doc = makeDoc(u"a\nb\nc\nd\ne");
    const std::vector<Cursor> cursors{Cursor{.position = 2, .anchor = 2},   // line1 ("b")
                                      Cursor{.position = 6, .anchor = 6}};  // line3 ("d")

    EXPECT_EQ(apply(doc, computeMoveLineEdits(doc, cursors, /*moveDown=*/true)), u"a\nc\nb\ne\nd");
}

TEST(MoveLineTest, UndoRestoresOriginalOrderAndCursor) {
    Document doc = makeDoc(u"abc\ndef\nghi");
    const std::vector<Cursor> cursorsBefore{Cursor{.position = 1, .anchor = 1, .isPrimary = true}};
    LineOperationPlan          plan = computeMoveLineEdits(doc, cursorsBefore, /*moveDown=*/true);

    neomifes::core::SelectionModel selection;
    ExecutionContext    ctx(doc, selection);
    LineOperationCommand cmd(std::move(plan.edits), cursorsBefore, std::move(plan.cursorMappings),
                             "edit.moveLineDown");
    cmd.execute(ctx);
    ASSERT_EQ(doc.toU16String(), u"def\nabc\nghi");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"abc\ndef\nghi");
    ASSERT_EQ(cmd.cursorsAfterUndo().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterUndo()[0].position, 1U);
}

TEST(LineOperationsTest, EmptyCursorSpanProducesAnEmptyPlan) {
    Document doc = makeDoc(u"abc");
    EXPECT_TRUE(computeDuplicateLineEdits(doc, {}).edits.empty());
    EXPECT_TRUE(computeDeleteLineEdits(doc, {}).edits.empty());
    EXPECT_TRUE(computeMoveLineEdits(doc, {}, true).edits.empty());
}

}  // namespace
