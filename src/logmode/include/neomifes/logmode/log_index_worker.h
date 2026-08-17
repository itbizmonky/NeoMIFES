#pragma once

// LogIndexWorker - runs LogModel::build() (WI-14b's piece-streaming
// overload) on a single dedicated background thread, mirroring
// render::SyntaxWorker (Phase 7c) as the direct template. Lives in
// neomifes::logmode rather than neomifes::render: log indexing is not a
// rendering concern the way syntax highlighting is, and neomifes::logmode
// is already a self-contained module depending only on neomifes::document -
// adding a render:: dependency here would be an unnecessary layering
// detour. SyntaxWorker itself directly touching HWND/PostMessageW for
// completion signaling is the existing precedent that doing the same here
// is not a design deviation.
//
// Deliberately NOT SyntaxWorker's "keep only the latest pending request"
// design: SyntaxWorker gets away with that because RenderPipeline only ever
// cares about ONE currently-active tab's tokens, so overwriting a stale
// pending request is harmless. LogIndexWorker serves multiple independent
// tabs (EditorSession) that each need their own result - overwriting would
// mean a tab whose request lost the race never gets indexed at all, a real
// correctness bug, not just staleness. So this class uses a FIFO queue
// (std::deque) that processes every request in submission order; no
// coalescing of duplicate requests for the same session is implemented yet
// (no UI exists to trigger rapid re-requests until WI-14c - CLAUDE.md rule
// 10, revisit if/when that becomes a real cost).

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"

namespace neomifes::logmode {

// Posted to the target HWND on completion of every requestIndex() call.
// WM_APP+1 (kMsgDeferredInit, main_window.cpp) and WM_APP+2
// (kMsgSyntaxTokensReady, syntax_worker.h) are the only other values in use
// codebase-wide (grep-confirmed - there is no central WM_APP+N registry,
// same caveat syntax_worker.h's own comment already notes).
inline constexpr UINT kMsgLogIndexReady = WM_APP + 3;

// One queued indexing request. `sessionToken` is an opaque identifier the
// caller supplies (in practice EditorSession's own `this` pointer) and this
// class never dereferences - it is only round-tripped back via
// kMsgLogIndexReady's wParam so the receiver (normal_mode_wiring.cpp) can
// find the right EditorSession to apply the result to, without this class
// needing to know anything about EditorSession/Workspace.
struct PendingLogIndexRequest {
    std::shared_ptr<const document::BufferSnapshot> snapshot;
    LogPatternRule                                   rule;
    std::optional<int>                               assumedYear;
    const void*                                       sessionToken = nullptr;
};

class LogIndexWorker {
public:
    // targetHwnd receives kMsgLogIndexReady on completion of every request
    // this instance ever processes. The thread starts immediately and
    // blocks on m_cv until the first requestIndex() call or destruction.
    explicit LogIndexWorker(HWND targetHwnd);
    ~LogIndexWorker();

    LogIndexWorker(const LogIndexWorker&)            = delete;
    LogIndexWorker& operator=(const LogIndexWorker&) = delete;
    LogIndexWorker(LogIndexWorker&&)                 = delete;
    LogIndexWorker& operator=(LogIndexWorker&&)      = delete;

    // Fire-and-forget: appends to the FIFO queue (see this file's header
    // comment on why this never overwrites a still-pending request the way
    // SyntaxWorker::requestParse() does). `sessionToken` is opaque - see
    // PendingLogIndexRequest. Safe to call only from the UI thread (same
    // single-writer assumption as every other worker-request API in this
    // codebase).
    void requestIndex(std::shared_ptr<const document::BufferSnapshot> snapshot, LogPatternRule rule,
                      std::optional<int> assumedYear, const void* sessionToken) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately (SyntaxWorker's own precedent):
    // member construction follows declaration order regardless of the
    // constructor's init-list order, and the constructor starts m_thread
    // running workerLoop() immediately - workerLoop() must never observe a
    // not-yet-constructed mutex/condition_variable.
    std::mutex                          m_mutex;
    std::condition_variable             m_cv;
    std::deque<PendingLogIndexRequest>  m_pending;  // guarded by m_mutex; FIFO, never overwritten
    bool                                 m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::logmode
