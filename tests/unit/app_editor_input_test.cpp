#include <gtest/gtest.h>

#include <windows.h>

#include "neomifes/app/editor_input.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::app::applyMouseWheelScroll;
using neomifes::app::applyOverwriteChar;
using neomifes::app::deleteAllSelections;
using neomifes::app::dispatchMouseDown;
using neomifes::app::handleAltClick;
using neomifes::app::handleChar;
using neomifes::app::handleDoubleClick;
using neomifes::app::handleKeyDown;
using neomifes::app::handleMouseDown;
using neomifes::app::handlePaste;
using neomifes::app::handleTripleClick;
using neomifes::app::textToCopy;
using neomifes::core::CommandDispatcher;
using neomifes::core::FoldingModel;
using neomifes::core::FoldRegion;
using neomifes::core::SelectionModel;
using neomifes::core::Viewport;
using neomifes::document::Document;
using neomifes::document::TextPos;

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

TEST(EditorInputTest, PageDownAndPageUpJumpByViewportVisibleLineCount) {
    Env env;
    env.doc.insertText(0, u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9");  // 10 single-char lines
    env.viewport.setVisibleLineCount(3);
    env.selection.moveAllTo(10);  // line 5

    const bool downChanged = handleKeyDown(VK_NEXT, false, false, env.dispatcher, env.selection,
                                           env.viewport, env.doc);
    EXPECT_TRUE(downChanged);
    EXPECT_EQ(env.selection.primaryCursor().position, 16U);  // line 5+3=8

    const bool upChanged = handleKeyDown(VK_PRIOR, false, false, env.dispatcher, env.selection,
                                         env.viewport, env.doc);
    EXPECT_TRUE(upChanged);
    EXPECT_EQ(env.selection.primaryCursor().position, 10U);  // line 8-3=5, back to start
}

TEST(EditorInputTest, ShiftPageDownExtendsSelection) {
    Env env;
    env.doc.insertText(0, u"0\n1\n2\n3\n4\n5");
    env.viewport.setVisibleLineCount(2);

    handleKeyDown(VK_NEXT, /*shiftDown=*/true, false, env.dispatcher, env.selection, env.viewport,
                 env.doc);
    EXPECT_EQ(env.selection.primaryCursor().anchor, 0U);
    EXPECT_TRUE(env.selection.primaryCursor().hasSelection());
}

TEST(EditorInputTest, DownArrowSnapsPastFoldedRegionToLineAfterIt) {
    Env env;
    env.doc.insertText(0, u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9");  // 10 single-char lines
    FoldingModel folding;
    folding.setFoldableRegions({FoldRegion{.headerLine = 2, .endLineInclusive = 4, .folded = false}});
    folding.toggleFold(2);  // hides lines 3-4
    env.selection.moveAllTo(env.doc.lineToOffset(2));  // on the (visible) header line itself

    const bool changed =
        handleKeyDown(VK_DOWN, false, false, env.dispatcher, env.selection, env.viewport, env.doc,
                     &folding);
    EXPECT_TRUE(changed);
    // Naive Down would land on line 3 (hidden) - corrected to land on line 5,
    // the first visible line after the fold.
    EXPECT_EQ(env.doc.offsetToLine(env.selection.primaryCursor().position), 5U);
}

TEST(EditorInputTest, UpArrowSnapsPastFoldedRegionToHeaderLine) {
    Env env;
    env.doc.insertText(0, u"0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
    FoldingModel folding;
    folding.setFoldableRegions({FoldRegion{.headerLine = 2, .endLineInclusive = 4, .folded = false}});
    folding.toggleFold(2);  // hides lines 3-4
    env.selection.moveAllTo(env.doc.lineToOffset(5));

    const bool changed =
        handleKeyDown(VK_UP, false, false, env.dispatcher, env.selection, env.viewport, env.doc, &folding);
    EXPECT_TRUE(changed);
    // Naive Up would land on line 4 (hidden) - corrected back to the fold's
    // own header line (2), which stays visible while folded.
    EXPECT_EQ(env.doc.offsetToLine(env.selection.primaryCursor().position), 2U);
}

TEST(EditorInputTest, MovementIntoUnfoldedRegionIsUnaffectedByFoldingModel) {
    Env env;
    env.doc.insertText(0, u"0\n1\n2\n3\n4");
    FoldingModel folding;  // no regions at all
    env.selection.moveAllTo(0);

    const bool changed =
        handleKeyDown(VK_DOWN, false, false, env.dispatcher, env.selection, env.viewport, env.doc,
                     &folding);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.offsetToLine(env.selection.primaryCursor().position), 1U);
}

TEST(EditorInputTest, CtrlLeftAndCtrlRightMoveByWord) {
    Env env;
    env.doc.insertText(0, u"hello world");
    env.selection.moveAllTo(0);

    const bool rightChanged = handleKeyDown(VK_RIGHT, false, /*ctrlDown=*/true, env.dispatcher,
                                            env.selection, env.viewport, env.doc);
    EXPECT_TRUE(rightChanged);
    EXPECT_EQ(env.selection.primaryCursor().position, 6U);  // start of "world"

    const bool leftChanged = handleKeyDown(VK_LEFT, false, /*ctrlDown=*/true, env.dispatcher,
                                           env.selection, env.viewport, env.doc);
    EXPECT_TRUE(leftChanged);
    EXPECT_EQ(env.selection.primaryCursor().position, 0U);  // back to start of "hello"
}

TEST(EditorInputTest, PlainLeftRightWithoutCtrlAreUnaffected) {
    Env env;
    env.doc.insertText(0, u"hello world");
    env.selection.moveAllTo(6);

    handleKeyDown(VK_RIGHT, false, /*ctrlDown=*/false, env.dispatcher, env.selection, env.viewport,
                 env.doc);
    EXPECT_EQ(env.selection.primaryCursor().position, 7U);  // moved by one character, not a word
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

// WI-07 step2: Ctrl+Z/Ctrl+Y moved out of handleKeyDown() into
// normal_mode_wiring.cpp's dispatchCommand() (not headlessly testable here -
// see that file's own Win32-integration testing convention). Both keys now
// fall through to applyMovementKey()'s default case (neither is a
// recognized movement key), so handleKeyDown() itself should report them as
// unhandled regardless of ctrlDown - this is what actually lets
// handleKeyDownEvent()'s explicit Ctrl+Z/Y check (checked BEFORE this
// fallback) claim them without ever reaching here in the real app.
TEST(EditorInputTest, CtrlZAndCtrlYAreNoLongerHandledHere) {
    Env env;
    env.doc.insertText(0, u"abc");

    const bool ctrlZChanged = handleKeyDown('Z', false, /*ctrlDown=*/true, env.dispatcher, env.selection,
                                            env.viewport, env.doc);
    EXPECT_FALSE(ctrlZChanged);

    const bool ctrlYChanged = handleKeyDown('Y', false, /*ctrlDown=*/true, env.dispatcher, env.selection,
                                            env.viewport, env.doc);
    EXPECT_FALSE(ctrlYChanged);
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

TEST(EditorInputTest, HandleCharOnEnterInheritsPreviousLineIndentation) {
    Env env;
    env.doc.insertText(0, u"    abc");
    env.selection.moveAllTo(7);  // end of the line, after 'c'

    const bool changed = handleChar(u'\r', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"    abc\n    ");
}

TEST(EditorInputTest, HandleCharOnEnterInheritsTabIndentationVerbatimWithoutConvertingToSpaces) {
    Env env;
    env.doc.insertText(0, u"\tfoo");
    env.selection.moveAllTo(4);  // end of the line

    handleChar(u'\r', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"\tfoo\n\t");
}

TEST(EditorInputTest, HandleCharOnEnterWithNoLeadingWhitespaceInsertsABareNewline) {
    Env env;
    env.doc.insertText(0, u"abc");
    env.selection.moveAllTo(3);

    handleChar(u'\r', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"abc\n");
}

TEST(EditorInputTest, HandleCharOnEnterGivesEachCursorItsOwnLinesIndentation) {
    Env env;
    env.doc.insertText(0, u"  foo\n    bar");
    env.selection.moveAllTo(5);                          // end of line0 ("  foo")
    env.selection.addCursor(13);                         // end of line1 ("    bar")

    handleChar(u'\r', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"  foo\n  \n    bar\n    ");
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

// WI-07 step5: applyOverwriteChar() - overwrites the character right after
// the cursor when one exists on the same line.
TEST(EditorInputTest, ApplyOverwriteCharReplacesTheCharacterAfterTheCursor) {
    Env env;
    env.doc.insertText(0, u"abc");
    env.selection.moveAllTo(1);  // between 'a' and 'b'

    const bool changed = applyOverwriteChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"aXc");
    EXPECT_EQ(env.selection.primaryCursor().position, 2U);
}

// At end of line (no character left to overwrite on this line), falls back
// to a plain insert rather than eating into the next line's content.
TEST(EditorInputTest, ApplyOverwriteCharAtLineEndFallsBackToInsert) {
    Env env;
    env.doc.insertText(0, u"ab\ncd");
    env.selection.moveAllTo(2);  // end of "ab", right before '\n'

    const bool changed = applyOverwriteChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"abX\ncd");
    EXPECT_EQ(env.selection.primaryCursor().position, 3U);
}

// At end of document, same fallback (nothing after the cursor at all).
TEST(EditorInputTest, ApplyOverwriteCharAtDocumentEndFallsBackToInsert) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(2);

    const bool changed = applyOverwriteChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"abX");
    EXPECT_EQ(env.selection.primaryCursor().position, 3U);
}

// An active selection is replaced (not overwritten-plus-one), identical to
// handleChar()'s own replace-selection behavior.
TEST(EditorInputTest, ApplyOverwriteCharReplacesActiveSelection) {
    Env env;
    env.doc.insertText(0, u"hello world");
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    ASSERT_TRUE(env.selection.primaryCursor().hasSelection());  // selected "he"

    applyOverwriteChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"Xllo world");
    EXPECT_EQ(env.selection.primaryCursor().position, 1U);
}

