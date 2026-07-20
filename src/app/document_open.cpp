#include "neomifes/app/document_open.h"

#include <algorithm>
#include <utility>
#include <variant>

#include "neomifes/core/bookmark_manager.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"

namespace neomifes::app {

namespace {

using document::Document;
using document::LoadError;
using document::LoadResult;
using document::TextPos;

// Same clamp shape as main.cpp's jumpToGotoTarget(); not shared with it
// because that function's input is 1-based (Ctrl+G UI convention) and
// converts internally, while this module's input is already 0-based -
// factoring out a common helper would need a 1-based/0-based adapter at
// one of the two sites for no real benefit given how small this is.
[[nodiscard]] TextPos clampedTargetPosition(const Document& document, document::LineNumber line,
                                            std::uint64_t column) {
    const auto lastLine    = document.lineCount() > 0 ? document.lineCount() - 1 : 0;
    const auto clampedLine = std::min(line, lastLine);
    const auto lineStart   = document.lineToOffset(clampedLine);
    const auto lineEnd     = (clampedLine + 1 < document.lineCount())
                                  ? document.lineToOffset(clampedLine + 1) - 1
                                  : document.length();
    return std::min(lineStart + column, lineEnd);
}

}  // namespace

std::optional<LoadError> openDocumentAt(
    const std::filesystem::path& path, std::optional<document::LineNumber> targetLine,
    std::optional<std::uint64_t> targetColumn, Document& document,
    core::CommandDispatcher& dispatcher, core::SelectionModel& selectionModel,
    core::Viewport& viewport, core::BookmarkManager& bookmarks,
    std::optional<TextPos>& altCursorAnchor, std::optional<TextPos>& rectangularAnchor,
    std::optional<std::uint32_t>& freeCursorVirtualColumns) {
    auto        loaded = document::loadUtf8File(path);
    auto* const result  = std::get_if<LoadResult>(&loaded);
    if (result == nullptr) {
        return std::get<LoadError>(loaded);
    }

    document = std::move(*result->document);
    dispatcher.resetUndoHistory();
    bookmarks.clear();
    altCursorAnchor.reset();
    rectangularAnchor.reset();
    freeCursorVirtualColumns.reset();

    const auto pos = clampedTargetPosition(document, targetLine.value_or(0), targetColumn.value_or(0));
    selectionModel.moveAllTo(pos);
    viewport.ensureVisible(pos, document);
    return std::nullopt;
}

}  // namespace neomifes::app
