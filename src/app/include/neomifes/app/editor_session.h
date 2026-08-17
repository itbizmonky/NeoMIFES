#pragma once

// EditorSession - "one open document"'s complete state (WI-04): one tab ==
// one EditorSession. Introduced to let src/app/main.cpp shed the ~15
// document-scoped local variables wWinMain used to hold directly (Document/
// SelectionModel/CommandDispatcher/Viewport/FoldingModel/BookmarkManager/
// find-replace state/file path) - WI-05's tab UI needs more than one of
// these alive at once.
//
// Move/copy DISABLED: core::CommandDispatcher captures &m_document/
// &m_selection as raw pointers at construction time (see
// core::ExecutionContext in command.h) and never re-resolves them. Moving
// or copying an EditorSession would leave those pointers dangling. Callers
// that need more than one session (Workspace, workspace.h) must therefore
// keep each EditorSession heap-allocated behind a stable address (e.g.
// std::vector<std::unique_ptr<EditorSession>>) rather than storing this
// type by value in a container that may reallocate/relocate its elements.
//
// language() is deliberately NOT cached: every existing call site in
// main.cpp (pre-WI-04) recomputes syntax::Language fresh from the current
// path via neomifes::app::detectLanguage() rather than storing it anywhere -
// caching it here would introduce a second copy that could drift out of
// sync with m_path/m_isUntitled (the same class of bug CLAUDE.md flags for
// kTabWidth's pre-existing duplicate-definition debt), so this class keeps
// the existing "always derive it" behavior instead.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "neomifes/core/bookmark_manager.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/document/text_pos.h"
#include "neomifes/encoding/encoding.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"
#include "neomifes/search/search_service.h"
#include "neomifes/syntax/syntax.h"

namespace neomifes::logmode {
class LogIndexWorker;
}  // namespace neomifes::logmode

namespace neomifes::app {

// WI-02: the document::saveFile()-relevant metadata for the currently open
// document - what Ctrl+S should reuse without prompting. Moved verbatim
// from main.cpp (WI-04) - same fields, same defaults.
struct DocumentFileState {
    encoding::Encoding   encoding   = encoding::Encoding::Utf8;
    encoding::LineEnding lineEnding = encoding::LineEnding::Crlf;  // build_plan.md: new document default
    bool                  writeBom   = false;
};

// Bundles the Find/Replace feature's session-lifetime state (Phase 5b3b).
// Moved verbatim from main.cpp (WI-04) - resetViewAfterDocumentSwap()'s
// pre-WI-04 behavior (clearing currentMatches/currentMatchIndex on every
// document swap) is exactly why this is scoped to EditorSession rather than
// shared across the whole Workspace, per WI-04's plan.
struct FindReplaceState {
    search::Query               currentQuery;
    std::vector<search::Match>  currentMatches;
    std::size_t                  currentMatchIndex = 0;
};

class EditorSession {
public:
    // A blank, untitled session (default Document, default DocumentFileState,
    // no path).
    EditorSession();

    // Builds a session directly from a Document already prepared by the
    // caller (main.cpp's prepareDocument(), used for the process's startup
    // document). `path` is the file it was loaded from, if any (nullopt for
    // a synthesized/empty/failed-load document) - determines isUntitled().
    EditorSession(document::Document document, DocumentFileState fileState,
                  const std::optional<std::filesystem::path>& path);

    EditorSession(const EditorSession&)            = delete;
    EditorSession& operator=(const EditorSession&) = delete;
    // See this file's header comment: CommandDispatcher's captured
    // Document*/SelectionModel* would dangle across a move.
    EditorSession(EditorSession&&)            = delete;
    EditorSession& operator=(EditorSession&&) = delete;
    ~EditorSession() = default;

    [[nodiscard]] document::Document&       document() noexcept { return m_document; }
    [[nodiscard]] const document::Document& document() const noexcept { return m_document; }
    [[nodiscard]] core::SelectionModel&       selection() noexcept { return m_selection; }
    [[nodiscard]] const core::SelectionModel& selection() const noexcept { return m_selection; }
    [[nodiscard]] core::CommandDispatcher& dispatcher() noexcept { return m_dispatcher; }
    [[nodiscard]] core::Viewport&           viewport() noexcept { return m_viewport; }
    [[nodiscard]] const core::Viewport&     viewport() const noexcept { return m_viewport; }
    [[nodiscard]] core::FoldingModel&       folding() noexcept { return m_folding; }
    [[nodiscard]] const core::FoldingModel& folding() const noexcept { return m_folding; }
    [[nodiscard]] core::BookmarkManager& bookmarks() noexcept { return m_bookmarks; }

