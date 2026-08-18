#pragma once

// CsvModelWorker - runs CsvModel::build() (WI-16a's BufferSnapshot overload)
// on a single dedicated background thread, mirroring
// neomifes::logmode::LogIndexWorker as the direct template. Lives in
// neomifes::csvmode rather than neomifes::render for the same reason
// LogIndexWorker lives in neomifes::logmode: this is not a rendering
// concern, and neomifes::csvmode is already a self-contained module
// depending only on neomifes::document - adding a render:: dependency here
// would be an unnecessary layering detour.
//
// FIFO (std::deque), not SyntaxWorker's "keep only the latest pending
// request" design - same reasoning as LogIndexWorker's own header comment:
// this worker serves multiple independent tabs (EditorSession), each of
// which needs its own result. Overwriting a stale pending request would mean
// a tab whose request lost the race never gets indexed at all.
//
// Deliberately the SAME shape as LogIndexWorker (not JsonTreeWorker): a
// failed build (CsvModel::build() returning CsvParseError) IS silently
// dropped, same as LogIndexWorker skips a LogPatternError::InvalidRegex
// result. CsvParseError::InvalidDelimiter (csv_model.h's own contract) is a
// caller-configuration mistake (options.delimiter set to '\r'/'\n'/'"'),
// not a statement about document content - the same shape as
// LogPatternError::InvalidRegex, which is unreachable for any of
// builtInLogPatterns(). This is unlike JsonTreeWorker's design, where a
// parseJsonTree() std::nullopt is an everyday, content-dependent outcome
// (the document simply isn't valid JSON) that must still be posted so the
// requesting EditorSession's "indexing in flight" flag doesn't stick
// forever. No command in this codebase calls requestIndex() with anything
// other than the default delimiter ',' or a detectCsvDelimiter() result
// (always one of its own known-good candidates) until a future WI adds one,
// so InvalidDelimiter is unreachable via any wired-up code path today.

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "neomifes/csvmode/csv_model.h"
#include "neomifes/document/buffer_snapshot.h"

namespace neomifes::csvmode {

// Posted to the target HWND on completion of every requestIndex() call.
// WM_APP+1 (kMsgDeferredInit, main_window.cpp), WM_APP+2
// (kMsgSyntaxTokensReady, syntax_worker.h), WM_APP+3 (kMsgLogIndexReady,
// log_index_worker.h), and WM_APP+4 (kMsgJsonTreeReady, json_tree_worker.h)
// are the only other values in use codebase-wide (grep-confirmed - there is
// no central WM_APP+N registry, same caveat those files' own comments
// already note).
inline constexpr UINT kMsgCsvIndexReady = WM_APP + 5;

// One queued indexing request. `sessionToken` is an opaque identifier the
// caller supplies (in practice EditorSession's own `this` pointer) and this
// class never dereferences - it is only round-tripped back via
// kMsgCsvIndexReady's wParam so the receiver (normal_mode_wiring.cpp) can
// find the right EditorSession to apply the result to, without this class
// needing to know anything about EditorSession/Workspace.
struct PendingCsvIndexRequest {
    std::shared_ptr<const document::BufferSnapshot> snapshot;
    CsvParseOptions                                   options;
    const void*                                       sessionToken = nullptr;
};

class CsvModelWorker {
public:
    // targetHwnd receives kMsgCsvIndexReady on completion of every request
    // this instance ever processes. The thread starts immediately and
    // blocks on m_cv until the first requestIndex() call or destruction.
    explicit CsvModelWorker(HWND targetHwnd);
    ~CsvModelWorker();

    CsvModelWorker(const CsvModelWorker&)            = delete;
    CsvModelWorker& operator=(const CsvModelWorker&) = delete;
    CsvModelWorker(CsvModelWorker&&)                 = delete;
    CsvModelWorker& operator=(CsvModelWorker&&)      = delete;

    // Fire-and-forget: appends to the FIFO queue (see this file's header
    // comment on why this never overwrites a still-pending request).
    // `sessionToken` is opaque - see PendingCsvIndexRequest. Safe to call
    // only from the UI thread (same single-writer assumption as every other
    // worker-request API in this codebase).
    void requestIndex(std::shared_ptr<const document::BufferSnapshot> snapshot, CsvParseOptions options,
                      const void* sessionToken) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately (LogIndexWorker's own
    // precedent): member construction follows declaration order regardless
    // of the constructor's init-list order, and the constructor starts
    // m_thread running workerLoop() immediately - workerLoop() must never
    // observe a not-yet-constructed mutex/condition_variable.
    std::mutex                          m_mutex;
    std::condition_variable             m_cv;
    std::deque<PendingCsvIndexRequest>  m_pending;  // guarded by m_mutex; FIFO, never overwritten
    bool                                 m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::csvmode