// Enter never overwrites/eats a line break - always falls back to
// handleChar()'s plain-insert behavior via insertTextAtEveryCursor().
TEST(EditorInputTest, ApplyOverwriteCharTranslatesCarriageReturnToPlainNewlineInsert) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(1);  // between 'a' and 'b'

    const bool changed = applyOverwriteChar(u'\r', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"a\nb");  // inserted, 'b' NOT overwritten
}

// Tab likewise always inserts rather than overwriting.
TEST(EditorInputTest, ApplyOverwriteCharInsertsTabRatherThanOverwriting) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(1);

    const bool changed = applyOverwriteChar(u'\t', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"a\tb");
}

TEST(EditorInputTest, ApplyOverwriteCharIgnoresOtherControlCharacters) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(1);

    const bool changed = applyOverwriteChar(0x01, env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_FALSE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"ab");
}

// Multiple cursors each independently overwrite (or fall back to insert)
// based on their OWN position, same per-cursor independence handleChar()'s
// multi-cursor test above verifies.
TEST(EditorInputTest, ApplyOverwriteCharWithMultipleCursorsOverwritesEachIndependently) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.moveAllTo(1);  // between 'a' and 'b'
    env.selection.addCursor(5);  // end of document (falls back to insert)
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = applyOverwriteChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"aX cdX");
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 2U);
    EXPECT_EQ(env.selection.cursors()[1].position, 6U);
}

