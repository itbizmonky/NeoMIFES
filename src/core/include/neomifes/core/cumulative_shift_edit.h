#pragma once

// applyEditsWithCumulativeShift / undoEditsDescending - the cumulative-
// offset apply/undo algorithm shared by MultiCursorEditCommand and
// ReplaceAllCommand (Phase 5b2). Extracted out of MultiCursorEditCommand
// (Phase 4b5a) so ReplaceAllCommand does not have to duplicate it - see
// replace_all_command.h for why a new command class was needed instead of
// reusing MultiCursorEditCommand wholesale (it hard-assumes
// edits.size() == cursorsBefore.size(), which does not hold for replace-all).

#include <span>
#include <string>
#include <vector>

#include "neomifes/core/edit_commands.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}

namespace neomifes::core {

// Applies each edit in `edits` (ascending, non-overlapping document order -
// the caller's responsibility, same contract MultiCursorEditCommand already
// documented) to `doc`, accumulating the shift caused by each edit's length
// delta so every edit's range.start (captured against the pre-command
// document) is translated to its correct position in the currently-
// mutating document. Records the deleted (original) text and each edit's
// actual start position - both needed by undoEditsDescending() - into
// `outDeletedTexts`/`outStartsAtExecute` (resized to edits.size()).
void applyEditsWithCumulativeShift(document::Document& doc, std::span<const PerCursorEdit> edits,
                                    std::vector<std::u16string>&    outDeletedTexts,
                                    std::vector<document::TextPos>& outStartsAtExecute);

// Undoes edits applied by applyEditsWithCumulativeShift(), highest offset
// first: undoing the highest-offset edit never shifts the (lower) offsets
// the remaining undos still need, so `startsAtExecute` stays valid without
// recomputing a shift.
void undoEditsDescending(document::Document& doc, std::span<const PerCursorEdit> edits,
                          std::span<const std::u16string>    deletedTexts,
                          std::span<const document::TextPos> startsAtExecute);

}  // namespace neomifes::core
