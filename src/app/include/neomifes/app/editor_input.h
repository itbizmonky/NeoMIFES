#pragma once

// editor_input - translates raw Win32 key/char/wheel input into calls
// against Editor Core (SelectionModel/CommandDispatcher/Viewport), per
// docs/decisions/ADR-012's Phase 4b re-evaluation trigger for keyboard
// wiring.
//
// Deliberately takes Win32-primitive parameters (UINT vkCode, wchar_t,
// short wheelDelta) rather than an HWND/MSG, and touches no Win32 window
// APIs itself - MainWindow::wndProc / main.cpp own translating actual
// messages into these calls. This keeps the logic headlessly unit-testable
// the same way src/core/'s Phase 4a components are (no Win32 message
// simulation harness exists in this codebase - see ADR-012's context).

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::core {
class CommandDispatcher;
class SelectionModel;
class Viewport;
}  // namespace neomifes::core

namespace neomifes::app {

// Handles WM_KEYDOWN: arrow movement (+Shift extend), Home/End (+Ctrl for
// document start/end), Backspace/Delete, Ctrl+Z/Ctrl+Y undo/redo. Returns
// true if the document or selection changed (caller should sync
// Viewport/RenderPipeline and invalidate).
bool handleKeyDown(UINT vkCode, bool shiftDown, bool ctrlDown,
                   core::CommandDispatcher& dispatcher, core::SelectionModel& selection,
                   core::Viewport& viewport, const document::Document& document);

// Handles WM_CHAR: printable characters and Enter/Tab are inserted (or
// replace the active selection); other control characters are ignored
// (Backspace/Escape etc. arrive via WM_KEYDOWN's handleKeyDown instead, so
// ignoring them here avoids double-handling). Returns true if the document
// changed.
bool handleChar(wchar_t ch, core::CommandDispatcher& dispatcher, core::SelectionModel& selection,
               core::Viewport& viewport, const document::Document& document);

// Pure function: maps a WM_MOUSEWHEEL delta to a new topLine, clamped to 0.
// No document/state dependency, so it's testable without constructing
// anything.
[[nodiscard]] document::LineNumber applyMouseWheelScroll(short wheelDelta,
                                                          document::LineNumber currentTopLine);

// Places the cursor at `pos` (collapsing any selection), or extends the
// selection to `pos` if shiftDown. `pos` is already hit-tested by the
// caller (RenderPipeline::hitTest(), Phase 4b2) - this module stays
// Win32/render-independent per the file header above, so the screen
// coordinate -> TextPos conversion happens in the render layer, not here.
// Always returns true (a click always warrants a repaint attempt).
bool handleMouseDown(document::TextPos pos, bool shiftDown, core::SelectionModel& selection,
                     core::Viewport& viewport, const document::Document& document);

// Selects the word (simple character-class boundaries, Phase 4b4) at `pos`.
// `pos` is already hit-tested by the caller. Always returns true.
bool handleDoubleClick(document::TextPos pos, core::SelectionModel& selection,
                       core::Viewport& viewport, const document::Document& document);

// Selects the entire line containing `pos` (Phase 4b4). `pos` is already
// hit-tested by the caller. Always returns true.
bool handleTripleClick(document::TextPos pos, core::SelectionModel& selection,
                       core::Viewport& viewport, const document::Document& document);

// Adds a new, non-primary cursor at `pos` (Phase 4b5b - Alt+click). `pos` is
// already hit-tested by the caller. Delegates to
// SelectionModel::addCursor(), which already handles merging with an
// existing cursor at the same position (Phase 4a). Always returns true.
bool handleAltClick(document::TextPos pos, core::SelectionModel& selection,
                    core::Viewport& viewport, const document::Document& document);

// Returns the primary cursor's selected text (Phase 4b6c - Ctrl+C/X), or
// nullopt if there's no active selection. Read-only: touches neither the
// document nor the clipboard - callers combine this with
// platform::setClipboardText() (src/platform/clipboard.h, kept out of this
// Win32-API-free module) and, for Cut, a follow-up delete of the same range.
[[nodiscard]] std::optional<std::u16string> textToCopy(const core::SelectionModel& selection,
                                                        const document::Document&  document);

// Inserts `text` at the primary cursor, replacing its selection if it has
// one. Scoped to the primary cursor only (Phase 4b6c) - distributing a
// paste across multiple cursors is a separate, unscoped design question.
// Always returns true.
bool handlePaste(std::u16string_view text, core::CommandDispatcher& dispatcher,
                 core::SelectionModel& selection, core::Viewport& viewport,
                 const document::Document& document);

}  // namespace neomifes::app