TEST(EditorInputTest, ApplyMouseWheelScrollUpDecreasesTopLineClampedToZero) {
    EXPECT_EQ(applyMouseWheelScroll(WHEEL_DELTA, 5, 1000), 2U);   // scroll up: -3 lines
    EXPECT_EQ(applyMouseWheelScroll(WHEEL_DELTA, 1, 1000), 0U);   // clamped, not negative
}

TEST(EditorInputTest, ApplyMouseWheelScrollDownIncreasesTopLine) {
    EXPECT_EQ(applyMouseWheelScroll(-WHEEL_DELTA, 5, 1000), 8U);  // scroll down: +3 lines
}

TEST(EditorInputTest, ApplyMouseWheelScrollDownClampsToLastLineNearEof) {
    // totalLines=10 -> maxTopLine=9; scrolling down from 8 would otherwise
    // reach 11, but must clamp to 9 (the bug WI-02 dogfooding reported:
    // Viewport::topLine() growing past what render-time clamping displays).
    EXPECT_EQ(applyMouseWheelScroll(-WHEEL_DELTA, 8, 10), 9U);
}

TEST(EditorInputTest, ApplyMouseWheelScrollDownWithZeroTotalLinesClampsToZero) {
    EXPECT_EQ(applyMouseWheelScroll(-WHEEL_DELTA, 0, 0), 0U);
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

TEST(EditorInputTest, RepeatedShiftedMouseDownSimulatesDragExtendingFromOriginalAnchor) {
    // Phase 4b3: drag-select is implemented purely as MainWindow Win32
    // plumbing (SetCapture + WM_MOUSEMOVE) that calls handleMouseDown with
    // shiftDown=true repeatedly - no new core/app logic. This test pins
    // down the behavior that design relies on: a plain mouse-down
    // establishes the anchor, and every subsequent "extend" call keeps that
    // same anchor no matter how many times it's called or how far position
    // moves - simulating a multi-point drag.
    Env env;
    env.doc.insertText(0, u"hello world");

    handleMouseDown(3, /*shiftDown=*/false, env.selection, env.viewport, env.doc);  // drag start
    ASSERT_EQ(env.selection.primaryCursor().anchor, 3U);
    ASSERT_FALSE(env.selection.primaryCursor().hasSelection());

    handleMouseDown(5, /*shiftDown=*/true, env.selection, env.viewport, env.doc);  // first move
    EXPECT_EQ(env.selection.primaryCursor().anchor, 3U);
    EXPECT_EQ(env.selection.primaryCursor().position, 5U);

    handleMouseDown(9, /*shiftDown=*/true, env.selection, env.viewport, env.doc);  // further move
    EXPECT_EQ(env.selection.primaryCursor().anchor, 3U);    // still the drag start
    EXPECT_EQ(env.selection.primaryCursor().position, 9U);  // tracks latest point
    EXPECT_TRUE(env.selection.primaryCursor().hasSelection());

    handleMouseDown(1, /*shiftDown=*/true, env.selection, env.viewport, env.doc);  // move back past start
    EXPECT_EQ(env.selection.primaryCursor().anchor, 3U);    // anchor never moves mid-drag
    EXPECT_EQ(env.selection.primaryCursor().position, 1U);
}

TEST(EditorInputTest, HandleDoubleClickSelectsWordAtPosition) {
    Env env;
    env.doc.insertText(0, u"hello world");

    const bool changed = handleDoubleClick(2, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.selection.primaryCursor().anchor, 0U);
    EXPECT_EQ(env.selection.primaryCursor().position, 5U);
}

TEST(EditorInputTest, HandleTripleClickSelectsLineAtPosition) {
    Env env;
    env.doc.insertText(0, u"line0\nline1\nline2");

    const bool changed = handleTripleClick(8, env.selection, env.viewport, env.doc);  // in "line1"
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.selection.primaryCursor().anchor, 6U);
    EXPECT_EQ(env.selection.primaryCursor().position, 12U);  // includes trailing '\n'
}

