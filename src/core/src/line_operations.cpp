#include "neomifes/core/line_operations.h"

#include <algorithm>

#include "neomifes/document/document.h"

namespace neomifes::core {

namespace {

struct CursorLineInfo {
    document::LineNumber line;
    std::uint32_t         column;
};

std::vector<CursorLineInfo> cursorLineInfos(const document::Document& document, std::span<const Cursor> cursors) {
    std::vector<CursorLineInfo> infos;
    infos.reserve(cursors.size());
    for (const Cursor& cursor : cursors) {
        const document::LineNumber line = document.offsetToLine(cursor.position);
        const auto column = static_cast<std::uint32_t>(cursor.position - document.lineToOffset(line));
        infos.push_back({.line = line, .column = column});
    }
    return infos;
}

// Sorted, de-duplicated line numbers touched by `infos`, plus (for each
// original cursor, by index) which entry of that sorted vector it maps to.
struct DistinctLines {
    std::vector<document::LineNumber> lines;              // ascending, unique
    std::vector<std::size_t>          cursorToLineIndex;  // infos.size() entries
};

DistinctLines collectDistinctLines(std::span<const CursorLineInfo> infos) {
    DistinctLines result;
    result.cursorToLineIndex.resize(infos.size());
    result.lines.reserve(infos.size());
    for (const auto& info : infos) {
        result.lines.push_back(info.line);
    }
    std::ranges::sort(result.lines);
    const auto dupRange = std::ranges::unique(result.lines);
    result.lines.erase(dupRange.begin(), dupRange.end());
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const auto it = std::ranges::lower_bound(result.lines, infos[i].line);
        result.cursorToLineIndex[i] = static_cast<std::size_t>(it - result.lines.begin());
    }
    return result;
}

struct LineRun {
    document::LineNumber start;
    std::uint64_t         count;
};

// Groups strictly ascending, unique `lines` into maximal runs of
// consecutive line numbers - e.g. {2,3,5,8,9,10} -> {2,2},{5,1},{8,3}.
std::vector<LineRun> groupIntoContiguousRuns(std::span<const document::LineNumber> lines) {
    std::vector<LineRun> runs;
    for (const document::LineNumber line : lines) {
        if (!runs.empty() && runs.back().start + runs.back().count == line) {
            ++runs.back().count;
        } else {
            runs.push_back(LineRun{.start = line, .count = 1});
        }
    }
    return runs;
}

// Appends one no-op edit per distinct line in `run`, pinning every cursor on
// it to its own unchanged position - used when a run is blocked from moving
// (already at the document edge in the requested direction). Still routed
// through applyEditsWithCumulativeShift() like every other edit, so it stays
// correctly positioned even though other runs in this same call are moving
// elsewhere in the document. Split out of computeMoveLineEdits() to keep
// that function's cognitive complexity under clang-tidy's threshold.
void appendBlockedRunEdits(const document::Document& document, const LineRun& run, LineOperationPlan& plan,
                           std::span<std::size_t> lineToEditIndex, std::span<std::uint32_t> lineToBaseOffset,
                           std::size_t& distinctIndex) {
    for (document::LineNumber line = run.start; line < run.start + run.count; ++line, ++distinctIndex) {
        const document::TextPos pos       = document.lineToOffset(line);
        const std::size_t        editIndex = plan.edits.size();
        plan.edits.push_back(PerCursorEdit{.range = {.start = pos, .end = pos}, .insertedText = u""});
        lineToEditIndex[distinctIndex]  = editIndex;
        lineToBaseOffset[distinctIndex] = 0;
    }
}

// The single combined edit that swaps a run of lines with one adjacent line,
// plus where each of the run's own lines landed within the replacement text
// (so computeMoveLineEdits() can place each cursor's column correctly).
struct RunMoveEdit {
    document::TextRange        range;
    std::u16string              replacement;
    std::vector<std::uint32_t> lineOffsetsInReplacement;
};

// Swaps [run.start, run.start+run.count) UP with the line immediately above
// it (run.start-1). That line always has its own trailing '\n' in the
// ORIGINAL document (something - the run - always follows it), but after the
// swap it occupies the LAST position of the span, so it drops that '\n'
// exactly when the run's own last line WAS the document's actual last line
// (i.e. is taking over as the new last line).
RunMoveEdit buildMoveUpEdit(const document::Document& document, const LineRun& run, std::uint64_t totalLines) {
    RunMoveEdit result;
    result.lineOffsetsInReplacement.reserve(run.count);
    const document::LineNumber aboveLine        = run.start - 1;
    const std::u16string        aboveLineText    = document.lineText(aboveLine);
    const bool                  blockWasAtDocEnd = (run.start + run.count >= totalLines);
    result.range = {.start = document.lineToOffset(aboveLine),
                    .end   = blockWasAtDocEnd ? document.length() : document.lineToOffset(run.start + run.count)};

    for (document::LineNumber line = run.start; line < run.start + run.count; ++line) {
        result.lineOffsetsInReplacement.push_back(static_cast<std::uint32_t>(result.replacement.size()));
        result.replacement += document.lineText(line);
        result.replacement += u'\n';
    }
    result.replacement += aboveLineText;
    if (!blockWasAtDocEnd) {
        result.replacement += u'\n';
    }
    return result;
}

// Swaps [run.start, run.start+run.count) DOWN with the line immediately
// below it (run.start+run.count). Symmetric to buildMoveUpEdit(): that line
// moves to the FIRST position of the span, so it never drops its own
// separator, but the run's own last line takes over as the new document end
// (dropping ITS trailing '\n') exactly when the swapped-up line WAS the
// document's actual last line.
RunMoveEdit buildMoveDownEdit(const document::Document& document, const LineRun& run, std::uint64_t totalLines) {
    RunMoveEdit result;
    result.lineOffsetsInReplacement.reserve(run.count);
    const document::LineNumber belowLine     = run.start + run.count;
    const std::u16string        belowLineText = document.lineText(belowLine);
    const bool                  belowWasLast  = (belowLine + 1 >= totalLines);
    result.range = {.start = document.lineToOffset(run.start),
                    .end   = belowWasLast ? document.length() : document.lineToOffset(belowLine + 1)};

    result.replacement += belowLineText;
    result.replacement += u'\n';
    for (document::LineNumber line = run.start; line < run.start + run.count; ++line) {
        result.lineOffsetsInReplacement.push_back(static_cast<std::uint32_t>(result.replacement.size()));
        result.replacement += document.lineText(line);
        const bool isRunLastLine = (line + 1 == run.start + run.count);
        if (!isRunLastLine || !belowWasLast) {
            result.replacement += u'\n';
        }
    }
    return result;
}

}  // namespace

