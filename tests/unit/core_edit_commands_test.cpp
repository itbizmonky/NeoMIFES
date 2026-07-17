#include <gtest/gtest.h>

#include <vector>

#include "neomifes/core/cursor.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::Cursor;
using neomifes::core::DeleteRangeCommand;
using neomifes::core::ExecutionContext;
using neomifes::core::InsertTextCommand;
using neomifes::core::MultiCursorEditCommand;
using neomifes::core::PerCursorEdit;
using neomifes::core::ReplaceRangeCommand;
using neomifes::core::SelectionModel;
using neomifes::document::Document;
using neomifes::document::TextRange;

TEST(InsertTextCommandTest, ExecuteInsertsThenUndoRemoves) {
    Document       doc;
    SelectionModel selection;
    ExecutionContext ctx(doc, selection);

    InsertTextCommand cmd(0, u"hello");
    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"hello");
    ASSERT_EQ(cmd.cursorsAfterExecute().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterExecute()[0].position, 5U);

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"");
    ASSERT_EQ(cmd.cursorsAfterUndo().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterUndo()[0].position, 0U);
}

TEST(InsertTextCommandTest, WeightMatchesTextSizeFormula) {
    const InsertTextCommand cmd(0, u"hello");
    EXPECT_EQ(cmd.weight(), (5U * 2) + 32);
}

TEST(DeleteRangeCommandTest, ExecuteDeletesThenUndoRestoresExactText) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    DeleteRangeCommand cmd(TextRange{.start = 5, .end = 11});  // " world"
    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"hello");
    ASSERT_EQ(cmd.cursorsAfterExecute().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterExecute()[0].position, 5U);

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"hello world");
    ASSERT_EQ(cmd.cursorsAfterUndo().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterUndo()[0].position, 11U);  // end of the restored " world"
}

TEST(ReplaceRangeCommandTest, ExecuteReplacesThenUndoRestoresOriginal) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    ReplaceRangeCommand cmd(TextRange{.start = 0, .end = 5}, u"HELLO");
    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"HELLO world");
    ASSERT_EQ(cmd.cursorsAfterExecute().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterExecute()[0].position, 5U);

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"hello world");
    ASSERT_EQ(cmd.cursorsAfterUndo().size(), 1U);
    EXPECT_EQ(cmd.cursorsAfterUndo()[0].position, 5U);
}

TEST(ReplaceRangeCommandTest, UndoRestoresOriginalLengthWhenReplacementIsLonger) {
    Document doc;
    doc.insertText(0, u"a b");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    ReplaceRangeCommand cmd(TextRange{.start = 0, .end = 1}, u"aaa");
    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"aaa b");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"a b");
}

TEST(MultiCursorEditCommandTest, InsertAtTwoCursorsAppliesCumulativeShiftToLaterCursor) {
    Document doc;
    doc.insertText(0, u"ab cd");  // insert 'X' after 'a' (pos 1) and after 'c' (pos 4)
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 1, .end = 1}, .insertedText = u"X"},
        PerCursorEdit{.range = TextRange{.start = 4, .end = 4}, .insertedText = u"X"},
    };
    std::vector<Cursor> before{
        Cursor{.position = 1, .anchor = 1, .isPrimary = true},
        Cursor{.position = 4, .anchor = 4, .isPrimary = false},
    };
    MultiCursorEditCommand cmd(edits, before);

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"aXb cXd");
    const auto after = cmd.cursorsAfterExecute();
    ASSERT_EQ(after.size(), 2U);
    EXPECT_EQ(after[0].position, 2U);  // right after the first inserted 'X'
    EXPECT_EQ(after[1].position, 6U);  // shifted by the first insert's +1
    EXPECT_TRUE(after[0].isPrimary);
    EXPECT_FALSE(after[1].isPrimary);

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"ab cd");
    const auto restored = cmd.cursorsAfterUndo();
    ASSERT_EQ(restored.size(), 2U);
    EXPECT_EQ(restored[0].position, 1U);
    EXPECT_EQ(restored[1].position, 4U);
}

TEST(MultiCursorEditCommandTest, ReplacesActiveSelectionsAtEachCursor) {
    Document doc;
    doc.insertText(0, u"foo bar");  // replace "foo" -> "X", "bar" -> "Y"
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 0, .end = 3}, .insertedText = u"X"},
        PerCursorEdit{.range = TextRange{.start = 4, .end = 7}, .insertedText = u"Y"},
    };
    std::vector<Cursor> before{
        Cursor{.position = 3, .anchor = 0, .isPrimary = true},  // selected "foo"
        Cursor{.position = 7, .anchor = 4, .isPrimary = false}, // selected "bar"
    };
    MultiCursorEditCommand cmd(edits, before);

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"X Y");
    const auto after = cmd.cursorsAfterExecute();
    ASSERT_EQ(after.size(), 2U);
    EXPECT_EQ(after[0].position, 1U);
    EXPECT_FALSE(after[0].hasSelection());  // selection collapses after replace
    EXPECT_EQ(after[1].position, 3U);

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"foo bar");
    const auto restored = cmd.cursorsAfterUndo();
    ASSERT_EQ(restored.size(), 2U);
    // Original selections (anchor != position) are restored exactly, not
    // just collapsed positions.
    EXPECT_EQ(restored[0].anchor, 0U);
    EXPECT_EQ(restored[0].position, 3U);
    EXPECT_EQ(restored[1].anchor, 4U);
    EXPECT_EQ(restored[1].position, 7U);
}

TEST(MultiCursorEditCommandTest, NoOpEditForBoundaryCursorLeavesItUnchanged) {
    Document doc;
    doc.insertText(0, u"ab");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    // Simulates Backspace with 2 cursors: one at document start (no-op, empty
    // range/empty text) and one after 'a' (deletes it).
    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 0, .end = 0}, .insertedText = u""},
        PerCursorEdit{.range = TextRange{.start = 1, .end = 1}, .insertedText = u""},
    };
    std::vector<Cursor> before{
        Cursor{.position = 0, .anchor = 0, .isPrimary = true},
        Cursor{.position = 1, .anchor = 1, .isPrimary = false},
    };
    MultiCursorEditCommand cmd(edits, before);

    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"ab");  // both ranges are empty - true no-op
    const auto after = cmd.cursorsAfterExecute();
    ASSERT_EQ(after.size(), 2U);
    EXPECT_EQ(after[0].position, 0U);
    EXPECT_EQ(after[1].position, 1U);
}

TEST(MultiCursorEditCommandTest, WeightSumsAllEdits) {
    std::vector<PerCursorEdit> edits{
        PerCursorEdit{.range = TextRange{.start = 0, .end = 0}, .insertedText = u"ab"},
        PerCursorEdit{.range = TextRange{.start = 5, .end = 5}, .insertedText = u"c"},
    };
    std::vector<Cursor> before{Cursor{.position = 0, .anchor = 0, .isPrimary = true},
                               Cursor{.position = 5, .anchor = 5, .isPrimary = false}};
    const MultiCursorEditCommand cmd(edits, before);
    EXPECT_EQ(cmd.weight(), ((2U * 2) + 32) + ((1U * 2) + 32));
}

}  // namespace
