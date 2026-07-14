#include "neomifes/document/line_index.h"

#include <algorithm>
#include <string_view>

#include "neomifes/document/add_buffer.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

void LineIndex::build(const BufferSnapshot& snapshot) {
    m_lineStarts.clear();
    m_lineStarts.push_back(0);   // line 0 always starts at offset 0

    TextPos cursor = 0;
    for (const auto& p : snapshot.pieces()) {
        // Only pieces that contain at least one '\n' need to be scanned. The
        // cached count on the Piece struct lets us skip large runs cheaply.
        if (p.newlineCount > 0) {
            // MVP: materialise the piece and rescan. Phase 2b will iterate the
            // underlying buffer directly (no allocation, no copy).
            const std::u16string owned = snapshot.extract({cursor, cursor + p.length});
            for (std::size_t i = 0; i < owned.size(); ++i) {
                if (owned[i] == u'\n') {
                    m_lineStarts.push_back(cursor + i + 1);
                }
            }
        }
        cursor += p.length;
    }
    m_documentLength = cursor;
}

LineNumber LineIndex::offsetToLine(TextPos pos) const noexcept {
    if (m_lineStarts.empty()) {
        return 0;
    }
    // Find the greatest line-start offset <= pos.
    auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), pos);
    if (it == m_lineStarts.begin()) {
        return 0;
    }
    return static_cast<LineNumber>(std::distance(m_lineStarts.begin(), it - 1));
}

TextPos LineIndex::lineToOffset(LineNumber line) const noexcept {
    if (m_lineStarts.empty()) {
        return 0;
    }
    if (line >= m_lineStarts.size()) {
        return m_lineStarts.back();
    }
    return m_lineStarts[static_cast<std::size_t>(line)];
}

}  // namespace neomifes::document