LineOperationPlan computeDuplicateLineEdits(const document::Document& document, std::span<const Cursor> cursors) {
    LineOperationPlan plan;
    if (cursors.empty()) {
        return plan;
    }
    const std::vector<CursorLineInfo> infos    = cursorLineInfos(document, cursors);
    const DistinctLines               distinct = collectDistinctLines(infos);

    plan.edits.reserve(distinct.lines.size());
    for (const document::LineNumber line : distinct.lines) {
        const std::u16string    lineText   = document.lineText(line);
        const document::TextPos contentEnd = document.lineToOffset(line) + lineText.size();
        // Inserted right before any '\n' that already terminates this
        // line (or at the true document end for the last line, which has
        // none) - works uniformly for both cases, see this header's
        // computeDuplicateLineEdits() comment.
        plan.edits.push_back(PerCursorEdit{.range         = {.start = contentEnd, .end = contentEnd},
                                           .insertedText = u"\n" + lineText});
    }

    plan.cursorMappings.reserve(cursors.size());
    for (std::size_t i = 0; i < cursors.size(); ++i) {
        // +1 skips the leading '\n' this edit's insertedText begins with,
        // so the cursor lands at the same column within the DUPLICATE,
        // not at the very start of the inserted text.
        plan.cursorMappings.push_back(
            CursorEditMapping{.editIndex             = distinct.cursorToLineIndex[i],
                              .offsetIntoInsertedText = infos[i].column + 1});
    }
    return plan;
}

