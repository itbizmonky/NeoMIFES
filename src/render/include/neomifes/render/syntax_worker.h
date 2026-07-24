#pragma once

// SyntaxWorker - runs neomifes::syntax::parseCpp() on a single dedicated
// background thread (Phase 7c, roadmap sec.7.9), so RenderPipeline's UI
// thread never blocks on a full-document re-parse (7a's benchmark: ~6.6s for
// 1,000,000 lines). This is the first std::thread in the codebase -
// detailed_design.md sec.16's thread-safety table and buffer_snapshot.h's
// "safe to hand out to arbitrary threads (search, syntax, plugin workers)"
// comment both already anticipated exactly this consumer.
//
// Deliberately NOT true tree-sitter incremental parsing (ts_tree_edit()) -
// every request re-parses the WHOLE document. Document has no edit-range
// change-notification mechanism yet (see the Phase 7c plan's Context
// section), so this only moves the existing full re-parse off the UI
// thread; it does not reduce the amount of work done per edit.

#include <windows.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "neomifes/document/buffer_snapshot.h"

namespace neomifes::render {

// Posted to the target HWND on completion of every requestParse() (see
// below). Lives here (not ui::main_window.h) so that neomifes::render never
// has to depend on neomifes::ui to know its own completion-message value -
// CLAUDE.md's layer rule has Rendering Engine BELOW UI Shell, so render::
// must not include anything from ui::. main.cpp (which already depends on
// both) is the only place that needs to compare against this constant; see
// ui::MainWindowConfig::onAppMessage's doc comment for the other half of
// this handoff. WM_APP+1 is main_window.cpp's own internal kMsgDeferredInit
// - chosen distinct from it, though note there is no single central
// registry of WM_APP+N values in this codebase (only two exist so far).
inline constexpr UINT kMsgSyntaxTokensReady = WM_APP + 2;

class SyntaxWorker {
public:
    // targetHwnd receives kMsgSyntaxTokensReady (above) on completion of
    // every request this instance ever processes. The thread starts
    // immediately and blocks on m_cv until the first requestParse() call or
    // destruction.
    explicit SyntaxWorker(HWND targetHwnd);
    ~SyntaxWorker();

    SyntaxWorker(const SyntaxWorker&)            = delete;
    SyntaxWorker& operator=(const SyntaxWorker&) = delete;
    SyntaxWorker(SyntaxWorker&&)                 = delete;
    SyntaxWorker& operator=(SyntaxWorker&&)      = delete;

    // Fire-and-forget. If the worker hasn't started a previous pending
    // request yet, `snapshot` silently replaces it (no queue - only the
    // most recent request matters, see this file's header comment). Safe to
    // call only from the UI thread (same single-writer assumption as every
    // other RenderPipeline method).
    void requestParse(std::shared_ptr<const document::BufferSnapshot> snapshot) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately: member construction follows
    // declaration order regardless of the constructor's init-list order, and
    // the constructor starts m_thread running workerLoop() immediately -
    // workerLoop() must never observe a not-yet-constructed mutex/condition_
    // variable.
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    // Guarded by m_mutex. Set by requestParse(), consumed (and reset to
    // nullptr) by workerLoop() - nullptr means "nothing pending".
    std::shared_ptr<const document::BufferSnapshot> m_pending;
    // Guarded by m_mutex. Set once by the destructor to wake the worker for
    // the final time and tell it to return instead of waiting again.
    bool m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::render
