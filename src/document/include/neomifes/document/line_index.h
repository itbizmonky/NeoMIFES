#pragma once

// LineIndex - converts between UTF-16 offsets and 0-based line numbers.
//
// Phase 2a MVP: computes a flat std::vector<TextPos> of line-start offsets
// each time the caller asks for one via `build(snapshot)`. Cost is O(N) in
// document length. This is acceptable for medium-sized documents but has to
// be replaced by an incremental structure integrated with the piece tree
// (Phase 2b) to hit the 10GB target - see docs/issues/piece_table_rb_tree.md.

#include <cstdint>
#include <vector>

#include "neomifes/document/text_pos.h"

namespace neomifes::document {

class BufferSnapshot;

class LineIndex {
public:
    // Builds the index from `snapshot`. Discards any previous state.
    void build(const BufferSnapshot& snapshot);

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
