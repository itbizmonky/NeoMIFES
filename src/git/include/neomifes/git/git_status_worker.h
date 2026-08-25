#pragma once

// GitStatusWorker - runs GitRepository::discover()+statusList() (WI-17e) on
// a single dedicated background thread, mirroring GitDiffWorker as the
// direct template (itself modeled on CsvModelWorker/JsonTreeWorker/
// LogIndexWorker). Lives in neomifes::git for the same reason GitDiffWorker
// does.
//
// Deliberately NOT parameterized by document::BufferSnapshot the way
// GitDiffWorker is: statusList() always reports on-disk state (see
// git_repository.h's own comment on statusList() for why), so a request here
// only ever needs a path to discover the repository from.
//
// Same "always post, never silently drop" contract as GitDiffWorker: a path
// outside any Git repository is an everyday, content/path-dependent outcome
// (most files a user opens are not under version control), not a caller
// mistake - workerLoop() always posts, carrying
// std::optional<std::vector<GitStatusEntry>> as the payload, so whichever
// caller is waiting on the result never gets stuck with an "in flight" flag
// that nothing ever clears.
//
// No caching across requests, same reasoning as GitDiffWorker: discover() is
// cheap, and there is no established cost pressure yet to justify caching
// complexity.

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include "neomifes/git/git_repository.h"

namespace neomifes::git {

// Posted to the target HWND on completion of every requestStatus() call.
// WM_APP+1 (kMsgDeferredInit, main_window.cpp), WM_APP+2
// (kMsgSyntaxTokensReady, syntax_worker.h), WM_APP+3 (kMsgLogIndexReady,
// log_index_worker.h), WM_APP+4 (kMsgJsonTreeReady, json_tree_worker.h),
// WM_APP+5 (kMsgCsvIndexReady, csv_model_worker.h), WM_APP+6
// (kMsgGitDiffReady, git_diff_worker.h), and WM_APP+7 (kMsgXmlTreeReady,
// xml_tree_worker.h) are the only other values in use codebase-wide
// (grep-confirmed - there is no central WM_APP+N registry, same caveat those
// files' own comments already note).
inline constexpr UINT kMsgGitStatusReady = WM_APP + 8;

// One queued status request. `sessionToken` is an opaque identifier the
// caller supplies and this class never dereferences - it is only
// round-tripped back via kMsgGitStatusReady's wParam, same contract as
// GitDiffWorker's PendingGitDiffRequest::sessionToken.
struct PendingGitStatusRequest {
    std::filesystem::path absoluteFilePath;
    const void*           sessionToken = nullptr;
};

class GitStatusWorker {
public:
    // targetHwnd receives kMsgGitStatusReady on completion of every request
    // this instance ever processes. The thread starts immediately and
    // blocks on m_cv until the first requestStatus() call or destruction.
    explicit GitStatusWorker(HWND targetHwnd);
    ~GitStatusWorker();

    GitStatusWorker(const GitStatusWorker&)            = delete;
    GitStatusWorker& operator=(const GitStatusWorker&) = delete;
    GitStatusWorker(GitStatusWorker&&)                 = delete;
    GitStatusWorker& operator=(GitStatusWorker&&)      = delete;

    // Fire-and-forget: appends to the FIFO queue. `absoluteFilePath` is only
    // ever used to discover() the enclosing repository from - it need not be
    // the file whose status the caller ultimately cares about (statusList()
    // itself returns every changed file in the repository, not just one).
    // Safe to call only from the UI thread (same single-writer assumption as
    // every other worker-request API in this codebase).
    void requestStatus(std::filesystem::path absoluteFilePath, const void* sessionToken) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately (every other worker's own
    // precedent): member construction follows declaration order regardless
    // of the constructor's init-list order, and the constructor starts
    // m_thread running workerLoop() immediately - workerLoop() must never
    // observe a not-yet-constructed mutex/condition_variable.
    std::mutex                          m_mutex;
    std::condition_variable             m_cv;
    std::deque<PendingGitStatusRequest> m_pending;  // guarded by m_mutex; FIFO, never overwritten
    bool                                 m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::git
