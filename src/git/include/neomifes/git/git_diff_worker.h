#pragma once

// GitDiffWorker - runs GitRepository::discover()+diffAgainstHead() (WI-17a
// headless computation, WI-17b's BufferSnapshot overload) on a single
// dedicated background thread, mirroring neomifes::csvmode::CsvModelWorker
// as the direct template (itself modeled on logmode::LogIndexWorker /
// render::SyntaxWorker). Lives in neomifes::git rather than neomifes::render
// for the same reason CsvModelWorker lives in neomifes::csvmode: this is not
// a rendering concern, and neomifes::git is already a self-contained module
// depending only on neomifes::document - adding a render:: dependency here
// would be an unnecessary layering detour.
//
// FIFO (std::deque), not SyntaxWorker's "keep only the latest pending
// request" design - same reasoning as every other worker in this codebase:
// this worker serves multiple independent tabs (EditorSession), each of
// which needs its own result. Overwriting a stale pending request would mean
// a tab whose request lost the race never gets diffed at all.
//
// Deliberate difference from CsvModelWorker (but the SAME shape as
// JsonTreeWorker): a request for a file outside any Git repository, or
// untracked/not yet committed - GitRepository::discover()/diffAgainstHead()
// both fail with std::nullopt for these - is NOT silently dropped.
// CsvModelWorker drops CsvParseError::InvalidDelimiter because that failure
// is purely a caller-configuration mistake, unreachable via any content the
// worker sees. Here, "this file isn't in a Git repository" is an everyday,
// content/path-dependent outcome (most files a user opens are NOT under
// version control) - dropping it would leave whichever EditorSession issued
// the request permanently stuck with its "diff in flight" flag set, since
// nothing would ever clear it. So workerLoop() always posts, carrying
// std::optional<std::vector<LineDiffRegion>> as the payload.
//
// No GitRepository caching across requests: each request re-runs
// GitRepository::discover() (a directory walk, not the expensive part) AND
// diffAgainstHead() (the actual diff computation) from scratch. discover()
// itself is cheap and GitRepository is a single unique_ptr, so there is no
// established cost pressure yet to justify caching complexity - same "start
// with the simplest thing that works, revisit only if a benchmark says
// otherwise" judgment WI-16a's own CsvModel::build() made.

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/git/git_repository.h"

namespace neomifes::git {

// Posted to the target HWND on completion of every requestDiff() call.
// WM_APP+1 (kMsgDeferredInit, main_window.cpp), WM_APP+2
// (kMsgSyntaxTokensReady, syntax_worker.h), WM_APP+3 (kMsgLogIndexReady,
// log_index_worker.h), WM_APP+4 (kMsgJsonTreeReady, json_tree_worker.h), and
// WM_APP+5 (kMsgCsvIndexReady, csv_model_worker.h) are the only other values
// in use codebase-wide (grep-confirmed - there is no central WM_APP+N
// registry, same caveat those files' own comments already note).
inline constexpr UINT kMsgGitDiffReady = WM_APP + 6;

// One queued diff request. `sessionToken` is an opaque identifier the caller
// supplies (in practice EditorSession's own `this` pointer) and this class
// never dereferences - it is only round-tripped back via kMsgGitDiffReady's
// wParam so the receiver (normal_mode_wiring.cpp) can find the right
// EditorSession to apply the result to, without this class needing to know
// anything about EditorSession/Workspace.
struct PendingGitDiffRequest {
    std::shared_ptr<const document::BufferSnapshot> snapshot;
    std::filesystem::path                            absoluteFilePath;
    const void*                                       sessionToken = nullptr;
};

class GitDiffWorker {
public:
    // targetHwnd receives kMsgGitDiffReady on completion of every request
    // this instance ever processes. The thread starts immediately and
    // blocks on m_cv until the first requestDiff() call or destruction.
    explicit GitDiffWorker(HWND targetHwnd);
    ~GitDiffWorker();

    GitDiffWorker(const GitDiffWorker&)            = delete;
    GitDiffWorker& operator=(const GitDiffWorker&) = delete;
    GitDiffWorker(GitDiffWorker&&)                 = delete;
    GitDiffWorker& operator=(GitDiffWorker&&)      = delete;

    // Fire-and-forget: appends to the FIFO queue (see this file's header
    // comment on why this never overwrites a still-pending request).
    // `sessionToken` is opaque - see PendingGitDiffRequest. Safe to call
    // only from the UI thread (same single-writer assumption as every other
    // worker-request API in this codebase).
    void requestDiff(std::shared_ptr<const document::BufferSnapshot> snapshot,
                     std::filesystem::path absoluteFilePath, const void* sessionToken) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately (every other worker's own
    // precedent): member construction follows declaration order regardless
    // of the constructor's init-list order, and the constructor starts
    // m_thread running workerLoop() immediately - workerLoop() must never
    // observe a not-yet-constructed mutex/condition_variable.
    std::mutex                            m_mutex;
    std::condition_variable               m_cv;
    std::deque<PendingGitDiffRequest>    m_pending;  // guarded by m_mutex; FIFO, never overwritten
    bool                                   m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::git
