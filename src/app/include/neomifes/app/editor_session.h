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
#include "neomifes/csvmode/csv_model.h"
#include "neomifes/csvmode/csv_row_order.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/document/text_pos.h"
#include "neomifes/encoding/encoding.h"
#include "neomifes/git/git_repository.h"
#include "neomifes/jsontree/json_tree.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"
#include "neomifes/search/search_service.h"
#include "neomifes/syntax/syntax.h"
#include "neomifes/xmltree/xml_tree.h"

namespace neomifes::logmode {
class LogIndexWorker;
}  // namespace neomifes::logmode

namespace neomifes::jsontree {
class JsonTreeWorker;
}  // namespace neomifes::jsontree

namespace neomifes::xmltree {
class XmlTreeWorker;
}  // namespace neomifes::xmltree

namespace neomifes::csvmode {
class CsvModelWorker;
}  // namespace neomifes::csvmode

namespace neomifes::git {
class GitDiffWorker;
}  // namespace neomifes::git

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

    // WI-15b: per-tab JSON-tree state. Always constructed, conditionally
    // populated - same "always there, empty until a feature turns it on"
    // pattern as m_logModel above. No WI-15c-scoped "disable" method exists
    // yet (mirrors WI-14b's own scoping: logModel()'s disableLogMode()
    // counterpart wasn't added until WI-14c shipped alongside the command
    // that calls it - see build_plan.md's WI-15b section for why this WI
    // deliberately stops short of that).
    //
    // Unlike logModel(), std::nullopt here is NOT reserved for "never
    // indexed" - it also covers "indexed, but the document wasn't valid
    // JSON" (parseJsonTree()'s own std::nullopt contract). jsonTree() alone
    // cannot distinguish those two cases; jsonTreeIndexInFlight() combined
    // with "has a JSON-tree indexing request ever been issued for this
    // session" (a WI-15c UI concern, not tracked here) is what a caller
    // would need to tell them apart.
    [[nodiscard]] const std::optional<jsontree::JsonNode>& jsonTree() const noexcept { return m_jsonTree; }
    [[nodiscard]] bool jsonTreeIndexInFlight() const noexcept { return m_jsonTreeIndexInFlight; }

    // Fires an async JsonTreeWorker request for this session's current
    // document snapshot, using `this` as the opaque sessionToken (see
    // json_tree_worker.h - never dereferenced by the worker, only round-
    // tripped back via kMsgJsonTreeReady's wParam so the receiver can find
    // this exact EditorSession again). Sets jsonTreeIndexInFlight()
    // immediately; jsonTree() is populated later by applyJsonTreeResult()
    // once the worker's response arrives - see that method's own comment on
    // why a std::nullopt response is a normal, expected outcome here (unlike
    // beginLogIndexing()'s LogPatternRule, there is no per-request
    // configuration to pass).
    void beginJsonTreeIndexing(jsontree::JsonTreeWorker& worker);

    // Called by the kMsgJsonTreeReady receiver once it has matched a
    // response's sessionToken back to this EditorSession. `result` is
    // std::nullopt whenever the document wasn't valid JSON - JsonTreeWorker
    // (unlike LogIndexWorker) always posts a response, precisely so this
    // method is reachable and jsonTreeIndexInFlight() never gets stuck at
    // true.
    void applyJsonTreeResult(std::optional<jsontree::JsonNode> result) noexcept {
        m_jsonTree              = std::move(result);
        m_jsonTreeIndexInFlight = false;
    }

    // WI-15g: per-tab XML-tree state. Always constructed, conditionally
    // populated - same "always there, empty until a feature turns it on"
    // pattern as m_jsonTree/m_logModel above. No "disable" method exists yet
    // (mirrors WI-15b's own scoping: the counterpart wasn't added until the
    // sub-WI that ships the command calling it).
    //
    // Unlike jsonTree(), std::nullopt here IS reserved for "never indexed"
    // only - xml_tree.h's own contract is that parseXmlTree() never fails
    // (it always returns a real XmlTree, using XmlNodeKind::Error as an
    // in-band sentinel rather than an out-of-band std::optional failure -
    // see that header's own comment). So xmlTree() reaching a populated
    // state is the only way it becomes non-nullopt; a caller that wants to
    // know whether the document parsed cleanly checks the populated
    // XmlTree::hasErrors field, not the outer std::optional.
    [[nodiscard]] const std::optional<xmltree::XmlTree>& xmlTree() const noexcept { return m_xmlTree; }
    [[nodiscard]] bool xmlTreeIndexInFlight() const noexcept { return m_xmlTreeIndexInFlight; }

    // Fires an async XmlTreeWorker request for this session's current
    // document snapshot, using `this` as the opaque sessionToken (see
    // xml_tree_worker.h - never dereferenced by the worker, only round-
    // tripped back via kMsgXmlTreeReady's wParam so the receiver can find
    // this exact EditorSession again). Sets xmlTreeIndexInFlight()
    // immediately; xmlTree() is populated later by applyXmlTreeResult() once
    // the worker's response arrives.
    void beginXmlTreeIndexing(xmltree::XmlTreeWorker& worker);

    // Called by the kMsgXmlTreeReady receiver once it has matched a
    // response's sessionToken back to this EditorSession. `result` is
    // always a real XmlTree (never absent) - XmlTreeWorker always posts,
    // and unlike applyJsonTreeResult()'s parameter this one is not itself
    // optional (see xmlTree()'s own comment on where the "did this parse
    // cleanly" signal actually lives).
    void applyXmlTreeResult(xmltree::XmlTree result) noexcept {
        m_xmlTree              = std::move(result);
        m_xmlTreeIndexInFlight = false;
    }

    // WI-16b: per-tab CSV-model state. Always constructed, conditionally
    // populated - same "always there, empty until a feature turns it on"
    // pattern as m_logModel/m_jsonTree above. No "disable" method exists yet
    // (mirrors WI-14b/WI-15b's own scoping: the counterpart wasn't added
    // until the sub-WI that ships the command calling it).
    //
    // Unlike jsonTree(), std::nullopt here IS reserved for "never indexed"
    // only - csv_model.h's own contract is that CsvModel::build() fails
    // solely on a caller configuration mistake (an invalid delimiter), never
    // on document content, and CsvModelWorker (unlike JsonTreeWorker) drops
    // that failure rather than posting it (see csv_model_worker.h). So
    // csvModel() reaching a populated state is the only way it becomes
    // non-nullopt.
    [[nodiscard]] const std::optional<csvmode::CsvModel>& csvModel() const noexcept { return m_csvModel; }
    [[nodiscard]] bool csvIndexInFlight() const noexcept { return m_csvIndexInFlight; }

    // Fires an async CsvModelWorker request for this session's current
    // document snapshot, using `this` as the opaque sessionToken (see
    // csv_model_worker.h - never dereferenced by the worker, only round-
    // tripped back via kMsgCsvIndexReady's wParam so the receiver can find
    // this exact EditorSession again). Sets csvIndexInFlight() immediately;
    // csvModel() is populated later by applyCsvIndexResult() once the
    // worker's response arrives.
    void beginCsvIndexing(csvmode::CsvModelWorker& worker, const csvmode::CsvParseOptions& options);

    // Called by the kMsgCsvIndexReady receiver once it has matched a
    // response's sessionToken back to this EditorSession. Unlike
    // applyJsonTreeResult(), CsvModelWorker never posts a failure (see this
    // class's csvModel() comment), so `result` here is always a
    // successfully-built CsvModel. Defined in the .cpp (not inline like
    // WI-16b left it) because it now also calls recomputeCsvRowOrder().
    void applyCsvIndexResult(csvmode::CsvModel result) noexcept;

    // WI-17b: per-tab Git diff state. Always constructed, conditionally
    // populated - same "always there, empty until a feature turns it on"
    // pattern as m_logModel/m_jsonTree/m_csvModel above.
    //
    // Unlike csvModel(), std::nullopt here is NOT reserved for "never
    // requested" - it also covers "requested, but this file isn't inside any
    // Git repository / isn't tracked at HEAD" (GitRepository::discover()/
    // diffAgainstHead()'s own std::nullopt contracts, both collapsed the
    // same way GitDiffWorker always posts even a std::nullopt result - see
    // git_diff_worker.h's own header comment). gitDiff() alone cannot
    // distinguish those cases, same acceptable ambiguity jsonTree() already
    // has toward "never indexed" vs "not valid JSON".
    [[nodiscard]] const std::optional<std::vector<git::LineDiffRegion>>& gitDiff() const noexcept {
        return m_gitDiff;
    }
    [[nodiscard]] bool gitDiffIndexInFlight() const noexcept { return m_gitDiffIndexInFlight; }

    // Fires an async GitDiffWorker request for this session's current
    // document snapshot, using `this` as the opaque sessionToken (see
    // git_diff_worker.h - never dereferenced by the worker, only round-
    // tripped back via kMsgGitDiffReady's wParam so the receiver can find
    // this exact EditorSession again). A no-op for an Untitled buffer
    // (pathIfNamed() == std::nullopt) - Git fundamentally cannot diff
    // content that has never been saved to a path, so neither
    // gitDiffIndexInFlight() nor gitDiff() change in that case. Otherwise
    // sets gitDiffIndexInFlight() immediately; gitDiff() is populated later
    // by applyGitDiffResult() once the worker's response arrives.
    void beginGitDiffIndexing(git::GitDiffWorker& worker);

    // Called by the kMsgGitDiffReady receiver once it has matched a
    // response's sessionToken back to this EditorSession. `result` is
    // std::nullopt whenever this file isn't inside a Git repository (or
    // isn't tracked at HEAD) - GitDiffWorker (like JsonTreeWorker, unlike
    // CsvModelWorker) always posts a response, precisely so this method is
    // reachable and gitDiffIndexInFlight() never gets stuck at true.
    void applyGitDiffResult(std::optional<std::vector<git::LineDiffRegion>> result) noexcept {
        m_gitDiff              = std::move(result);
        m_gitDiffIndexInFlight = false;
    }

    // WI-16e: this session's current CSV grid filter/sort configuration -
    // always present (default-constructed = "no filter, unsorted"), one per
    // tab, same "always there" shape as m_logLevelFilterMask above. A
    // caller reopening CsvGridPane for this session (after a tab switch)
    // reads these back to restore the tab's own filter text / sort-arrow
    // column, rather than the pane silently keeping whatever the
    // PREVIOUSLY active tab had shown.
    [[nodiscard]] const csvmode::CsvFilterOptions& csvFilter() const noexcept { return m_csvFilter; }
    [[nodiscard]] const csvmode::CsvSortOptions&   csvSort() const noexcept { return m_csvSort; }

    // The filtered+sorted data-row-index order derived from
    // csvModel()/csvFilter()/csvSort() - CsvGridPane's virtual-mode
    // LVN_GETDISPINFOW fires this lookup once per VISIBLE CELL PER REPAINT,
    // and computeCsvRowOrder() is O(csvModel()->dataRowCount()) (WI-16d
    // benchmarked it at 569ms/1,214ms for a 1,000,000-row filter/sort), so
    // recomputing it inside that callback would be catastrophic. This is
    // therefore a CACHE: always kept in sync with the CURRENT
    // csvFilter()/csvSort()/csvModel() by construction - setCsvFilter()/
    // setCsvSort()/applyCsvIndexResult() are the only 3 ways any of those 3
    // inputs can change, and all 3 recompute this in the same call, so
    // there is no separate "dirty" flag to track. Empty until csvModel()
    // is populated.
    [[nodiscard]] const std::vector<std::size_t>& csvRowOrder() const noexcept { return m_csvRowOrder; }
    // Replaces csvFilter() and recomputes csvRowOrder() before returning -
    // a caller can never observe the two out of sync with each other.
    void setCsvFilter(csvmode::CsvFilterOptions filter);
    // Replaces csvSort() and recomputes csvRowOrder() before returning.
    void setCsvSort(csvmode::CsvSortOptions sort);

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
    // FindDialog dependency).
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
    std::optional<jsontree::JsonNode>      m_jsonTree;
    bool                                   m_jsonTreeIndexInFlight = false;
    std::optional<csvmode::CsvModel>       m_csvModel;
    bool                                   m_csvIndexInFlight = false;
    csvmode::CsvFilterOptions              m_csvFilter;
    csvmode::CsvSortOptions                m_csvSort;
    std::vector<std::size_t>               m_csvRowOrder;
    std::optional<std::vector<git::LineDiffRegion>> m_gitDiff;
    bool                                              m_gitDiffIndexInFlight = false;
    std::optional<xmltree::XmlTree>        m_xmlTree;
    bool                                    m_xmlTreeIndexInFlight = false;

    // WI-16e: the single place that keeps m_csvRowOrder in sync with
    // m_csvModel/m_csvFilter/m_csvSort - see csvRowOrder()'s own comment
    // for why this recompute-on-every-input-change contract exists instead
    // of a lazy/dirty-flag design.
    void recomputeCsvRowOrder();
};

}  // namespace neomifes::app
