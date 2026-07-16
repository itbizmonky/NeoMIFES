#include <gtest/gtest.h>

#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::DeleteRangeCommand;
using neomifes::core::ExecutionContext;
using neomifes::core::InsertTextCommand;
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

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"");
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

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"hello world");
}

TEST(ReplaceRangeCommandTest, ExecuteReplacesThenUndoRestoresOriginal) {
    Document doc;
    doc.insertText(0, u"hello world");
    SelectionModel   selection;
    ExecutionContext ctx(doc, selection);

    ReplaceRangeCommand cmd(TextRange{.start = 0, .end = 5}, u"HELLO");
    cmd.execute(ctx);
    EXPECT_EQ(doc.toU16String(), u"HELLO world");

    cmd.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"hello world");
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

}  // namespace