    [[nodiscard]] FindReplaceState&       findReplaceState() noexcept { return m_findReplace; }
    [[nodiscard]] const FindReplaceState& findReplaceState() const noexcept { return m_findReplace; }
    [[nodiscard]] DocumentFileState&       fileState() noexcept { return m_fileState; }
    [[nodiscard]] const DocumentFileState& fileState() const noexcept { return m_fileState; }

    [[nodiscard]] std::optional<document::TextPos>& altCursorAnchor() noexcept {
        return m_altCursorAnchor;
    }
    [[nodiscard]] std::optional<document::TextPos>& rectangularAnchor() noexcept {
        return m_rectangularAnchor;
    }
    [[nodiscard]] std::optional<std::uint32_t>& freeCursorVirtualColumns() noexcept {
        return m_freeCursorVirtualColumns;
    }

    // WI-07 step5: true while this session is in Overwrite (OVR) mode -
    // VK_INSERT toggles it (normal_mode_wiring.cpp), handleCharEvent()
    // branches between handleChar()/applyOverwriteChar() on it, and the
    // status bar's INS/OVR part (buildStatusBarParts()) reads it. Session-
    // lifetime UI state, not document state - same reasoning as
    // altCursorAnchor()/rectangularAnchor()/freeCursorVirtualColumns()
    // above (this class's own header comment on why those live here). Const
    // overload returns by value (not const bool&) - same "read-only view of
    // a plain bool" shape isDirty() already uses - so buildStatusBarParts()
    // (which only reads, given a const EditorSession&) can call this too.
    [[nodiscard]] bool& overwriteMode() noexcept { return m_overwriteMode; }
    [[nodiscard]] bool  overwriteMode() const noexcept { return m_overwriteMode; }

    // WI-14b: per-tab log-mode state. Always constructed, conditionally
    // populated - same "always there, empty until a feature turns it on"
    // pattern as m_folding/m_bookmarks above. std::optional (not a bare
    // LogModel) because "log mode never enabled for this tab" and "enabled,
    // 0 matching lines" are distinct states a plain LogModel::lines().empty()
    // check couldn't tell apart. WI-14c wires the "Log: Enable/Disable"
    // commands (normal_mode_wiring.cpp) that populate/clear this.
    //
    // Known limitation (documented, not solved here, WI-14c): logModel()
    // does NOT track document edits after indexing - same reasoning as
    // core::BookmarkManager's own documented gap (bookmark_manager.h): this
    // codebase has no edit-event/observer mechanism to subscribe to, only
    // Document::version() polling. Editing a document after enabling log
    // mode can leave line-number-keyed color-coding/filtering/jump targets
    // pointing at the wrong lines until the user re-runs a "Log: Enable"
    // command to re-index.
    [[nodiscard]] const std::optional<logmode::LogModel>& logModel() const noexcept { return m_logModel; }
    [[nodiscard]] const std::optional<logmode::LogPatternRule>& logPatternRule() const noexcept {
        return m_logPatternRule;
    }
    [[nodiscard]] bool logIndexInFlight() const noexcept { return m_logIndexInFlight; }

    // Fires an async LogIndexWorker request for this session's current
    // document snapshot, using `this` as the opaque sessionToken (see
    // log_index_worker.h - never dereferenced by the worker, only round-
    // tripped back via kMsgLogIndexReady's wParam so the receiver can find
    // this exact EditorSession again). Sets logPatternRule()/
    // logIndexInFlight() immediately; logModel() is populated later by
    // applyLogIndexResult() once the worker's response arrives.
    void beginLogIndexing(logmode::LogIndexWorker& worker, const logmode::LogPatternRule& rule,
                          std::optional<int> assumedYear);

    // Called by the kMsgLogIndexReady receiver once it has matched a
    // response's sessionToken back to this EditorSession.
    void applyLogIndexResult(logmode::LogModel result) noexcept {
        m_logModel         = std::move(result);
        m_logIndexInFlight = false;
    }

    // WI-14c: per-tab log-level filter mask (bit i = logmode::
    // logLevelFilterBit(LogLevel(i))). Meaningless while logModel() is
    // nullopt - RenderPipeline::isLineHidden() only consults its own copy
    // of this value when it also holds a non-empty per-line level array
    // (see pushLogVisualsForSession(), normal_mode_wiring.cpp). Mutable
    // reference accessor, same shape as overwriteMode() above - the toggle/
    // preset logic itself lives in normal_mode_wiring.cpp's palette
    // commands, not here (this class stays thin storage, matching
    // overwriteMode()'s own division of responsibility).
    [[nodiscard]] std::uint8_t& logLevelFilterMask() noexcept { return m_logLevelFilterMask; }
    [[nodiscard]] std::uint8_t  logLevelFilterMask() const noexcept { return m_logLevelFilterMask; }

