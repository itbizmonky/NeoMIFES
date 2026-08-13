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

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::core {
class CommandDispatcher;
class SelectionModel;
class Viewport;
class FoldingModel;
enum class IndentationConversionTarget;
}  // namespace neomifes::core

namespace neomifes::syntax {
struct OutlineNode;
}  // namespace neomifes::syntax

namespace neomifes::app {

// Handles WM_KEYDOWN: arrow movement (+Shift extend), Home/End (+Ctrl for
// document start/end), Backspace/Delete. Returns true if the document or
// selection changed (caller should sync Viewport/RenderPipeline and
// invalidate). Ctrl+Z/Ctrl+Y (undo/redo) moved out (WI-07 step2) - see
// normal_mode_wiring.cpp's dispatchCommand().
//
// `folding` (Phase 7i, nullopt-like default nullptr - same "optional feature,
// absent pointer = behave exactly as before" convention as
// RenderPipeline::setDocument()'s nullptr) is only consulted for vertical
// movement (Up/Down/PageUp/PageDown): if that movement lands a cursor inside
// a currently-folded (hidden) region, it gets snapped to the region's near
// boundary in the direction of travel - see this file's .cpp for why this
// lives here rather than in core::SelectionModel (independent-engines
// principle, same reasoning as core::Viewport's own header comment).
bool handleKeyDown(UINT vkCode, bool shiftDown, bool ctrlDown,
                   core::CommandDispatcher& dispatcher, core::SelectionModel& selection,
                   core::Viewport& viewport, const document::Document& document,
                   const core::FoldingModel* folding = nullptr);

// Handles WM_CHAR: printable characters and Enter/Tab are inserted (or
// replace the active selection); other control characters are ignored
// (Backspace/Escape etc. arrive via WM_KEYDOWN's handleKeyDown instead, so
// ignoring them here avoids double-handling). Returns true if the document
// changed.
bool handleChar(wchar_t ch, core::CommandDispatcher& dispatcher, core::SelectionModel& selection,
               core::Viewport& viewport, const document::Document& document);

// WI-07 step5: Overwrite (OVR) mode's WM_CHAR handling - the caller
// (normal_mode_wiring.cpp's handleCharEvent()) branches to this instead of
// handleChar() while EditorSession::overwriteMode() is true. Enter ('\r')
// and Tab ('\t') always fall back to plain insertion (handleChar()'s own
// behavior) - overwriting a newline or expanding a tab in place isn't a
// standard OVR-mode behavior in any mainstream editor. For other printable
// characters: a cursor with an active selection replaces it (identical to
// handleChar()'s own replace-selection behavior); a cursor with no
// selection replaces the single character immediately after it IF one
// exists on the same line (document::Document::lineText() excludes the
// trailing '\n', so `column < lineText(line).size()` is exactly "there is
// a same-line character to overwrite") - otherwise (end of line or end of
// document) it falls back to a plain insert, so OVR mode can never delete a
// line break or eat into the next line. Reuses core::MultiCursorEditCommand
// directly (same mechanism handleChar()/handlePaste() already use) so
// Undo/Redo work with no new ICommand subclass. Out of scope: changing the
// caret's visual shape while in OVR mode (build_plan.md's WI-07 step5
// design note).
bool applyOverwriteChar(wchar_t ch, core::CommandDispatcher& dispatcher, core::SelectionModel& selection,
                        core::Viewport& viewport, const document::Document& document);

// Pure function: maps a WM_MOUSEWHEEL delta to a new topLine, clamped to
// [0, totalLines - 1] (or 0 if totalLines == 0) - the same effective bound
// RenderPipeline already applies at render time for display purposes
// (m_topLine < totalLines ? m_topLine : totalLines - 1). Before this upper
// bound existed here too, scrolling past EOF left Viewport's stored topLine
// growing unboundedly - nothing visibly changed once the display-side clamp
// kicked in, but scrolling back up required "unwinding" all of that
// invisible excess first. Clamping here keeps Viewport::topLine() from ever
// diverging from what's actually drawn.
[[nodiscard]] document::LineNumber applyMouseWheelScroll(short wheelDelta,
                                                          document::LineNumber currentTopLine,
                                                          document::LineNumber totalLines);

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

// Returns every selecting cursor's text joined with '\n' in ascending
// cursor order (Phase 4b6c - Ctrl+C/X; generalized to all cursors in Phase
// 4b7c), or nullopt if no cursor has an active selection. Cursors without a
// selection are skipped, not represented as an empty chunk. Read-only:
// touches neither the document nor the clipboard - callers combine this
// with platform::setClipboardText() (src/platform/clipboard.h, kept out of
// this Win32-API-free module) and, for Cut, a follow-up
// deleteAllSelections().
[[nodiscard]] std::optional<std::u16string> textToCopy(const core::SelectionModel& selection,
                                                        const document::Document&  document);

// Inserts `text` identically at every cursor, replacing each one's
// selection if it has one (Phase 4b6c, generalized to all cursors in Phase
// 4b7c - same "same text at every cursor" rule as handleChar(), not the
// "distribute N copied chunks across N cursors" some editors do, which
// would need clipboard metadata this codebase doesn't have). Always
// returns true.
bool handlePaste(std::u16string_view text, core::CommandDispatcher& dispatcher,
                 core::SelectionModel& selection, core::Viewport& viewport,
                 const document::Document& document);

// Deletes every cursor's active selection (Phase 4b7c - Ctrl+X's delete
// half, after the caller has already copied the text via textToCopy()).
// Cursors without a selection are left untouched. Returns false without
// dispatching if no cursor had a selection to delete.
bool deleteAllSelections(core::CommandDispatcher& dispatcher, core::SelectionModel& selection,
                         core::Viewport& viewport, const document::Document& document);

// Picks which click interpretation applies to a hit-tested WM_LBUTTONDOWN and
// applies it (WI-04: moved here from main.cpp unchanged - Win32/RenderPipeline
// independent). `altCursorAnchor` (Phase 4b6d) is the caller's session-lifetime
// state tracking the anchor of the cursor a prior plain Alt+click added, so a
// later Alt+Shift+click (and drag) can extend that specific cursor -
// SelectionModel::moveAllTo()/moveAll() always apply to every cursor
// uniformly, so this targeted extension needs the caller to remember which
// cursor is "active" across separate mouse events. `rectangularAnchor` (Phase
// 4b8a) is the equivalent session-lifetime state for Shift+Alt+drag
// rectangular selection - only ever *set* here, on a Shift+Alt+click, never
// acted upon here (a click alone falls through to the existing
// altCursorAnchor/handleAltClick logic unchanged).
bool dispatchMouseDown(document::TextPos hit, bool shiftDown, bool altDown, int clickCount,
                       core::SelectionModel& selectionModel, core::Viewport& viewport,
                       const document::Document& document,
                       std::optional<document::TextPos>& altCursorAnchor,
                       std::optional<document::TextPos>& rectangularAnchor);

// WI-03: standard Win32 scroll-code decode for WM_HSCROLL - nullopt for
// SB_ENDSCROLL and anything else unrecognized (a no-op). WI-04: moved here
// from main.cpp unchanged (pure function).
[[nodiscard]] std::optional<std::uint32_t> computeHScrollTargetColumn(
    WORD scrollCode, WORD scrollPos, std::uint32_t currentColumn, std::uint32_t pageStep) noexcept;

// Convert Tabs to Spaces / Convert Spaces to Tabs command-palette actions
// (Phase 4b8d). Applies to the whole document. Reuses core::ReplaceAllCommand
// (Phase 5b2) rather than a bespoke command class - see
// indentation_conversion.h's header comment. Returns false (no-op, no
// dispatch) if no lines need conversion. WI-04: moved here from main.cpp,
// with its HWND parameter and ::InvalidateRect() call removed and a bool
// return added in their place - matching handlePaste()/handleChar()'s
// existing "return whether the document changed, caller repaints" convention
// used throughout this module. The one call site (main.cpp) now performs the
// repaint itself when this returns true. WI-08: tabWidth is now a caller-
// supplied parameter (core::Settings::tabWidth) instead of a local
// constexpr 4 - this is one of the two kTabWidth duplicates the WI-08 DoD
// requires eliminating (the other was render_pipeline.cpp's, resolved in
// WI-08 step 2 via RenderPipeline::m_tabWidth).
bool applyIndentationConversion(core::IndentationConversionTarget target, document::Document& document,
                                core::CommandDispatcher& dispatcher, const core::SelectionModel& selectionModel,
                                std::uint32_t tabWidth);

// Parses the currently open document into an OutlineNode tree (empty if no
// language detected - an untitled session, or an unrecognized extension).
// Feeds both the outline panel and core::FoldingModel's fold regions from the
// exact same parse, rather than each computing (and re-parsing) its own.
// WI-04: moved here from main.cpp unchanged (Win32/RenderPipeline
// independent).
[[nodiscard]] std::vector<syntax::OutlineNode> extractCurrentOutline(
    const document::Document& document, const std::optional<std::filesystem::path>& currentDocumentPath);

}  // namespace neomifes::app
