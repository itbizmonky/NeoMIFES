#include "neomifes/logmode/log_index_worker.h"

#include <memory>
#include <utility>

namespace neomifes::logmode {

LogIndexWorker::LogIndexWorker(HWND targetHwnd)
    : m_targetHwnd(targetHwnd), m_thread(&LogIndexWorker::workerLoop, this) {}

LogIndexWorker::~LogIndexWorker() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_shuttingDown = true;
    }
    m_cv.notify_one();
    m_thread.join();
}

void LogIndexWorker::requestIndex(std::shared_ptr<const document::BufferSnapshot> snapshot, LogPatternRule rule,
                                  std::optional<int> assumedYear, const void* sessionToken) noexcept {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(PendingLogIndexRequest{.snapshot     = std::move(snapshot),
                                                    .rule         = std::move(rule),
                                                    .assumedYear  = assumedYear,
                                                    .sessionToken = sessionToken});
    }
    m_cv.notify_one();
}

void LogIndexWorker::workerLoop() {
    // See the same NOLINT in syntax_worker.cpp's workerLoop(): ownership of
    // the heap-allocated LogModel below is transferred across the
    // PostMessageW/kMsgLogIndexReady boundary to a different translation
    // unit (normal_mode_wiring.cpp's onAppMessage hook), which the
    // single-TU static analyzer can't see reclaims it. The directive must
    // sit on the line immediately above the code it suppresses (NOLINTNEXTLINE
    // only applies to the very next physical line) - an earlier version of
    // this comment placed it 6 lines too high, at the top of this block,
    // where it suppressed nothing and let the CI clang-tidy job fail.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    while (true) {
        PendingLogIndexRequest request;
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

        // LogModel::build() is not noexcept; a genuine std::bad_alloc is
        // allowed to propagate and terminate the process rather than being
        // swallowed here (CLAUDE.md forbids unconditional catch(...)),
        // matching SyntaxWorker's own stance on this.
        auto result = LogModel::build(*request.snapshot, request.rule, request.assumedYear);
        if (!result.has_value()) {
            // InvalidRegex is unreachable for any of builtInLogPatterns()
            // (WI-14a's EveryBuiltInPatternCompilesAsValidRe2 test pins
            // this down) - only a future hand-edited/user-supplied rule
            // (WI-14d) could trigger it. No diagnostic surfacing exists for
            // this yet (no UI consumes LogIndexWorker at all until WI-14c);
            // silently skip rather than post a result for this request.
            continue;
        }

        auto modelPtr = std::make_unique<LogModel>(std::move(*result));

        // Ownership transferred to whichever code handles
        // kMsgLogIndexReady - it must reconstruct a unique_ptr from this
        // pointer immediately upon receipt. Only released once
        // PostMessageW actually succeeds - if the target window is already
        // gone (e.g. a shutdown race), `modelPtr` stays owned by this
        // unique_ptr and its destructor reclaims the memory instead of
        // leaking it.
        if (::PostMessageW(m_targetHwnd, kMsgLogIndexReady, reinterpret_cast<WPARAM>(request.sessionToken),
                           reinterpret_cast<LPARAM>(modelPtr.get())) != 0) {
            [[maybe_unused]] auto* released = modelPtr.release();
        }
    }
}

}  // namespace neomifes::logmode
