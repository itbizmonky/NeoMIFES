#include "neomifes/app/editor_session.h"

#include <utility>
#include <variant>

#include "neomifes/app/document_open.h"
#include "neomifes/app/syntax_language.h"
#include "neomifes/jsontree/json_tree_worker.h"
#include "neomifes/logmode/log_index_worker.h"

namespace neomifes::app {

EditorSession::EditorSession() : EditorSession(document::Document{}, DocumentFileState{}, std::nullopt) {}

EditorSession::EditorSession(document::Document document, DocumentFileState fileState,
                              const std::optional<std::filesystem::path>& path)
    : m_document(std::move(document)),
      m_dispatcher(m_document, m_selection),
      m_viewport(),
      m_folding(),
      m_bookmarks(),
      m_findReplace(),
      m_fileState(fileState),
      m_altCursorAnchor(std::nullopt),
      m_rectangularAnchor(std::nullopt),
      m_freeCursorVirtualColumns(std::nullopt),
      m_path(path.value_or(std::filesystem::path{})),
      m_isUntitled(!path.has_value()) {}

std::optional<syntax::Language> EditorSession::language() const noexcept {
    return m_isUntitled ? std::nullopt : detectLanguage(m_path);
}

std::optional<document::LoadError> EditorSession::openFile(const std::filesystem::path& path,
                                                             std::optional<document::LineNumber> targetLine,
                                                             std::optional<std::uint64_t> targetColumn) {
    auto result = openDocumentAt(path, targetLine, targetColumn, m_document, m_dispatcher, m_selection,
                                  m_viewport, m_bookmarks, m_altCursorAnchor, m_rectangularAnchor,
                                  m_freeCursorVirtualColumns);
    auto* const meta = std::get_if<LoadedFileMeta>(&result);
    if (meta == nullptr) {
        return std::get<document::LoadError>(result);
    }
    m_path       = path;
    m_fileState  = DocumentFileState{
        .encoding = meta->encoding, .lineEnding = meta->lineEnding, .writeBom = meta->hadBom};
    m_isUntitled = false;
    // Deliberately does NOT touch m_findReplace here - pre-WI-04,
    // openDocumentAt() never took a FindReplaceState parameter at all; only
    // resetViewAfterDocumentSwap() (main.cpp, called by the caller right
    // after this returns) clears currentMatches/currentMatchIndex, and it
    // deliberately leaves currentQuery alone. Resetting the whole struct
    // here would additionally clear currentQuery, a behavior change this
    // pure refactor must not introduce.
    return std::nullopt;
}

void EditorSession::beginLogIndexing(logmode::LogIndexWorker& worker, const logmode::LogPatternRule& rule,
                                      std::optional<int> assumedYear) {
    worker.requestIndex(m_document.snapshot(), rule, assumedYear, /*sessionToken=*/this);
    m_logPatternRule   = rule;
    m_logIndexInFlight = true;
}

void EditorSession::beginJsonTreeIndexing(jsontree::JsonTreeWorker& worker) {
    worker.requestIndex(m_document.snapshot(), /*sessionToken=*/this);
    m_jsonTreeIndexInFlight = true;
}

void EditorSession::resetToBlank() {
    m_document = document::Document{};
    m_dispatcher.resetUndoHistory();
    m_bookmarks.clear();
    m_altCursorAnchor.reset();
    m_rectangularAnchor.reset();
    m_freeCursorVirtualColumns.reset();
    m_selection.moveAllTo(0);
    m_viewport.ensureVisible(0, m_document);
    // Deliberately does NOT touch m_findReplace - see openFile()'s comment
    // above (same reasoning: pre-WI-04, handleNewDocumentKey() never
    // touched findReplaceState directly either, only
    // resetViewAfterDocumentSwap() did, and only currentMatches/
    // currentMatchIndex).
    m_fileState = DocumentFileState{};
    m_path.clear();
    m_isUntitled = true;
}

}  // namespace neomifes::app