    // WI-14c: "Log: Disable" command. Symmetric with beginLogIndexing() -
    // clears logModel()/logPatternRule() and resets the filter mask to
    // "show everything", so a later beginLogIndexing() call starts from a
    // clean slate rather than an old filter selection silently carrying
    // over. Does not cancel an in-flight LogIndexWorker request (there is
    // no cancellation mechanism - see log_index_worker.h); a stale response
    // arriving afterward simply gets applied and then immediately
    // superseded by whatever the next real action does, same as any other
    // "fire and forget" async result in this codebase.
    void disableLogMode() noexcept {
        m_logModel.reset();
        m_logPatternRule.reset();
        m_logIndexInFlight   = false;
        m_logLevelFilterMask = logmode::kAllLogLevelsVisible;
    }

    // Always derived from path()/isUntitled() - see this file's header
    // comment on why this is not cached.
    [[nodiscard]] std::optional<syntax::Language> language() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }
    [[nodiscard]] bool isUntitled() const noexcept { return m_isUntitled; }
    [[nodiscard]] bool isDirty() const noexcept { return m_document.isDirty(); }

    // path(), wrapped as the optional<path> shape several existing
    // call sites already need (detectLanguage()/showSaveFileDialog()'s own
    // parameter conventions predate this class) - nullopt when
    // isUntitled(), path() otherwise. A small convenience accessor, not new
    // behavior: every caller of this was already writing this exact ternary
    // inline before WI-04.
    [[nodiscard]] std::optional<std::filesystem::path> pathIfNamed() const {
        return m_isUntitled ? std::nullopt : std::optional<std::filesystem::path>(m_path);
    }

    // Ctrl+Shift+S (Save As): records where this session's Document was just
    // saved, without loading anything (unlike openFile() above, which both
    // loads AND updates path/fileState together) - performSave() already
    // saved the document's CURRENT content to `path` via
    // document::saveFile() before calling this, so there is nothing left to
    // load here, only bookkeeping to update.
    void setSavedPath(std::filesystem::path path) noexcept {
        m_path       = std::move(path);
        m_isUntitled = false;
    }

    // Thin wrapper around document_open.h's openDocumentAt(): loads `path`
    // into this session's Document in place (preserving the CommandDispatcher
    // pointer-stability contract - see that function's own header comment).
    // On success, updates path()/fileState(). Does NOT touch
    // findReplaceState() - pre-WI-04, opening a file never reset it directly
    // either; only the caller's resetViewAfterDocumentSwap() (main.cpp)
    // clears currentMatches/currentMatchIndex afterward, deliberately
    // leaving currentQuery alone. On failure, returns the LoadError and
    // leaves every piece of session state untouched (openDocumentAt()'s own
    // contract, preserved here).
    [[nodiscard]] std::optional<document::LoadError> openFile(
        const std::filesystem::path& path,
        std::optional<document::LineNumber> targetLine   = std::nullopt,
        std::optional<std::uint64_t>        targetColumn = std::nullopt);

    // Ctrl+N: resets this session to a fresh blank document IN PLACE
    // (move-assignment onto the existing Document member, not a new
    // EditorSession - CommandDispatcher is bound to this specific Document
    // instance). Mirrors main.cpp's pre-WI-04 handleNewDocumentKey() reset
    // sequence verbatim: resetUndoHistory()/bookmarks.clear()/both selection
    // anchors reset/freeCursorVirtualColumns reset/selection moved to 0/
    // viewport made visible at 0/fileState and path cleared. Does NOT reset
    // findReplaceState() (same reasoning as openFile() above) or
    // folding/RenderPipeline-side visuals (the caller's responsibility, same
    // division as openFile(), since EditorSession has no RenderPipeline/
    // FindBar dependency).
    void resetToBlank();

private:
    document::Document       m_document;
    core::SelectionModel     m_selection;
    core::CommandDispatcher  m_dispatcher;  // binds &m_document/&m_selection - keep declaration order
    core::Viewport           m_viewport;
    core::FoldingModel       m_folding;
    core::BookmarkManager    m_bookmarks;
    FindReplaceState         m_findReplace;
    DocumentFileState        m_fileState;
    std::optional<document::TextPos>      m_altCursorAnchor;
    std::optional<document::TextPos>      m_rectangularAnchor;
    std::optional<std::uint32_t>          m_freeCursorVirtualColumns;
    std::filesystem::path                 m_path;
    bool                                   m_isUntitled = true;
    bool                                   m_overwriteMode = false;
    std::optional<logmode::LogModel>       m_logModel;
    std::optional<logmode::LogPatternRule> m_logPatternRule;
    bool                                   m_logIndexInFlight = false;
    std::uint8_t                           m_logLevelFilterMask = logmode::kAllLogLevelsVisible;
};

}  // namespace neomifes::app
