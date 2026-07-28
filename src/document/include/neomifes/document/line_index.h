#pragma once

// LineIndex - converts between UTF-16 offsets and 0-based line numbers.
//
// Computes a flat std::vector<TextPos> of line-start offsets each time the
// caller asks for one via `build(snapshot)`. Cost is O(N) in document length;
// queries after a build are O(log n) via binary search.
//
// This design is intentionally NOT replaced by a tree-aggregate-based O(log n)
// rebuild in Phase 2b2: PieceTree's subtreeNewlines aggregate only tracks a
// per-piece newline COUNT, not the individual newline OFFSETS within a piece's
// text, and the tree has no access to the backing buffers to compute those on
// demand. Making offsetToLine/lineToOffset O(log n) end-to-end would require
// tracking per-piece newline-offset arrays (recomputed whenever a piece is
// split), which is deferred - see docs/issues/line_index_o_log_n.md.
//
// applyInsert()/applyErase() (Phase 7p) implement that issue doc's "案C":
// keep an already-built index consistent across a single edit without a
// full rescan, by shifting affected line-starts and splicing/removing only
// the ones the edit actually touches. Document uses these on every mutating
// call instead of marking the index dirty and re-running build() lazily -
// see Document::insertText()'s header comment for why the lazy/full-rebuild
// approach became an O(document length) cost on EVERY edit once Phase 7k
// started reading offsetToLine()/lineToOffset() from inside insertText()
// itself (not just once per frame from RenderPipeline as originally
// assumed).

#include <cstdint>
#include <string_view>
#include <vector>

#include "neomifes/document/text_pos.h"

namespace neomifes::document {

class BufferSnapshot;

class LineIndex {
public:
    // Builds the index from `snapshot`. Discards any previous state. O(N)
    // in document length - see applyInsert()/applyErase() for the
    // per-edit alternative that avoids this cost.
    void build(const BufferSnapshot& snapshot);

    // Updates the index for an insertion of `text` at `pos`, given the
    // index is already consistent with the pre-insert buffer. O(lines at
    // or after `pos` + newlines in `text`) - NOT O(document length). A
    // pure insertion at the document's current end (the common "typing"
    // and "programmatic append" case) touches zero existing line-starts,
    // making this effectively O(newlines in text).
    void applyInsert(TextPos pos, std::u16string_view text);

    // Updates the index for erasing `range`, given the index is already
    // consistent with the pre-erase buffer. Line-starts strictly within
    // (range.start, range.end] are removed (their lines merged into the
    // one containing range.start); everything from range.end onward
    // shifts left by range.length(). Same complexity note as
    // applyInsert().
    void applyErase(TextRange range);

    // 0-based line number that contains `pos`. If `pos` == length(), returns
    // the number of the last line.
    [[nodiscard]] LineNumber offsetToLine(TextPos pos) const noexcept;

    // Returns the UTF-16 offset at which `line` starts. Out-of-range yields
    // the offset of the last line.
    [[nodiscard]] TextPos    lineToOffset(LineNumber line) const noexcept;

    [[nodiscard]] std::uint64_t lineCount() const noexcept {
        // A document always has at least one line (even if empty).
        return m_lineStarts.empty() ? 1 : m_lineStarts.size();
    }

    // Reset to the empty (single-line) state.
    void clear() noexcept { m_lineStarts.clear(); m_documentLength = 0; }

private:
    // Offsets of the first UTF-16 CU of each line (line 0 always starts at 0).
    std::vector<TextPos> m_lineStarts;
    TextPos              m_documentLength = 0;
};

}  // namespace neomifes::document
