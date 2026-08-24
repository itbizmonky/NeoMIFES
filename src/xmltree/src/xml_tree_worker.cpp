#include "neomifes/xmltree/xml_tree_worker.h"

#include <memory>
#include <utility>

namespace neomifes::xmltree {

XmlTreeWorker::XmlTreeWorker(HWND targetHwnd)
    : m_targetHwnd(targetHwnd), m_thread(&XmlTreeWorker::workerLoop, this) {}

XmlTreeWorker::~XmlTreeWorker() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_shuttingDown = true;
    }
    m_cv.notify_one();
    m_thread.join();
}

void XmlTreeWorker::requestIndex(std::shared_ptr<const document::BufferSnapshot> snapshot,
                                 const void* sessionToken) noexcept {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(
            PendingXmlTreeRequest{.snapshot = std::move(snapshot), .sessionToken = sessionToken});
    }
    m_cv.notify_one();
}

void XmlTreeWorker::workerLoop() {
    // See the same NOLINT in json_tree_worker.cpp/log_index_worker.cpp's
    // workerLoop(): ownership of the heap-allocated XmlTree below is
    // transferred across the PostMessageW/kMsgXmlTreeReady boundary to a
    // different translation unit (normal_mode_wiring.cpp's onAppMessage
    // hook), which the single-TU static analyzer can't see reclaims it.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    while (true) {
        PendingXmlTreeRequest request;
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

        // parseXmlTree() does not throw (xml_tree.h's own contract, and it
        // never returns std::optional - see this file's header comment on
        // xml_tree_worker.h); a genuine std::bad_alloc is allowed to
        // propagate and terminate the process rather than being swallowed
        // here (CLAUDE.md forbids unconditional catch(...)).
        auto resultPtr = std::make_unique<XmlTree>(parseXmlTree(*request.snapshot));

        // Ownership transferred to whichever code handles kMsgXmlTreeReady -
        // it must reconstruct a unique_ptr from this pointer immediately
        // upon receipt. Only released once PostMessageW actually succeeds -
        // if the target window is already gone (e.g. a shutdown race),
        // `resultPtr` stays owned by this unique_ptr and its destructor
        // reclaims the memory instead of leaking it.
        if (::PostMessageW(m_targetHwnd, kMsgXmlTreeReady, reinterpret_cast<WPARAM>(request.sessionToken),
                           reinterpret_cast<LPARAM>(resultPtr.get())) != 0) {
            [[maybe_unused]] auto* released = resultPtr.release();
        }
    }
}

}  // namespace neomifes::xmltree
