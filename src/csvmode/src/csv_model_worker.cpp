#include "neomifes/csvmode/csv_model_worker.h"

#include <memory>
#include <utility>

namespace neomifes::csvmode {

CsvModelWorker::CsvModelWorker(HWND targetHwnd)
    : m_targetHwnd(targetHwnd), m_thread(&CsvModelWorker::workerLoop, this) {}

CsvModelWorker::~CsvModelWorker() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_shuttingDown = true;
    }
    m_cv.notify_one();
    m_thread.join();
}

void CsvModelWorker::requestIndex(std::shared_ptr<const document::BufferSnapshot> snapshot,
                                  CsvParseOptions options, const void* sessionToken) noexcept {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(PendingCsvIndexRequest{
            .snapshot = std::move(snapshot), .options = options, .sessionToken = sessionToken});
    }
    m_cv.notify_one();
}

void CsvModelWorker::workerLoop() {
    // See the same NOLINT in log_index_worker.cpp/json_tree_worker.cpp's
    // workerLoop(): ownership of the heap-allocated CsvModel below is
    // transferred across the PostMessageW/kMsgCsvIndexReady boundary to a
    // different translation unit (normal_mode_wiring.cpp's onAppMessage
    // hook), which the single-TU static analyzer can't see reclaims it. The
    // directive must sit on the line immediately above the code it
    // suppresses (NOLINTNEXTLINE only applies to the very next physical
    // line).
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    while (true) {
        PendingCsvIndexRequest request;
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

        // CsvModel::build() is not noexcept; a genuine std::bad_alloc is
        // allowed to propagate and terminate the process rather than being
        // swallowed here (CLAUDE.md forbids unconditional catch(...)),
        // matching LogIndexWorker/JsonTreeWorker's own stance on this.
        auto result = CsvModel::build(*request.snapshot, request.options);
        if (!result.has_value()) {
            // InvalidDelimiter is unreachable via any code path this
            // codebase wires up today (see csv_model_worker.h's own header
            // comment) - no diagnostic surfacing exists for it yet, so
            // silently skip rather than post a result for this request,
            // mirroring LogIndexWorker's identical treatment of
            // LogPatternError::InvalidRegex.
            continue;
        }

        auto modelPtr = std::make_unique<CsvModel>(std::move(*result));

        // Ownership transferred to whichever code handles
        // kMsgCsvIndexReady - it must reconstruct a unique_ptr from this
        // pointer immediately upon receipt. Only released once
        // PostMessageW actually succeeds - if the target window is already
        // gone (e.g. a shutdown race), `modelPtr` stays owned by this
        // unique_ptr and its destructor reclaims the memory instead of
        // leaking it.
        if (::PostMessageW(m_targetHwnd, kMsgCsvIndexReady, reinterpret_cast<WPARAM>(request.sessionToken),
                           reinterpret_cast<LPARAM>(modelPtr.get())) != 0) {
            [[maybe_unused]] auto* released = modelPtr.release();
        }
    }
}

}  // namespace neomifes::csvmode
