#include <gtest/gtest.h>

#include "neomifes/document/document.h"
#include "neomifes/document/text_pos.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;

TEST(DocumentVersionTest, StartsAtZero) {
    const Document doc;
    EXPECT_EQ(doc.version(), 0U);
}

TEST(DocumentVersionTest, InsertTextIncrementsByOne) {
    Document doc;
    doc.insertText(0, u"hello");
    EXPECT_EQ(doc.version(), 1U);
    doc.insertText(0, u"x");
    EXPECT_EQ(doc.version(), 2U);
}

TEST(DocumentVersionTest, EraseRangeIncrementsByOne) {
    Document doc;
    doc.insertText(0, u"hello");
    const auto afterInsert = doc.version();
    doc.eraseRange(TextRange{.start = 0, .end = 1});
    EXPECT_EQ(doc.version(), afterInsert + 1);
}

TEST(DocumentVersionTest, ReplaceRangeIncrementsByOne) {
    Document doc;
    doc.insertText(0, u"hello");
    const auto afterInsert = doc.version();
    doc.replaceRange(TextRange{.start = 0, .end = 1}, u"H");
    EXPECT_EQ(doc.version(), afterInsert + 1);
}

TEST(DocumentVersionTest, ReadOnlyCallsDoNotChangeVersion) {
    Document doc;
    doc.insertText(0, u"hello\nworld");
    const auto version = doc.version();

    (void)doc.snapshot();
    (void)doc.length();
    (void)doc.lineCount();
    (void)doc.pieceCount();
    (void)doc.toU16String();
    (void)doc.offsetToLine(2);
    (void)doc.lineToOffset(1);

    EXPECT_EQ(doc.version(), version);
}

// Phase 7k: EditDelta correctness. "line0\nline1\nline2" line starts are
// 0/6/12 (5-char lines + '\n'), used as the fixture across these tests.
TEST(DocumentEditDeltaTest, InsertTextWithinLineRecordsMatchingStartAndEnd) {
    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    (void)doc.takePendingEdits();  // discard the setup edit

    doc.insertText(6, u"X");  // "Xline1" - insert at line1's start
    const auto edits = doc.takePendingEdits();
    ASSERT_EQ(edits.size(), 1u);
    const auto& e = edits[0];
    EXPECT_EQ(e.startPos, 6u);
    EXPECT_EQ(e.startLine, 1u);
    EXPECT_EQ(e.startColumn, 0u);
    EXPECT_EQ(e.oldEndPos, 6u);
    EXPECT_EQ(e.oldEndLine, 1u);
    EXPECT_EQ(e.oldEndColumn, 0u);
    EXPECT_EQ(e.newEndPos, 7u);
    EXPECT_EQ(e.newEndLine, 1u);
    EXPECT_EQ(e.newEndColumn, 1u);
}

TEST(DocumentEditDeltaTest, InsertingNewlineAdvancesNewEndLine) {
    Document doc;
    doc.insertText(0, u"line0\nline1");
    (void)doc.takePendingEdits();

    doc.insertText(2, u"\n");  // splits "line0" into "li" + "ne0"
    const auto edits = doc.takePendingEdits();
    ASSERT_EQ(edits.size(), 1u);
    const auto& e = edits[0];
    EXPECT_EQ(e.startLine, 0u);
    EXPECT_EQ(e.startColumn, 2u);
    EXPECT_EQ(e.oldEndLine, 0u);
    EXPECT_EQ(e.oldEndColumn, 2u);
    EXPECT_EQ(e.newEndLine, 1u);    // now on the line the inserted '\n' created
    EXPECT_EQ(e.newEndColumn, 0u);  // right after the inserted '\n'
}

TEST(DocumentEditDeltaTest, EraseRangeAcrossLinesRecordsOldEndOnLaterLine) {
    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    (void)doc.takePendingEdits();

    doc.eraseRange(TextRange{.start = 3, .end = 8});  // "e0\nli" spanning line0->line1
    const auto edits = doc.takePendingEdits();
    ASSERT_EQ(edits.size(), 1u);
    const auto& e = edits[0];
    EXPECT_EQ(e.startPos, 3u);
    EXPECT_EQ(e.startLine, 0u);
    EXPECT_EQ(e.startColumn, 3u);
    EXPECT_EQ(e.oldEndPos, 8u);
    EXPECT_EQ(e.oldEndLine, 1u);
    EXPECT_EQ(e.oldEndColumn, 2u);
    // Nothing inserted - newEnd collapses back to the (post-edit) start.
    EXPECT_EQ(e.newEndPos, 3u);
    EXPECT_EQ(e.newEndLine, 0u);
    EXPECT_EQ(e.newEndColumn, 3u);
}

TEST(DocumentEditDeltaTest, ReplaceRangeRecordsOldAndNewEnd) {
    Document doc;
    doc.insertText(0, u"line0\nline1");
    (void)doc.takePendingEdits();

    doc.replaceRange(TextRange{.start = 6, .end = 11}, u"XY");  // "line1" -> "XY"
    const auto edits = doc.takePendingEdits();
    ASSERT_EQ(edits.size(), 1u);
    const auto& e = edits[0];
    EXPECT_EQ(e.startPos, 6u);
    EXPECT_EQ(e.startLine, 1u);
    EXPECT_EQ(e.startColumn, 0u);
    EXPECT_EQ(e.oldEndPos, 11u);
    EXPECT_EQ(e.oldEndLine, 1u);
    EXPECT_EQ(e.oldEndColumn, 5u);
    EXPECT_EQ(e.newEndPos, 8u);
    EXPECT_EQ(e.newEndLine, 1u);
    EXPECT_EQ(e.newEndColumn, 2u);
}

TEST(DocumentEditDeltaTest, TakePendingEditsClearsTheList) {
    Document doc;
    doc.insertText(0, u"abc");
    EXPECT_EQ(doc.takePendingEdits().size(), 1u);
    EXPECT_TRUE(doc.takePendingEdits().empty());
}

TEST(DocumentEditDeltaTest, MultipleEditsAccumulateInOrder) {
    // Mirrors what MultiCursorEditCommand produces: several mutations before
    // the next reparse ever drains takePendingEdits().
    Document doc;
    doc.insertText(0, u"a");
    doc.insertText(1, u"b");
    doc.insertText(2, u"c");
    const auto edits = doc.takePendingEdits();
    ASSERT_EQ(edits.size(), 3u);
    EXPECT_EQ(edits[0].startPos, 0u);
    EXPECT_EQ(edits[1].startPos, 1u);
    EXPECT_EQ(edits[2].startPos, 2u);
}

TEST(DocumentEditDeltaTest, UndoLikeEraseAfterInsertIsRecordedTheSameWay) {
    // InsertTextCommand::undo() erases what execute() inserted - same
    // insertText()/eraseRange() call path, so no separate tracking needed.
    Document doc;
    doc.insertText(0, u"hello");
    (void)doc.takePendingEdits();

    doc.eraseRange(TextRange{.start = 0, .end = 5});
    const auto edits = doc.takePendingEdits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].startPos, 0u);
    EXPECT_EQ(edits[0].oldEndPos, 5u);
    EXPECT_EQ(edits[0].newEndPos, 0u);
}

}  // namespace
