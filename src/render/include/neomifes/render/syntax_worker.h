#pragma once

// SyntaxWorker - runs syntax::IncrementalParser::reparseDelta() (language
// selected per request, Phase 7d) on a single dedicated background thread
// (Phase 7c, roadmap sec.7.9), so RenderPipeline's UI thread never blocks on
// a re-parse (7a's benchmark: ~6.6s for a full 1,000,000-line parse). This is
// the first std::thread in the codebase -
// detailed_design.md sec.16's thread-safety table and buffer_snapshot.h's
// "safe to hand out to arbitrary threads (search, syntax, plugin workers)"
// comment both already anticipated exactly this consumer.
//
// Phase 7l: true tree-sitter incremental parsing (ts_tree_edit(), Phase 7k's
// syntax::IncrementalParser). Every requestParse() call carries the
// document::EditDelta's (Phase 7k) recorded since the previous call - the
// worker accumulates ALL of them (never drops one, even if several
// requestParse() calls race ahead of the worker picking any of them up) and
// replays them against a retained parse tree, so most edits only cost
// tree-sitter's incremental re-walk of the affected subtrees, not a full
// document re-parse. This is a correctness requirement, not just a
// performance one: skipping even one edit permanently desyncs the retained
// tree's byte-offset bookkeeping from the real document (see
// IncrementalParser's header comment on this hazard) - hence "accumulate,
// never discard" replacing this class's original Phase 7c "keep only the
// latest request, discard whatever else was pending" design.

#include <windows.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/syntax/incremental_parser.h"
#include "neomifes/syntax/syntax.h"

namespace neomifes::render {

// Converts one document::EditDelta (Phase 7k) into the shape tree-sitter's
// TSInputEdit needs (via syntax::ReparseEdit) - pure, stateless bridge
// between Document's UTF-16-code-unit-offset coordinate system and
// syntax::IncrementalParser's byte-offset one. Deliberately lives here
// (neomifes::render), not in neomifes::syntax - ReparseEdit's own header
// comment says this conversion is "the caller's responsibility, kept out of
// this header so neomifes::syntax stays independent of neomifes::document".
// Row/line numbers pass through unscaled; position/column fields are
// doubled (UTF-16 code units -> bytes, this module's existing convention,
// see syntax.cpp's appendLeafToken()).
[[nodiscard]] syntax::ReparseEdit toReparseEdit(const document::EditDelta& delta) noexcept;

// Posted to the target HWND on completion of every requestParse() (see
// below). Lives here (not ui::main_window.h) so that neomifes::render never
// has to depend on neomifes::ui to know its own completion-message value -
// CLAUDE.md's layer rule has Rendering Engine BELOW UI Shell, so render::
// must not include anything from ui::. main.cpp (which already depends on
// both) is the only place that needs to compare against this constant; see
// ui::MainWindowConfig::onAppMessage's doc comment for the other half of
// this handoff. WM_APP+1 is main_window.cpp's own internal kMsgDeferredInit
// - chosen distinct from it, though note there is no single central
// registry of WM_APP+N values in this codebase (only two exist so far).
inline constexpr UINT kMsgSyntaxTokensReady = WM_APP + 2;

class SyntaxWorker {
public:
    // targetHwnd receives kMsgSyntaxTokensReady (above) on completion of
    // every request this instance ever processes. The thread starts
    // immediately and blocks on m_cv until the first requestParse() call or
    // destruction.
    explicit SyntaxWorker(HWND targetHwnd);
    ~SyntaxWorker();

    SyntaxWorker(const SyntaxWorker&)            = delete;
    SyntaxWorker& operator=(const SyntaxWorker&) = delete;
    SyntaxWorker(SyntaxWorker&&)                 = delete;
    SyntaxWorker& operator=(SyntaxWorker&&)      = delete;

    // Fire-and-forget. `edits` are every document::EditDelta recorded since
    // the PREVIOUS requestParse() call (Document::takePendingEdits(), Phase
    // 7k), in chronological order - if the worker hasn't picked up an
    // earlier pending request yet, `edits` is APPENDED to (never replaces)
    // whatever is already queued, so no edit is ever silently dropped (see
    // this file's header comment on why that matters for correctness, not
    // just performance). `snapshot`/`language` always overwrite the pending
    // ones - only the FINAL text/language matters once all queued edits are
    // eventually replayed against it. `resetIncrementalState=true` tells the
    // worker to discard whatever incremental-parse tree it has retained
    // (from any earlier request, picked up or still pending) and start over
    // with a full parse - `edits` is irrelevant in that case (a fresh parser
    // has no tree for ts_tree_edit() to apply them to). Pass this whenever
    // the caller's own document identity might have changed (e.g.
    // RenderPipeline after setLanguage(), see its forceFullReparse) -
    // passing empty `edits` alone is NOT equivalent: an existing retained
    // tree from an unrelated document would otherwise still get reused as
    // tree-sitter's reparse hint, corrupting the result (see
    // IncrementalParser::reparseDelta()'s behavior when a tree is retained).
    // Sticky: once set true for the still-pending batch, stays true until
    // the worker actually picks the batch up, even if a later call in the
    // same batch passes false. Safe to call only from the UI thread (same
    // single-writer assumption as every other RenderPipeline method).
    void requestParse(std::shared_ptr<const document::BufferSnapshot> snapshot,
                      syntax::Language                               language,
                      std::vector<document::EditDelta>               edits,
                      bool                                            resetIncrementalState) noexcept;

private:
    void workerLoop();

    HWND m_targetHwnd;

    // Declared BEFORE m_thread deliberately: member construction follows
    // declaration order regardless of the constructor's init-list order, and
    // the constructor starts m_thread running workerLoop() immediately -
    // workerLoop() must never observe a not-yet-constructed mutex/condition_
    // variable.
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    // Guarded by m_mutex. Set by requestParse(), consumed (and reset to
    // nullptr) by workerLoop() - nullptr means "nothing pending". Kept as a
    // shared_ptr (not std::optional<PendingRequest>) deliberately: clang-tidy's
    // bugprone-unchecked-optional-access can't see that workerLoop()'s
    // std::exchange() below only ever runs after the wait predicate already
    // confirmed non-null, and flags every subsequent access as unchecked -
    // this shared_ptr-null-means-empty shape sidesteps that false positive
    // entirely rather than fighting it with NOLINTs. Always the LATEST
    // snapshot passed to requestParse() (only the final text matters).
    std::shared_ptr<const document::BufferSnapshot> m_pendingSnapshot;
    // Guarded by m_mutex. ACCUMULATES across requestParse() calls (appended,
    // never overwritten) until the worker drains the whole batch in one
    // workerLoop() iteration - Phase 7l's core change from Phase 7c's single
    // overwritten m_pending (see requestParse()'s doc comment on why).
    std::vector<document::EditDelta> m_pendingEdits;
    // Guarded by m_mutex, meaningful only while m_pendingSnapshot != nullptr
    // (Phase 7d - workerLoop() must know which grammar to parse with; always
    // written together with m_pendingSnapshot in requestParse() so they
    // never disagree about which request they describe).
    syntax::Language m_pendingLanguage = syntax::Language::Cpp;
    // Guarded by m_mutex. OR-latched by requestParse() (see its doc comment
    // on stickiness), consumed (and reset to false) by workerLoop().
    bool m_pendingReset = false;
    // Guarded by m_mutex. Set once by the destructor to wake the worker for
    // the final time and tell it to return instead of waiting again.
    bool m_shuttingDown = false;

    std::thread m_thread;
};

}  // namespace neomifes::render