TEST(EditorInputTest, HandleAltClickAddsNewCursorWithoutDisturbingThePrimary) {
    Env env;
    env.doc.insertText(0, u"hello world");

    const bool changed = handleAltClick(6, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 0U);  // untouched primary
    EXPECT_EQ(env.selection.cursors()[1].position, 6U);  // new cursor at the click
}

// WI-27 (rectangular_anchor_stale_across_keyboard_only_reuse.md): dispatchMouseDown()
// itself already resets rectangularAnchor/altCursorAnchor correctly on the mouse
// side (editor_input.cpp:447,455-456) but had zero test coverage before this WI -
// these lock in that existing behavior as a safety net for half of the invariant
// the WI-27 keyboard-side fix (normal_mode_wiring.cpp, not unit-testable) depends on.
TEST(EditorInputTest, DispatchMouseDownPlainClickResetsBothAnchors) {
    Env env;
    env.doc.insertText(0, u"hello world");
    std::optional<TextPos> altCursorAnchor    = 3U;
    std::optional<TextPos> rectangularAnchor = 5U;

    const bool changed = dispatchMouseDown(0, /*shiftDown=*/false, /*altDown=*/false, /*clickCount=*/1,
                                           env.selection, env.viewport, env.doc, altCursorAnchor,
                                           rectangularAnchor);
    EXPECT_TRUE(changed);
    EXPECT_FALSE(altCursorAnchor.has_value());
    EXPECT_FALSE(rectangularAnchor.has_value());
}

