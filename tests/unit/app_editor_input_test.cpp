#include <gtest/gtest.h>

#include <windows.h>

#include "neomifes/app/editor_input.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::app::applyMouseWheelScroll;
using neomifes::app::handleChar;
using neomifes::app::handleKeyDown;
using neomifes::app::handleMouseDown;
using neomifes::core::CommandDispatcher;
using neomifes::core::SelectionModel;
using neomifes::core::Viewport;
using neomifes::document::Document;

// Bundles the four objects every handler under test needs, so each TEST body
// only has to declare one fixture-like value instead of four.
struct Env {
    Document          doc;
    SelectionModel    selection{0};
    CommandDispatcher dispatcher{doc, selection};
    Viewport          viewport;
};

TEST(EditorInputTest, ArrowRightMovesCursorAndReportsChanged) {
    Env env;
    env.doc.insertText(0, u"abc");

    const bool changed = handleKeyDown(VK_RIGHT, false, false, env.dispatcher, env.selection,
                                       env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.selection.primaryCursor().position, 1U);
}

TEST(EditorInputTest, ShiftArrowExtendsSelection) {
    Env env;
    env.doc.insertText(0, u"abc");

    handleKeyDown(VK_RIGHT, /*shiftDown=*/true, false, env.dispatcher, env.selection, env.viewport,
                 env.doc);
    EXPECT_TRUE(env.selection.primaryCursor().hasSelection());
    EXPECT_EQ(env.selection.primaryCursor().anchor, 0U);
    EXPECT_EQ(env.selection.primaryCursor().position, 1U);
}

TEST(EditorInputTest, HomeAndCtrlHomeMoveToLineAndDocumentStart) {
    Env env;
    env.doc.insertText(0, u"hello\nworld");
    env.selection.moveAllTo(8);  // 'r' in "world"

    handleKeyDown(VK_HOME, false, /*ctrlDown=*/false, env.dispatcher, env.selection, env.viewport,
                 env.doc);
    EXPECT_EQ(env.selection.primaryCursor().position, 6U);  // start of "world"

    handleKeyDown(VK_HOME, false, /*ctrlDown=*/true, env.dispatcher, env.selection, env.viewport,
                 env.doc);
    EXPECT_EQ(env.selection.primaryCursor().position, 0U);
}

TEST(EditorInputTest, EndAndCtrlEndMoveToLineAndDocumentEnd) {
    Env env;
    env.doc.insertText(0, u"hello\nworld");
    env.selection.moveAllTo(0);

    handleKeyDown(VK_END, false, /*ctrlDown=*/false, env.dispatcher, env.selection, env.viewport,
                 env.doc);
    EXPECT_EQ(env.selection.primaryCursor().position, 5U);  // end of "hello"

    handleKeyDown(VK_END, false, /*ctrlDown=*/true, env.dispatcher, env.selection, env.viewport,
                 env.doc);
    EXPECT_EQ(env.selection.primaryCursor().position, env.doc.length());
}

TEST(EditorInputTest, BackspaceDeletesPrecedingCharacter) {
    Env env;
    env.doc.insertText(0, u"abc");
    env.selection.moveAllTo(2);

    const bool changed = handleKeyDown(VK_BACK, false, false, env.dispatcher, env.selection,
                                       env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"ac");
    EXPECT_EQ(env.selection.primaryCursor().position, 1U);
}

TEST(EditorInputTest, BackspaceAtDocumentStartIsNoOp) {
    Env env;
    env.doc.insertText(0, u"abc");
    env.selection.moveAllTo(0);

    const bool changed = handleKeyDown(VK_BACK, false, false, env.dispatcher, env.selection,
                                       env.viewport, env.doc);
    EXPECT_FALSE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"abc");
}

TEST(EditorInputTest, DeleteRemovesFollowingCharacter) {
    Env env;
    env.doc.insertText(0, u"abc");
    env.selection.moveAllTo(0);

    const bool changed = handleKeyDown(VK_DELETE, false, false, env.dispatcher, env.selection,
                                       env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"bc");
    EXPECT_EQ(env.selection.primaryCursor().position, 0U);
}

TEST(EditorInputTest, DeleteAtDocumentEndIsNoOp) {
    Env env;
    env.doc.insertText(0, u"abc");
    env.selection.moveAllTo(3);

    const bool changed = handleKeyDown(VK_DELETE, false, false, env.dispatcher, env.selection,
                                       env.viewport, env.doc);
    EXPECT_FALSE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"abc");
}