LineOperationPlan computeDeleteLineEdits(const document::Document& document, std::span<const Cursor> cursors) {
    LineOperationPlan plan;
    if (cursors.empty()) {
        return plan;
    }
    const std::vector<CursorLineInfo> infos      = cursorLineInfos(document, cursors);
    const DistinctLines               distinct   = collectDistinctLines(infos);
    const std::vector<LineRun>        runs       = groupIntoContiguousRuns(distinct.lines);
    const std::uint64_t                totalLines = document.lineCount();

    // One edit per RUN (maximal block of contiguous deleted lines), not per
    // line: the "does this need to also eat the preceding '\n'" decision is
    // a property of the whole block reaching the document's true end, not
    // of any individual line within it. Deciding it per-line (as an earlier
    // version of this function did) made the block's FIRST line responsible
    // for its own trailing '\n' while its LAST line silently assumed that
    // newline had already been consumed - correct for avoiding an overlap,
    // but wrong for the actual boundary: it left the preceding surviving
    // line's own trailing '\n' behind, so e.g. deleting the last two lines
    // of "abc\ndef\nghi" produced "abc\n" instead of "abc".
    std::vector<std::size_t> lineToEditIndex(distinct.lines.size());
    plan.edits.reserve(runs.size());
    std::size_t distinctIndex = 0;
    for (const LineRun& run : runs) {
        const document::LineNumber runLastLine    = run.start + run.count - 1;
        const bool                 runReachesEnd  = (runLastLine + 1 >= totalLines);
        document::TextRange        range;
        if (!runReachesEnd) {
            // Owns the whole block's own trailing '\n' (the one right
            // after runLastLine) - the standard case.
            range = {.start = document.lineToOffset(run.start), .end = document.lineToOffset(runLastLine + 1)};
        } else if (run.start == 0) {
            // The run covers every line in the document.
            range = {.start = 0, .end = document.length()};
        } else {
            // The run reaches the document's true end but doesn't start
            // at line 0: also eat the '\n' immediately before the run, so
            // the preceding surviving line becomes the new last line
            // without a dangling trailing newline.
            range = {.start = document.lineToOffset(run.start) - 1, .end = document.length()};
        }
        const std::size_t editIndex = plan.edits.size();
        plan.edits.push_back(PerCursorEdit{.range = range, .insertedText = u""});
        for (std::uint64_t j = 0; j < run.count; ++j, ++distinctIndex) {
            lineToEditIndex[distinctIndex] = editIndex;
        }
    }

    plan.cursorMappings.reserve(cursors.size());
    for (std::size_t i = 0; i < cursors.size(); ++i) {
        // Every original cursor's own line is, by construction, one of
        // the deleted lines (delete-line deletes exactly the lines that
        // have a cursor) - offset 0 into an empty insertedText places it
        // right where the surviving content now begins.
        plan.cursorMappings.push_back(CursorEditMapping{.editIndex = lineToEditIndex[distinct.cursorToLineIndex[i]]});
    }
    return plan;
}

LineOperationPlan computeMoveLineEdits(const document::Document& document, std::span<const Cursor> cursors,
                                       bool moveDown) {
    LineOperationPlan plan;
    if (cursors.empty()) {
        return plan;
    }
    const std::vector<CursorLineInfo> infos      = cursorLineInfos(document, cursors);
    const DistinctLines               distinct   = collectDistinctLines(infos);
    const std::vector<LineRun>        runs       = groupIntoContiguousRuns(distinct.lines);
    const std::uint64_t                totalLines = document.lineCount();

    std::vector<std::size_t>   lineToEditIndex(distinct.lines.size());
    std::vector<std::uint32_t> lineToBaseOffset(distinct.lines.size());
    bool                        anyRunMoved = false;
    std::size_t                 distinctIndex = 0;

    for (const LineRun& run : runs) {
        const bool blocked = moveDown ? (run.start + run.count >= totalLines) : (run.start == 0);
        if (blocked) {
            appendBlockedRunEdits(document, run, plan, lineToEditIndex, lineToBaseOffset, distinctIndex);
            continue;
        }
        anyRunMoved = true;
        const std::size_t editIndex = plan.edits.size();
        const RunMoveEdit moveEdit  = moveDown ? buildMoveDownEdit(document, run, totalLines)
                                                : buildMoveUpEdit(document, run, totalLines);

        plan.edits.push_back(PerCursorEdit{.range = moveEdit.range, .insertedText = moveEdit.replacement});
        for (std::size_t j = 0; j < run.count; ++j, ++distinctIndex) {
            lineToEditIndex[distinctIndex]  = editIndex;
            lineToBaseOffset[distinctIndex] = moveEdit.lineOffsetsInReplacement[j];
        }
    }

    if (!anyRunMoved) {
        return {};
    }

    plan.cursorMappings.reserve(cursors.size());
    for (std::size_t i = 0; i < cursors.size(); ++i) {
        const std::size_t idx = distinct.cursorToLineIndex[i];
        plan.cursorMappings.push_back(
            CursorEditMapping{.editIndex             = lineToEditIndex[idx],
                              .offsetIntoInsertedText = lineToBaseOffset[idx] + infos[i].column});
    }
    return plan;
}

}  // namespace neomifes::core
