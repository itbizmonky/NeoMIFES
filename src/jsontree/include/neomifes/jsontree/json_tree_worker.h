#pragma once

// JsonTreeWorker - runs parseJsonTree() (WI-15b's BufferSnapshot overload)
// on a single dedicated background thread, mirroring
// neomifes::logmode::LogIndexWorker as the direct template (itself modeled
// on render::SyntaxWorker, Phase 7c). Lives in neomifes::jsontree rather than
// neomifes::render for the same reason LogIndexWorker lives in
// neomifes::logmode: this is not a rendering concern, and neomifes::jsontree
// is already a self-contained module depending only on neomifes::document -
// adding a render:: dependency here would be an unnecessary layering detour.
//
// FIFO (std::deque), not SyntaxWorker's "keep only the latest pending
// request" design - same reasoning as LogIndexWorker's own header comment:
// this worker serves multiple independent tabs (EditorSession), each of
// which needs its own result. Overwriting a stale pending request would mean
// a tab whose request lost the race never gets its JSON tree at all.
//
// Deliberate difference from LogIndexWorker: a failed parse (parseJsonTree()
// returning std::nullopt) is NOT silently dropped. LogIndexWorker's
// workerLoop() skips posting a result when LogModel::build() fails, because
// that failure path (InvalidRegex) is unreachable for any built-in pattern
// and has no UI to observe it yet. For JSON, std::nullopt is an everyday
// outcome (the document simply isn't valid JSON) - dropping it would leave
// whichever EditorSession issued the request permanently stuck with its
// "indexing in flight" flag set, since nothing would ever clear it. So
// workerLoop() always posts, carrying std::optional<JsonNode> as the
// payload rather than JsonNode itself.

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/jsontree/json_tree.h"

namespace neomifes::jsontree {

// Posted to the target HWND on completion of every requestIndex() call.
// WM_APP+1 (kMsgDeferredInit, main_window.cpp), WM_APP+2
// (kMsgSyntaxTokensReady, syntax_worker.h), and WM_APP+3 (kMsgLogIndexReady,
// log_index_worker.h) are the only other values in use codebase-wide (grep-
// confirmed - there is no central WM_APP+N registry, same caveat those
// files' own comments already note).
inline constexpr UINT kMsgJsonTreeReady = WM_APP + 4;

// One queued indexing request. `sessionToken` is an opaque identifier the
// caller supplies (in practice EditorSession's own `this` pointer) and this
// class never dereferences - it is only round-tripped back via
// kMsgJsonTreeReady's wParam so the receiver (normal_mode_wiring.cpp) can
// find the right EditorSession to apply the result to, without this class
// needing to know anything about EditorSession/Workspace. Unlike
// PendingLogIndexRequest, there is no per-request configuration (no
// LogPatternRule/assumedYear equivalent) - parseJsonTree() takes only the
// document text.
struct PendingJsonTreeRequest {
    std::shared_ptr<const document::BufferSnapshot> snapshot;
    const void*                                       sessionToken = nullptr;
};

class JsonTreeWorker {
public:
    // targetHwnd receives kMsgJsonTreeReady on completion of every request
    // this instance ever processes. The thread starts immediately and
    // blocks on m_cv until the first requestIndex() call or destruction.
    explicit JsonTreeWorker(HWND targetHwnd);
    ~JsonTreeWorker();

    JsonTreeWorker(const JsonTreeWorker&)            = delete;
    JsonTreeWorker& operator=(const JsonTreeWorker&) = delete;
    JsonTreeWorker(JsonTreeWorker&&)                 = delete;
    JsonTreeWorker& operator=(JsonTreeWorker&&)      = delete;

    // Fire-and-forget: appends to the FIFO queue (see this file's header
    // comment on why this never overwrites a still-pending request).
    // `sessionToken` is opaque - see PendingJsonTreeRequest. Safe to call
    // only from the UI thread (same single-writer assumption as every other
    // worker-request API in this codebase).
    void requestIndex(std::shared_ptr<const document::BufferSnapshot> snapshot,
                      const void* sessionToken) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately (LogIndexWorker's own
    // precedent): member construction follows declaration order regardless
    // of the constructor's init-list order, and the constructor starts
    // m_thread running workerLoop() immediately - workerLoop() must never
    // observe a not-yet-constructed mutex/condition_variable.
    std::mutex                            m_mutex;
    std::condition_variable               m_cv;
    std::deque<PendingJsonTreeRequest>    m_pending;  // guarded by m_mutex; FIFO, never overwritten
    bool                                   m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::jsontree