TEST(EditorInputTest, DispatchMouseDownPlainAltClickResetsRectangularAnchorButSetsAltCursorAnchor) {
    Env env;
    env.doc.insertText(0, u"hello world");
    std::optional<TextPos> altCursorAnchor    = std::nullopt;
    std::optional<TextPos> rectangularAnchor = 5U;

    const bool changed = dispatchMouseDown(6, /*shiftDown=*/false, /*altDown=*/true, /*clickCount=*/1,
                                           env.selection, env.viewport, env.doc, altCursorAnchor,
                                           rectangularAnchor);
    EXPECT_TRUE(changed);
    EXPECT_FALSE(rectangularAnchor.has_value());
    ASSERT_TRUE(altCursorAnchor.has_value());
    EXPECT_EQ(*altCursorAnchor, 6U);
}

TEST(EditorInputTest, DispatchMouseDownShiftAltClickSetsRectangularAnchor) {
    Env env;
    env.doc.insertText(0, u"hello world");
    std::optional<TextPos> altCursorAnchor    = std::nullopt;
    std::optional<TextPos> rectangularAnchor = std::nullopt;

    dispatchMouseDown(6, /*shiftDown=*/true, /*altDown=*/true, /*clickCount=*/1, env.selection, env.viewport,
                      env.doc, altCursorAnchor, rectangularAnchor);
    ASSERT_TRUE(rectangularAnchor.has_value());
    EXPECT_EQ(*rectangularAnchor, 6U);
}

TEST(EditorInputTest, HandleCharWithMultipleCursorsInsertsAtEachCursor) {
    // Phase 4b5b end-to-end: two cursors (0 and 3 in "ab cd", right before
    // 'a' and right before 'c') both receive the typed character, and the
    // second cursor's final position accounts for the shift the first
    // cursor's insert introduced (MultiCursorEditCommand's cumulative-offset
    // math, exercised here through the actual handleChar/dispatcher path).
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.addCursor(3);
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = handleChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"Xab Xcd");
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 1U);
    EXPECT_EQ(env.selection.cursors()[1].position, 5U);
}

TEST(EditorInputTest, BackspaceWithMultipleCursorsDeletesAtEachCursor) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.moveAllTo(1);  // right after 'a'
    env.selection.addCursor(4);  // right after 'c'
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = handleKeyDown(VK_BACK, false, false, env.dispatcher, env.selection,
                                       env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"b d");
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 0U);
    EXPECT_EQ(env.selection.cursors()[1].position, 2U);
}

TEST(EditorInputTest, BackspaceWithOneCursorAtStartStillDeletesForOtherCursors) {
    // A cursor that can't move (document start) contributes a no-op edit but
    // doesn't block the rest - only "every cursor is a no-op" suppresses the
    // dispatch entirely (see BackspaceAtDocumentStartIsNoOp for that case).
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(0);
    env.selection.addCursor(2);  // end of "ab"
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = handleKeyDown(VK_BACK, false, false, env.dispatcher, env.selection,
                                       env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"a");
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 0U);  // unchanged, was already at start
    EXPECT_EQ(env.selection.cursors()[1].position, 1U);
}

TEST(EditorInputTest, TextToCopyReturnsPrimarySelectionText) {
    Env env;
    env.doc.insertText(0, u"hello world");
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    ASSERT_TRUE(env.selection.primaryCursor().hasSelection());  // selected "he"

    const auto text = textToCopy(env.selection, env.doc);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, u"he");
}

TEST(EditorInputTest, TextToCopyReturnsNulloptWithNoSelection) {
    Env env;
    env.doc.insertText(0, u"hello world");
    env.selection.moveAllTo(3);

    EXPECT_FALSE(textToCopy(env.selection, env.doc).has_value());
}

TEST(EditorInputTest, HandlePasteInsertsAtCursorWithNoSelection) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(1);

    const bool changed = handlePaste(u"XYZ", env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"aXYZb");
    EXPECT_EQ(env.selection.primaryCursor().position, 4U);
}

TEST(EditorInputTest, HandlePasteReplacesActiveSelection) {
    Env env;
    env.doc.insertText(0, u"hello world");
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    handleKeyDown(VK_RIGHT, true, false, env.dispatcher, env.selection, env.viewport, env.doc);
    ASSERT_TRUE(env.selection.primaryCursor().hasSelection());  // selected "he"

    handlePaste(u"HE", env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_EQ(env.doc.toU16String(), u"HEllo world");
    EXPECT_FALSE(env.selection.primaryCursor().hasSelection());
}

TEST(EditorInputTest, TextToCopyJoinsMultipleSelectionsWithNewline) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.moveAllTo(2, /*extendSelection=*/false);
    env.selection.moveAllTo(0, /*extendSelection=*/true);  // primary selects "ab" (0-2)
    env.selection.addCursor(5);
    env.selection.moveCursorMatching(/*identifyingAnchor=*/5, /*newPos=*/3);  // second selects "cd" (3-5)
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const auto text = textToCopy(env.selection, env.doc);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, u"ab\ncd");
}

