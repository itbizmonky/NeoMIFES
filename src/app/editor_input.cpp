#include "neomifes/app/editor_input.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"

namespace neomifes::app {

namespace {

using core::CommandDispatcher;
using core::Cursor;
using core::MovementKind;
using core::MultiCursorEditCommand;
using core::PerCursorEdit;
using core::SelectionModel;
using core::Viewport;
using document::Document;
using document::TextRange;

[[nodiscard]] TextRange selectionRange(const Cursor& cursor) noexcept {
    return TextRange{.start = std::min(cursor.position, cursor.anchor),
                     .end   = std::max(cursor.position, cursor.anchor)};
}

// Arrow/Home/End/PageUp/PageDown navigation. Returns false for any vkCode
// that isn't a movement key (caller then tries other interpretations).
// `pageSize` (Phase 4b6a) is only consulted for PageUp/PageDown - callers
// pass the viewport's visible line count.
bool applyMovementKey(UINT vkCode, bool shiftDown, bool ctrlDown, SelectionModel& selection,
                      const Document& document, document::LineNumber pageSize) {
    MovementKind kind{};
    switch (vkCode) {
        case VK_LEFT:  kind = MovementKind::Left;  break;
        case VK_RIGHT: kind = MovementKind::Right; break;
        case VK_UP:    kind = MovementKind::Up;    break;
        case VK_DOWN:  kind = MovementKind::Down;  break;
        case VK_HOME:  kind = ctrlDown ? MovementKind::DocumentStart : MovementKind::LineStart; break;
        case VK_END:   kind = ctrlDown ? MovementKind::DocumentEnd   : MovementKind::LineEnd;   break;
        case VK_PRIOR: kind = MovementKind::PageUp;   break;
        case VK_NEXT:  kind = MovementKind::PageDown; break;
        default:
            return false;
    }
    selection.moveAll(kind, document, shiftDown, pageSize);
    return true;
}

// Backspace/Delete, applied to every cursor (Phase 4b5b). Deletes each
// cursor's active selection if it has one, regardless of which of the two
// keys was pressed; otherwise deletes one code unit in the key's direction.
// A cursor at the document's start (Backspace) or end (Delete) with no
// selection contributes an empty (no-op) edit rather than being dropped from
// the edit list - MultiCursorEditCommand expects exactly one PerCursorEdit
// per cursor, 1:1 by index (see edit_commands.h). If every cursor's edit
// ends up empty, nothing is dispatched (mirrors the single-cursor "nothing
// happened" early return this replaced).
bool applyDeleteKey(UINT vkCode, CommandDispatcher& dispatcher, const SelectionModel& selection,
                    const Document& document) {
    std::vector<Cursor> before(selection.cursors().begin(), selection.cursors().end());
    std::vector<PerCursorEdit> edits;
    edits.reserve(before.size());
    bool anyChange = false;
    for (const Cursor& cursor : before) {
        TextRange range{.start = cursor.position, .end = cursor.position};
        if (cursor.hasSelection()) {
            range = selectionRange(cursor);
        } else if (vkCode == VK_BACK) {
            if (cursor.position > 0) {
                range = TextRange{.start = cursor.position - 1, .end = cursor.position};
            }
        } else if (cursor.position < document.length()) {
            range = TextRange{.start = cursor.position, .end = cursor.position + 1};
        }
        anyChange = anyChange || !range.empty();
        edits.push_back(PerCursorEdit{.range = range, .insertedText = u""});
    }
    if (!anyChange) {
        return false;
    }
    dispatcher.dispatch(std::make_unique<MultiCursorEditCommand>(std::move(edits), std::move(before)));
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
        const auto visible = viewport.visibleLines();
        changed = applyMovementKey(vkCode, shiftDown, ctrlDown, selection, document,
                                   visible.end - visible.start);
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
    const std::u16string text(1, inserted);

    // Every cursor gets the same inserted text (Phase 4b5b): a plain insert
    // at cursors with no selection, or a replace at cursors that have one -
    // same per-cursor rule as applyDeleteKey above, generalizing the
    // pre-4b5b single-cursor InsertTextCommand/ReplaceRangeCommand dispatch.
    std::vector<Cursor> before(selection.cursors().begin(), selection.cursors().end());
    std::vector<PerCursorEdit> edits;
    edits.reserve(before.size());
    for (const Cursor& cursor : before) {
        const TextRange range = cursor.hasSelection()
                                    ? selectionRange(cursor)
                                    : TextRange{.start = cursor.position, .end = cursor.position};
        edits.push_back(PerCursorEdit{.range = range, .insertedText = text});
    }
    dispatcher.dispatch(std::make_unique<MultiCursorEditCommand>(std::move(edits), std::move(before)));
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

bool handleAltClick(document::TextPos pos, SelectionModel& selection, Viewport& viewport,
                    const Document& document) {
    selection.addCursor(pos);
    viewport.ensureVisible(pos, document);
    return true;
}

}  // namespace neomifes::app
