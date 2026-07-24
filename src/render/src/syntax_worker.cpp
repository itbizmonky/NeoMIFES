#include "neomifes/render/syntax_worker.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "neomifes/document/text_pos.h"
#include "neomifes/syntax/syntax.h"

namespace neomifes::render {

SyntaxWorker::SyntaxWorker(HWND targetHwnd)
    : m_targetHwnd(targetHwnd), m_thread(&SyntaxWorker::workerLoop, this) {}

SyntaxWorker::~SyntaxWorker() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_shuttingDown = true;
    }
    m_cv.notify_one();
    m_thread.join();
}

void SyntaxWorker::requestParse(std::shared_ptr<const document::BufferSnapshot> snapshot) noexcept {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        // Silently supersedes whatever request hadn't been picked up yet -
        // see this class's header comment on why there is no queue.
        m_pending = std::move(snapshot);
    }
    m_cv.notify_one();
}

void SyntaxWorker::workerLoop() {
    // False-positive leak diagnostic anchors here: ownership of the heap-
    // allocated token vector below is transferred across the
    // PostMessageW/kMsgSyntaxTokensReady boundary to main.cpp's
    // onAppMessage hook (a different translation unit), which the
    // single-TU static analyzer can't see reclaims it. The one leak path
    // it CAN see - PostMessageW failing - is already guarded (see the
    // comment on that call below).
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    while (true) {
        std::shared_ptr<const document::BufferSnapshot> snapshot;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_pending != nullptr || m_shuttingDown; });
            if (m_shuttingDown) {
                return;
            }
            snapshot = std::exchange(m_pending, nullptr);
        }

        // Full-document re-parse (no true tree-sitter incremental diffing
        // yet - see this class's header comment). Neither extract() nor
        // parseCpp() is noexcept; a genuine std::bad_alloc is allowed to
        // propagate and terminate the process rather than being swallowed
        // here, matching BufferSnapshot::pieceView()'s own documented
        // stance on this (CLAUDE.md forbids unconditional catch(...)).
        const std::u16string text =
            snapshot->extract(document::TextRange{.start = 0, .end = snapshot->length()});
        auto tokens = std::make_unique<std::vector<syntax::Token>>(syntax::parseCpp(text));

        // Ownership transferred to whichever code handles kMsgSyntaxTokensReady
        // (main.cpp's onAppMessage hook) - it must reconstruct a unique_ptr
        // from this pointer immediately upon receipt. Only released once
        // PostMessageW actually succeeds - if the target window is already
        // gone (e.g. a shutdown race), `tokens` stays owned by this
        // unique_ptr and its destructor reclaims the memory instead of
        // leaking it.
        if (::PostMessageW(m_targetHwnd, kMsgSyntaxTokensReady, 0,
                           reinterpret_cast<LPARAM>(tokens.get())) != 0) {
            [[maybe_unused]] auto* released = tokens.release();
        }
    }
}

}  // namespace neomifes::render
