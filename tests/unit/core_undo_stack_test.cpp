#include <gtest/gtest.h>

#include <memory>

#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/undo_stack.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::ExecutionContext;
using neomifes::core::InsertTextCommand;
using neomifes::core::SelectionModel;
using neomifes::core::UndoStack;
using neomifes::document::Document;

TEST(UndoStackTest, StartsEmpty) {
    const UndoStack stack;
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
    EXPECT_EQ(stack.undoCount(), 0U);
    EXPECT_EQ(stack.redoCount(), 0U);
}

TEST(UndoStackTest, PushMakesCanUndoTrueAndClearsRedo) {
    UndoStack stack;
    stack.push(std::make_unique<InsertTextCommand>(0, u"a"));
    EXPECT_TRUE(stack.canUndo());
    EXPECT_EQ(stack.undoCount(), 1U);
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStackTest, UndoOnEmptyStackReturnsFalse) {
    Document          doc;
    SelectionModel    selection;
    ExecutionContext  ctx(doc, selection);
    UndoStack         stack;
    EXPECT_FALSE(stack.undo(ctx));
}

TEST(UndoStackTest, RedoOnEmptyStackReturnsFalse) {
    Document          doc;
    SelectionModel    selection;
    ExecutionContext  ctx(doc, selection);
    UndoStack         stack;
    EXPECT_FALSE(stack.redo(ctx));
}

TEST(UndoStackTest, UndoInvertsAndMovesToRedoStack) {
    Document          doc;
    SelectionModel    selection;
    ExecutionContext  ctx(doc, selection);
    UndoStack         stack;

    auto cmd = std::make_unique<InsertTextCommand>(0, u"hello");
    cmd->execute(ctx);
    stack.push(std::move(cmd));
    EXPECT_EQ(doc.toU16String(), u"hello");

    EXPECT_TRUE(stack.undo(ctx));
    EXPECT_EQ(doc.toU16String(), u"");
    EXPECT_FALSE(stack.canUndo());
    EXPECT_TRUE(stack.canRedo());
}

TEST(UndoStackTest, RedoReappliesCommand) {
    Document          doc;
    SelectionModel    selection;
    ExecutionContext  ctx(doc, selection);
    UndoStack         stack;

    auto cmd = std::make_unique<InsertTextCommand>(0, u"hello");
    cmd->execute(ctx);
    stack.push(std::move(cmd));
    stack.undo(ctx);
    ASSERT_EQ(doc.toU16String(), u"");

    EXPECT_TRUE(stack.redo(ctx));
    EXPECT_EQ(doc.toU16String(), u"hello");
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStackTest, NewPushAfterUndoDiscardsRedoHistory) {
    Document          doc;
    SelectionModel    selection;
    ExecutionContext  ctx(doc, selection);
    UndoStack         stack;

    auto cmd1 = std::make_unique<InsertTextCommand>(0, u"a");
    cmd1->execute(ctx);
    stack.push(std::move(cmd1));
    stack.undo(ctx);
    ASSERT_TRUE(stack.canRedo());

    auto cmd2 = std::make_unique<InsertTextCommand>(0, u"b");
    cmd2->execute(ctx);
    stack.push(std::move(cmd2));

    EXPECT_FALSE(stack.canRedo());
    EXPECT_EQ(doc.toU16String(), u"b");
}

TEST(UndoStackTest, MultipleUndoRedoRoundTripRestoresExactContent) {
    Document          doc;
    SelectionModel    selection;
    ExecutionContext  ctx(doc, selection);
    UndoStack         stack;

    for (const std::u16string& text : {std::u16string(u"a"), std::u16string(u"b"), std::u16string(u"c")}) {
        auto cmd = std::make_unique<InsertTextCommand>(doc.length(), text);
        cmd->execute(ctx);
        stack.push(std::move(cmd));
    }
    ASSERT_EQ(doc.toU16String(), u"abc");

    stack.undo(ctx);
    stack.undo(ctx);
    stack.undo(ctx);
    EXPECT_EQ(doc.toU16String(), u"");
    EXPECT_FALSE(stack.canUndo());

    stack.redo(ctx);
    stack.redo(ctx);
    stack.redo(ctx);
    EXPECT_EQ(doc.toU16String(), u"abc");
    EXPECT_FALSE(stack.canRedo());
}

// Phase 5c2 ("open a different file at runtime"): clear().
TEST(UndoStackTest, ClearOnEmptyStackIsNoop) {
    UndoStack stack;
    stack.clear();
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStackTest, ClearDiscardsUndoHistory) {
    UndoStack stack;
    stack.push(std::make_unique<InsertTextCommand>(0, u"a"));
    ASSERT_TRUE(stack.canUndo());

    stack.clear();
    EXPECT_FALSE(stack.canUndo());
    EXPECT_EQ(stack.undoCount(), 0U);
}

TEST(UndoStackTest, ClearDiscardsRedoHistory) {
    Document          doc;
    SelectionModel    selection;
    ExecutionContext  ctx(doc, selection);
    UndoStack         stack;

    auto cmd = std::make_unique<InsertTextCommand>(0, u"a");
    cmd->execute(ctx);
    stack.push(std::move(cmd));
    stack.undo(ctx);
    ASSERT_TRUE(stack.canRedo());

    stack.clear();
    EXPECT_FALSE(stack.canRedo());
    EXPECT_EQ(stack.redoCount(), 0U);
}

}  // namespace