TEST(EditorInputTest, TextToCopySkipsCursorsWithoutSelection) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.moveAllTo(0);        // no selection
    env.selection.addCursor(5);
    env.selection.moveCursorMatching(5, 3);  // selects "cd" (3-5)
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const auto text = textToCopy(env.selection, env.doc);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, u"cd");
}

TEST(EditorInputTest, HandlePasteInsertsAtEveryCursor) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.addCursor(3);  // primary at 0, second right before 'c'
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = handlePaste(u"X", env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"Xab Xcd");
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 1U);
    EXPECT_EQ(env.selection.cursors()[1].position, 5U);
}

// Phase 4b8f: N:N distribution - chunk count equals cursor count, so each
// cursor gets its own corresponding line rather than the whole pasted text.
TEST(EditorInputTest, HandlePasteDistributesOneChunkPerCursorWhenCountsMatch) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.addCursor(3);  // primary at 0, second right before 'c'
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = handlePaste(u"X\nY", env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"Xab Ycd");
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 1U);
    EXPECT_EQ(env.selection.cursors()[1].position, 5U);
}

// Phase 4b8f: chunk count (3) does not match cursor count (2) - falls back
// to inserting the whole pasted text identically at every cursor, same as
// HandlePasteInsertsAtEveryCursor above but proving the >1-cursor mismatch
// path specifically (not just the single-cursor case below).
TEST(EditorInputTest, HandlePasteFallsBackToWholeTextWhenChunkAndCursorCountsMismatch) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.addCursor(2);  // primary at 0, second at end (after "ab")
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = handlePaste(u"P\nQ\nR", env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"P\nQ\nRabP\nQ\nR");
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    EXPECT_EQ(env.selection.cursors()[0].position, 5U);
    EXPECT_EQ(env.selection.cursors()[1].position, 12U);
}

// Phase 4b8f non-regression: a single cursor pasting multi-line text is
// inherently a chunk/cursor count mismatch (N chunks vs. 1 cursor), so it
// must keep inserting the whole text verbatim exactly as before this phase.
TEST(EditorInputTest, HandlePasteInsertsWholeMultilineTextAtSingleCursorUnchanged) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(1);

    const bool changed = handlePaste(u"X\nY\nZ", env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"aX\nY\nZb");
    EXPECT_EQ(env.selection.primaryCursor().position, 6U);  // right after "X\nY\nZ"
}

TEST(EditorInputTest, DeleteAllSelectionsDeletesEachCursorsSelection) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.moveAllTo(2, false);
    env.selection.moveAllTo(0, true);  // primary selects "ab"
    env.selection.addCursor(5);
    env.selection.moveCursorMatching(5, 3);  // second selects "cd"
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = deleteAllSelections(env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u" ");
}

TEST(EditorInputTest, DeleteAllSelectionsLeavesCursorsWithoutSelectionUntouched) {
    Env env;
    env.doc.insertText(0, u"ab cd");
    env.selection.moveAllTo(0);  // primary: no selection
    env.selection.addCursor(5);
    env.selection.moveCursorMatching(5, 3);  // second selects "cd"
    ASSERT_EQ(env.selection.cursors().size(), 2U);

    const bool changed = deleteAllSelections(env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"ab ");
}

TEST(EditorInputTest, DeleteAllSelectionsReturnsFalseWithNoSelectionsAtAll) {
    Env env;
    env.doc.insertText(0, u"ab");
    env.selection.moveAllTo(1);

    const bool changed = deleteAllSelections(env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_FALSE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"ab");
}

