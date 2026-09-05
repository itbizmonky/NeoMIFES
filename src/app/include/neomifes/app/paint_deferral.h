#pragma once

// paint_deferral - WI-32: pure decision logic behind
// syncRenderStateAndInvalidate()'s (normal_mode_wiring.cpp) input-queue-drain
// optimization. Kept separate from that function's Win32-facing glue (the
// actual PeekMessageW() call, which needs a live HWND/message queue and
// cannot be unit-tested) so the one real *decision* here is - same split as
// reindent.h's computeReindentEdits() vs its own Win32-facing caller.
// normal_mode_wiring.cpp compiles straight into the NeoMIFES executable
// target rather than the neomifes_app_input library (see src/app/
// CMakeLists.txt), so nothing defined there is reachable from a unit test -
// this header lives under neomifes_app_input's public include dir instead,
// specifically so tests/unit/ can include it directly.
//
// See docs/issues/keystroke_burst_render_backlog.md for the backlog this
// exists to fix: every keystroke otherwise forces a synchronous, vsync-
// locked Present1() before the next queued keystroke can even be
// dispatched, so a fast typing burst backs up in the Win32 message queue
// and the display visibly "catches up" afterward.

namespace neomifes::app {

// `moreKeyboardInputQueued`: true if PeekMessageW(hwnd, WM_KEYFIRST,
// WM_KEYLAST, PM_NOREMOVE) found another keyboard message already waiting
// for this window - the message loop reaches this same call site again
// within microseconds for that message (cheap: no Direct2D work, just an
// in-memory document edit), so this frame's repaint can wait.
//
// `paintOverdue`: true once RenderPipeline::paintOverdue() reports that
// more than a small bounded time has passed since the last actual repaint
// request - the safety valve. Some queued keyboard messages never
// themselves reach this decision again (WM_KEYUP has no case in
// MainWindow::wndProc()'s switch at all; a boundary no-op like Backspace
// held past an already-emptied document's start returns changed=false and
// skips its own call site's invalidate) - without this valve, a burst that
// ends in one of those would strand its last real edit's repaint
// indefinitely.
[[nodiscard]] constexpr bool shouldPaintNow(bool moreKeyboardInputQueued, bool paintOverdue) noexcept {
    return !moreKeyboardInputQueued || paintOverdue;
}

}  // namespace neomifes::app
