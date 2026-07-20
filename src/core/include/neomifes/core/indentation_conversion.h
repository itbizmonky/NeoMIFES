#pragma once

// Headless pure function computing tabs<->spaces conversion edits over a
// document's leading-whitespace indentation, per Phase 4b8d (master_roadmap.md
// §3.7). Deliberately produces core::PerCursorEdit rather than a dedicated
// command class - the result is dispatched through the existing
// core::ReplaceAllCommand (Phase 5b2), which already implements "N
// independent range-replace edits as one undo step" and has no reason to be
// duplicated here (CLAUDE.md rule 6). Only the leading run of tab/space
// characters on each line is touched; whitespace elsewhere in a line's
// content is never considered.

#include <vector>

#include "neomifes/core/edit_commands.h"
#include "neomifes/document/document.h"

namespace neomifes::core {

enum class IndentationConversionTarget {
    TabsToSpaces,
    SpacesToTabs,
};

// tabWidth must be >= 1. A line whose leading whitespace is already in the
// target form is omitted from the result (so callers with no edits can skip
// dispatching entirely, same convention as an empty search-match set).
[[nodiscard]] std::vector<PerCursorEdit> computeIndentationConversionEdits(
    IndentationConversionTarget target, int tabWidth, const document::Document& doc);

}  // namespace neomifes::core
