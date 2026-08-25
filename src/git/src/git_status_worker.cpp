#include "neomifes/git/git_status_worker.h"

#include <memory>
#include <optional>
#include <utility>

namespace neomifes::git {

GitStatusWorker::GitStatusWorker(HWND targetHwnd)
    : m_targetHwnd(targetHwnd), m_thread(&GitStatusWorker::workerLoop, this) {}

GitStatusWorker::~GitStatusWorker() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_shuttingDown = true;
    }
    m_cv.notify_one();
    m_thread.join();
}

void GitStatusWorker::requestStatus(std::filesystem::path absoluteFilePath, const void* sessionToken) noexcept {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(
            PendingGitStatusRequest{.absoluteFilePath = std::move(absoluteFilePath), .sessionToken = sessionToken});
    }
    m_cv.notify_one();
}

void GitStatusWorker::workerLoop() {
    // See the same NOLINT in git_diff_worker.cpp's workerLoop(): ownership
    // of the heap-allocated std::optional<std::vector<GitStatusEntry>> below
    // is transferred across the PostMessageW/kMsgGitStatusReady boundary to
    // a different translation unit, which the single-TU static analyzer
    // can't see reclaims it.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    while (true) {
        PendingGitStatusRequest request;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_pending.empty() || m_shuttingDown; });
            if (m_pending.empty()) {
                // Only reachable via m_shuttingDown - nothing left to
                // process.
                return;
            }
            request = std::move(m_pending.front());
            m_pending.pop_front();
        }

        // Neither discover() nor statusList() throws (git_repository.h's
        // own contract); a genuine std::bad_alloc is allowed to propagate
        // and terminate the process rather than being swallowed here
        // (CLAUDE.md forbids unconditional catch(...)), matching every
        // other worker's own stance on this.
        std::optional<std::vector<GitStatusEntry>> result;
        auto repo = GitRepository::discover(request.absoluteFilePath.parent_path());
        if (repo.has_value()) {
            result = repo->statusList();
        }

        // Unlike CsvModelWorker, a std::nullopt result is NOT dropped here -
        // see this file's header comment on git_status_worker.h for why.
        auto resultPtr = std::make_unique<std::optional<std::vector<GitStatusEntry>>>(std::move(result));

        // Ownership transferred to whichever code handles kMsgGitStatusReady
        // - it must reconstruct a unique_ptr from this pointer immediately
        // upon receipt. Only released once PostMessageW actually succeeds -
        // if the target window is already gone (e.g. a shutdown race),
        // `resultPtr` stays owned by this unique_ptr and its destructor
        // reclaims the memory instead of leaking it.
        if (::PostMessageW(m_targetHwnd, kMsgGitStatusReady, reinterpret_cast<WPARAM>(request.sessionToken),
                           reinterpret_cast<LPARAM>(resultPtr.get())) != 0) {
            [[maybe_unused]] auto* released = resultPtr.release();
        }
    }
}

}  // namespace neomifes::git
