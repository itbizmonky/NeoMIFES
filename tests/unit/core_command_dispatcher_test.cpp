#include <gtest/gtest.h>

#include <memory>

#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::core::CommandDispatcher;
using neomifes::core::DeleteRangeCommand;
using neomifes::core::InsertTextCommand;
using neomifes::core::SelectionModel;
using neomifes::document::Document;
using neomifes::document::TextRange;

TEST(CommandDispatcherTest, DispatchExecutesImmediately) {
    Document          doc;
    SelectionModel    selection;
    CommandDispatcher dispatcher(doc, selection);

    dispatcher.dispatch(std::make_unique<InsertTextCommand>(0, u"hello"));
    EXPECT_EQ(doc.toU16String(), u"hello");
    EXPECT_TRUE(dispatcher.canUndo());
    EXPECT_FALSE(dispatcher.canRedo());
}

TEST(CommandDispatcherTest, UndoRedoRoundTrip) {
    Document          doc;
    SelectionModel    selection;
    CommandDispatcher dispatcher(doc, selection);

    dispatcher.dispatch(std::make_unique<InsertTextCommand>(0, u"hello"));
    ASSERT_TRUE(dispatcher.undo());
    EXPECT_EQ(doc.toU16String(), u"");
    EXPECT_TRUE(dispatcher.canRedo());

    ASSERT_TRUE(dispatcher.redo());
    EXPECT_EQ(doc.toU16String(), u"hello");
}

TEST(CommandDispatcherTest, UndoOnEmptyHistoryReturnsFalse) {
    Document          doc;
    SelectionModel    selection;
    CommandDispatcher dispatcher(doc, selection);
    EXPECT_FALSE(dispatcher.undo());
}

TEST(CommandDispatcherTest, DispatchUndoRedoMoveCursorToCommandReportedPositions) {
    Document          doc;
    SelectionModel    selection(0);
    CommandDispatcher dispatcher(doc, selection);

    dispatcher.dispatch(std::make_unique<InsertTextCommand>(0, u"hello"));
    EXPECT_EQ(selection.primaryCursor().position, 5U);  // end of inserted text

    ASSERT_TRUE(dispatcher.undo());
    EXPECT_EQ(selection.primaryCursor().position, 0U);  // back to insertion point

    ASSERT_TRUE(dispatcher.redo());
    EXPECT_EQ(selection.primaryCursor().position, 5U);  // end of inserted text again
}

TEST(CommandDispatcherTest, MultipleCommandsUndoInReverseOrder) {
    Document          doc;
    SelectionModel    selection;
    CommandDispatcher dispatcher(doc, selection);

    dispatcher.dispatch(std::make_unique<InsertTextCommand>(0, u"ab"));
    dispatcher.dispatch(std::make_unique<DeleteRangeCommand>(TextRange{.start = 0, .end = 1}));
    ASSERT_EQ(doc.toU16String(), u"b");

    ASSERT_TRUE(dispatcher.undo());
    EXPECT_EQ(doc.toU16String(), u"ab");

    ASSERT_TRUE(dispatcher.undo());
    EXPECT_EQ(doc.toU16String(), u"");
    EXPECT_FALSE(dispatcher.canUndo());
}

}  // namespace