// WI-26 (縦編集/列編集): setRectangularSelection() itself is only unit-tested
// in isolation (core_selection_model_test.cpp) - these four exercise it
// through the actual editing handlers above, the combination a WI-26 code
// audit found was never covered end-to-end despite the underlying machinery
// (MultiCursorEditCommand with real per-cursor selections) being generic and
// correct. "aaa\nbbb\nccc": a(0)a(1)a(2)\n(3)b(4)b(5)b(6)\n(7)c(8)c(9)c(10).
TEST(EditorInputTest, RectangularSelectionColumnInsertTypesIndependentlyOnEachRow) {
    Env env;
    env.doc.insertText(0, u"aaa\nbbb\nccc");
    env.selection.setRectangularSelection(1, 5, env.doc);  // col 1, rows 0-1, zero-width
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    ASSERT_FALSE(env.selection.cursors()[0].hasSelection());
    ASSERT_FALSE(env.selection.cursors()[1].hasSelection());

    const bool changed = handleChar(u'X', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"aXaa\nbXbb\nccc");  // row 2 untouched
}

// "abcd\nefgh\nijkl": a(0)b(1)c(2)d(3)\n(4)e(5)f(6)g(7)h(8)\n(9)i(10)j(11)k(12)l(13).
TEST(EditorInputTest, RectangularSelectionColumnDeleteRemovesOnlyEachRowsOwnSpan) {
    Env env;
    env.doc.insertText(0, u"abcd\nefgh\nijkl");
    env.selection.setRectangularSelection(1, 8, env.doc);  // cols [1,3), rows 0-1: "bc"/"fg"
    ASSERT_EQ(env.selection.cursors().size(), 2U);
    ASSERT_TRUE(env.selection.cursors()[0].hasSelection());
    ASSERT_TRUE(env.selection.cursors()[1].hasSelection());

    const bool changed =
        handleKeyDown(VK_DELETE, false, false, env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"ad\neh\nijkl");  // row 2 untouched
}

TEST(EditorInputTest, RectangularSelectionColumnOverwriteReplacesEachRowsSpanWithTypedChar) {
    Env env;
    env.doc.insertText(0, u"abcd\nefgh\nijkl");
    env.selection.setRectangularSelection(1, 8, env.doc);  // cols [1,3), rows 0-1: "bc"/"fg"

    const bool changed = handleChar(u'Z', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"aZd\neZh\nijkl");  // row 2 untouched
}

// A row shorter than the rectangle's right edge gets its column clamped to
// that row's own length by setRectangularSelection() itself (no padding) -
// "xy" (row 1, length 2) clamps cols [2,5) down to an empty, collapsed
// selection sitting exactly at its own end, rather than crashing or being
// padded out to width 5. "abcdef\nxy\nghijkl":
// a(0)b(1)c(2)d(3)e(4)f(5)\n(6)x(7)y(8)\n(9)g(10)h(11)i(12)j(13)k(14)l(15).
TEST(EditorInputTest, RectangularSelectionClampsShortRowToCollapsedCursorNoPadding) {
    Env env;
    env.doc.insertText(0, u"abcdef\nxy\nghijkl");
    env.selection.setRectangularSelection(2, 10 + 5, env.doc);  // cols [2,5), rows 0-2 (row 2 starts at 10)
    ASSERT_EQ(env.selection.cursors().size(), 3U);
    EXPECT_FALSE(env.selection.cursors()[1].hasSelection());  // "xy" row: clamped, collapsed

    const bool changed = handleChar(u'Z', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"abZf\nxyZ\nghZl");
}

// column.append's whole reason for existing: unlike the three tests above,
// append must land at each row's REAL end, decoupled from the rectangle's
// own column - verified here with three very differently-sized rows (2, 6,
// 1 chars) so a bug that used the rectangle's column instead of the row's
// real end would visibly insert text in the wrong place on at least one row.
// "ab\nabcdef\na": a(0)b(1)\n(2)a(3)b(4)c(5)d(6)e(7)f(8)\n(9)a(10).
TEST(EditorInputTest, RectangularSelectionThenConvertToLineEndCursorsAppendsAtEachRowsRealEnd) {
    Env env;
    env.doc.insertText(0, u"ab\nabcdef\na");
    env.selection.setRectangularSelection(1, 11, env.doc);  // col 1, rows 0-2
    ASSERT_EQ(env.selection.cursors().size(), 3U);

    env.selection.convertToLineEndCursors(env.doc);
    const bool changed = handleChar(u'Z', env.dispatcher, env.selection, env.viewport, env.doc);
    EXPECT_TRUE(changed);
    EXPECT_EQ(env.doc.toU16String(), u"abZ\nabcdefZ\naZ");
}

}  // namespace
