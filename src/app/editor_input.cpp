#include "neomifes/app/editor_input.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"

namespace neomifes::app {

namespace {

using core::CommandDispatcher;
using core::Cursor;
using core::DeleteRangeCommand;
using core::InsertTextCommand;
using core::MovementKind;
using core::ReplaceRangeCommand;
using core::SelectionModel;
using core::Viewport;
using document::Document;
using document::TextRange;

[[nodiscard]] TextRange selectionRange(const Cursor& cursor) noexcept {
    return TextRange{.start = std::min(cursor.position, cursor.anchor),
                     .end   = std::max(cursor.position, cursor.anchor)};
}

// Arrow/Home/End navigation. Returns false for any vkCode that isn't a
// movement key (caller then tries other interpretations).
bool applyMovementKey(UINT vkCode, bool shiftDown, bool ctrlDown, SelectionModel& selection,
                      const Document& document) {
    MovementKind kind{};
    switch (vkCode) {
        case VK_LEFT:  kind = MovementKind::Left;  break;
        case VK_RIGHT: kind = MovementKind::Right; break;
        case VK_UP:    kind = MovementKind::Up;    break;
        case VK_DOWN:  kind = MovementKind::Down;  break;
        case VK_HOME:  kind = ctrlDown ? MovementKind::DocumentStart : MovementKind::LineStart; break;
        case VK_END:   kind = ctrlDown ? MovementKind::DocumentEnd   : MovementKind::LineEnd;   break;
        default:
            return false;
    }
    selection.moveAll(kind, document, shiftDown);
    return true;
}

// Backspace/Delete. Deletes the active selection if there is one, regardless
// of which of the two keys was pressed; otherwise deletes one code unit in
// the key's direction, clamped at the document's start/end (no-op there).
bool applyDeleteKey(UINT vkCode, CommandDispatcher& dispatcher, const SelectionModel& selection,
                    const Document& document) {
    const Cursor& cursor = selection.primaryCursor();
    TextRange     range;
    if (cursor.hasSelection()) {
        range = selectionRange(cursor);
    } else if (vkCode == VK_BACK) {
        if (cursor.position == 0) {
            return false;
        }
        range = TextRange{.start = cursor.position - 1, .end = cursor.position};
    } else {
        if (cursor.position >= document.length()) {
            return false;
        }
        range = TextRange{.start = cursor.position, .end = cursor.position + 1};
    }
    dispatcher.dispatch(std::make_unique<DeleteRangeCommand>(range));
    return true;
}

}  // namespace

bool handleKeyDown(UINT vkCode, bool shiftDown, bool ctrlDown, CommandDispatcher& dispatcher,
                   SelectionModel& selection, Viewport& viewport, const Document& document) {
    bool changed = false;
    if (vkCode == VK_BACK || vkCode == VK_DELETE) {
        changed = applyDeleteKey(vkCode, dispatcher, selection, document);
    } else if (ctrlDown && vkCode == 'Z') {
        changed = dispatcher.undo();
    } else if (ctrlDown && vkCode == 'Y') {
        changed = dispatcher.redo();
    } else {
        changed = applyMovementKey(vkCode, shiftDown, ctrlDown, selection, document);
    }
    if (changed) {
        viewport.ensureVisible(selection.primaryCursor().position, document);
    }
    return changed;
}

bool handleChar(wchar_t ch, CommandDispatcher& dispatcher, SelectionModel& selection,
               Viewport& viewport, const Document& document) {
    // Backspace(0x08)/Escape(0x1B)/other C0 controls arrive here too, but
    // WM_KEYDOWN's handleKeyDown already handles Backspace/navigation - only
    // Enter (\r, translated to \n) and Tab are accepted besides printable
    // characters, to avoid double-handling the same keypress.
    if (ch < 0x20 && ch != u'\r' && ch != u'\t') {
        return false;
    }
    auto inserted = static_cast<char16_t>(ch);
    if (ch == u'\r') {
        inserted = u'\n';
    }
    const Cursor&         cursor  = selection.primaryCursor();
    const std::u16string  text(1, inserted);
    if (cursor.hasSelection()) {
        dispatcher.dispatch(std::make_unique<ReplaceRangeCommand>(selectionRange(cursor), text));
    } else {
        dispatcher.dispatch(std::make_unique<InsertTextCommand>(cursor.position, text));
    }
    viewport.ensureVisible(selection.primaryCursor().position, document);
    return true;
}

document::LineNumber applyMouseWheelScroll(short wheelDelta, document::LineNumber currentTopLine) {
    constexpr std::int64_t kLinesPerNotch = 3;
    const std::int64_t     notches        = wheelDelta / WHEEL_DELTA;
    // WM_MOUSEWHEEL convention: positive delta = wheel rotated away from the
    // user ("scroll up" gesture), which reveals earlier lines - topLine
    // should decrease, hence the negation before scaling by lines/notch.
    const std::int64_t linesToScroll = -notches * kLinesPerNotch;
    if (linesToScroll >= 0) {
        return currentTopLine + static_cast<document::LineNumber>(linesToScroll);
    }
    const auto scrollUp = static_cast<document::LineNumber>(-linesToScroll);
    return (currentTopLine >= scrollUp) ? currentTopLine - scrollUp : 0;
}

bool handleMouseDown(document::TextPos pos, bool shiftDown, SelectionModel& selection,
                     Viewport& viewport, const Document& document) {
    selection.moveAllTo(pos, shiftDown);
    viewport.ensureVisible(selection.primaryCursor().position, document);
    return true;
}

bool handleDoubleClick(document::TextPos pos, SelectionModel& selection, Viewport& viewport,
                       const Document& document) {
    selection.selectWordAt(pos, document);
    viewport.ensureVisible(selection.primaryCursor().position, document);
    return true;
}

bool handleTripleClick(document::TextPos pos, SelectionModel& selection, Viewport& viewport,
                       const Document& document) {
    selection.selectLineAt(pos, document);
    viewport.ensureVisible(selection.primaryCursor().position, document);
    return true;
}

}  // namespace neomifes::app
