#include "neomifes/render/syntax_worker.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "neomifes/document/text_pos.h"
#include "neomifes/syntax/incremental_parser.h"
#include "neomifes/syntax/syntax.h"

namespace neomifes::render {

syntax::ReparseEdit toReparseEdit(const document::EditDelta& delta) noexcept {
    return syntax::ReparseEdit{
        .startByte    = static_cast<std::uint32_t>(delta.startPos * 2),
        .oldEndByte   = static_cast<std::uint32_t>(delta.oldEndPos * 2),
        .newEndByte   = static_cast<std::uint32_t>(delta.newEndPos * 2),
        .startRow     = static_cast<std::uint32_t>(delta.startLine),
        .startColumn  = delta.startColumn * 2,
        .oldEndRow    = static_cast<std::uint32_t>(delta.oldEndLine),
        .oldEndColumn = delta.oldEndColumn * 2,
        .newEndRow    = static_cast<std::uint32_t>(delta.newEndLine),
        .newEndColumn = delta.newEndColumn * 2,
    };
}

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

void SyntaxWorker::requestParse(std::shared_ptr<const document::BufferSnapshot> snapshot,
                                syntax::Language                               language,
                                std::vector<document::EditDelta>               edits,
                                bool                                            resetIncrementalState) noexcept {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingSnapshot = std::move(snapshot);
        m_pendingEdits.insert(m_pendingEdits.end(), std::make_move_iterator(edits.begin()),
                              std::make_move_iterator(edits.end()));
        m_pendingLanguage = language;
        m_pendingReset    = m_pendingReset || resetIncrementalState;
    }
    m_cv.notify_one();
}

void SyntaxWorker::workerLoop() {
    // Retained across loop iterations (Phase 7l) - reconstructed wholesale
    // (never a partial reset() on the existing instance) whenever a picked-
    // up batch demands it, since a freshly-constructed IncrementalParser has
    // no retained tree yet, which is exactly what "start over with a full
    // parse" needs (see reparseDelta()'s "edits empty, no tree retained"
    // full-parse path).
    std::optional<syntax::IncrementalParser> parser;
    syntax::Language                          parserLanguage = syntax::Language::Cpp;
    // Phase 7q: the full token list, persisted across loop iterations and
    // updated in place via syntax::applyTokenPatch() - reparseDelta() itself
    // now only returns the DELTA since the previous call (see
    // incremental_parser.h's header comment on why). Cleared whenever
    // `parser` itself is reconstructed (below) - a fresh parser's first
    // reparseDelta() call always returns a "whole document" patch anyway, so
    // merging it against a stale non-empty list here would just be wasted
    // work, not a correctness issue.
    std::vector<syntax::Token> persistedTokens;

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
        std::vector<document::EditDelta>                edits;
        syntax::Language                                 language = syntax::Language::Cpp;
        bool                                              reset    = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_pendingSnapshot != nullptr || m_shuttingDown; });
            if (m_shuttingDown) {
                return;
            }
            snapshot = std::exchange(m_pendingSnapshot, nullptr);
            edits    = std::exchange(m_pendingEdits, {});
            language = m_pendingLanguage;
            reset    = std::exchange(m_pendingReset, false);
        }

        // A fresh parser is also required (regardless of `reset`) the very
        // first time through, and whenever the active language changes - a
        // retained tree from a different grammar is meaningless to
        // ts_tree_edit()/ts_parser_parse_string_encoding() for the new one.
        if (reset || !parser.has_value() || parserLanguage != language) {
            parser.emplace(language);
            parserLanguage = language;
            persistedTokens.clear();
        }

        // Neither extract() nor IncrementalParser::reparseDelta() is noexcept; a
        // genuine std::bad_alloc is allowed to propagate and terminate the
        // process rather than being swallowed here, matching
        // BufferSnapshot::pieceView()'s own documented stance on this
        // (CLAUDE.md forbids unconditional catch(...)).
        const std::u16string text =
            snapshot->extract(document::TextRange{.start = 0, .end = snapshot->length()});

        std::vector<syntax::ReparseEdit> reparseEdits;
        reparseEdits.reserve(edits.size());
        for (const document::EditDelta& delta : edits) {
            reparseEdits.push_back(toReparseEdit(delta));
        }
        // Phase 7q: reparseDelta() returns only what changed (O(edit size)
        // on the tree-sitter side); applyTokenPatch() merges it into this
        // loop's own persisted list - see this function's local variable
        // comment above and incremental_parser.h's header comment for the
        // full rationale, including the benchmark-confirmed caveat that the
        // merge itself is still O(persisted list size), not O(edit size).
        const syntax::TokenPatch patch = parser->reparseDelta(text, reparseEdits);
        persistedTokens = syntax::applyTokenPatch(std::move(persistedTokens), patch);
        auto tokens = std::make_unique<std::vector<syntax::Token>>(persistedTokens);

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
