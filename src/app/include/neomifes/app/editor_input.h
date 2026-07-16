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

}  // namespace neomifes::app