TEST(EditorInputTest, BackspaceWithSelectionDeletesTheSelection) {
    Env env;
    env.doc.insertText(0, u"hello world");
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    ASSERT_TRUE(env.selection.primaryCursor().hasSelection());  // selected "he"

    handleKeyDown(VK_BACK, false, false, env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"llo world");
    EXPECT_EQ(env.selection.primaryCursor().position, 0U);
}

TEST(EditorInputTest, CtrlZUndoesAndCtrlYRedoes) {
    Env env;
    handleChar(u'x', env.dispatcher, env.selection, env.viewport, env.doc);
    ASSERT_EQ(env.doc.toU16String(), u"x");

    const bool undone = handleKeyDown('Z', false, /*ctrlDown=*/true, env.dispatcher, env.selection,
                                      env.viewport, env.doc);
    EXPECT_TRUE(undone);
    EXPECT_EQ(env.doc.toU16String(), u"");

    const bool redone = handleKeyDown('Y', false, /*ctrlDown=*/true, env.dispatcher, env.selection,
                                      env.viewport, env.doc);
    EXPECT_TRUE(redone);
    EXPECT_EQ(env.doc.toU16String(), u"x");
}

TEST(EditorInputTest, PlainZWithoutCtrlIsNotUndo) {
    Env env;
    env.doc.insertText(0, u"abc");
    const bool changed =
        handleKeyDown('Z', false, /*ctrlDown=*/false, env.dispatcher, env.selection, env.viewport,
                     env.doc);
    EXPECT_FALSE(changed);  // 'Z' alone is not a movement key either
}

TEST(EditorInputTest, HandleCharInsertsPrintableCharacterAndAdvancesCursor) {
    Env env;
    const bool changed = handleChar(u'a', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"a");
    EXPECT_EQ(env.selection.primaryCursor().position, 1U);
}

TEST(EditorInputTest, HandleCharReplacesActiveSelection) {
    Env env;
    env.doc.insertText(0, u"hello world");
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    ASSERT_TRUE(env.selection.primaryCursor().hasSelection());  // selected "he"

    handleChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"Xllo world");
    EXPECT_EQ(env.selection.primaryCursor().position, 1U);
}

TEST(EditorInputTest, HandleCharTranslatesCarriageReturnToNewline) {
    Env env;
    const bool changed = handleChar(u'\r', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"\n");
}

TEST(EditorInputTest, HandleCharInsertsTab) {
    Env env;
    handleChar(u'\t', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"\t");
}

TEST(EditorInputTest, HandleCharIgnoresOtherControlCharacters) {
    Env env;
    const bool changed = handleChar(0x01, env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_FALSE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"");
}

TEST(EditorInputTest, ApplyMouseWheelScrollUpDecreasesTopLineClampedToZero) {
    EXPECT_EQ(applyMouseWheelScroll(WHEEL_DELTA, 5), 2U);   // scroll up: -3 lines
    EXPECT_EQ(applyMouseWheelScroll(WHEEL_DELTA, 1), 0U);   // clamped, not negative
}

TEST(EditorInputTest, ApplyMouseWheelScrollDownIncreasesTopLine) {
    EXPECT_EQ(applyMouseWheelScroll(-WHEEL_DELTA, 5), 8U);  // scroll down: +3 lines
}

TEST(EditorInputTest, HandleMouseDownPlacesCursorAndClearsSelection) {
    Env env;
    env.doc.insertText(0, u"hello world");
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    ASSERT_TRUE(env.selection.primaryCursor().hasSelection());  // selected "he"

    const bool changed =
        handleMouseDown(7, /*shiftDown=*/false, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.selection.primaryCursor().position, 7U);
    EXPECT_EQ(env.selection.primaryCursor().anchor, 7U);
    EXPECT_FALSE(env.selection.primaryCursor().hasSelection());
}

TEST(EditorInputTest, HandleMouseDownWithShiftExtendsSelection) {
    Env env;
    env.doc.insertText(0, u"hello world");
    env.selection.moveAllTo(2);

    const bool changed =
        handleMouseDown(8, /*shiftDown=*/true, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.selection.primaryCursor().anchor, 2U);    // unchanged
    EXPECT_EQ(env.selection.primaryCursor().position, 8U);  // moved to click
    EXPECT_TRUE(env.selection.primaryCursor().hasSelection());
}

}  // namespace
