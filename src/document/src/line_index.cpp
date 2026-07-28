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
            // Directly walk the piece's backing buffer - no allocation and no
            // O(N) re-walk of the piece list (unlike the previous MVP path
            // which went through BufferSnapshot::extract).
            const std::u16string_view v = snapshot.pieceView(p);
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (v[i] == u'\n') {
                    m_lineStarts.push_back(cursor + i + 1);
                }
            }
        }
        cursor += p.length;
    }
    m_documentLength = cursor;
}

void LineIndex::applyInsert(TextPos pos, std::u16string_view text) {
    // First line-start strictly greater than pos: everything from here
    // shifts right by text.size(), and this is also the splice point for
    // any new line-starts created by newlines inside `text` (a line-start
    // exactly AT pos - i.e. inserting right at an existing line's start -
    // is left alone; the insertion becomes part of that line, matching
    // build()'s "line 0 always starts at offset 0" convention for pos==0).
    const auto splitIt    = std::ranges::upper_bound(m_lineStarts, pos);
    const auto splitIndex = static_cast<std::size_t>(std::distance(m_lineStarts.begin(), splitIt));

    std::vector<TextPos> newStarts;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\n') {
            newStarts.push_back(pos + i + 1);
        }
    }

    for (std::size_t i = splitIndex; i < m_lineStarts.size(); ++i) {
        m_lineStarts[i] += text.size();
    }
    if (!newStarts.empty()) {
        m_lineStarts.insert(m_lineStarts.begin() + static_cast<std::ptrdiff_t>(splitIndex), newStarts.begin(),
                             newStarts.end());
    }
    m_documentLength += text.size();
}

void LineIndex::applyErase(TextRange range) {
    const TextPos length = range.length();
    // Line-starts in (range.start, range.end] are wholly removed (their
    // lines merge into the one containing range.start); everything from
    // range.end onward (now immediately following the erased span) shifts
    // left by `length`.
    const auto eraseBeginIt = std::ranges::upper_bound(m_lineStarts, range.start);
    const auto eraseEndIt   = std::ranges::upper_bound(m_lineStarts, range.end);
    const auto eraseBeginIndex = static_cast<std::size_t>(std::distance(m_lineStarts.begin(), eraseBeginIt));
    const auto eraseEndIndex   = static_cast<std::size_t>(std::distance(m_lineStarts.begin(), eraseEndIt));

    m_lineStarts.erase(m_lineStarts.begin() + static_cast<std::ptrdiff_t>(eraseBeginIndex),
                        m_lineStarts.begin() + static_cast<std::ptrdiff_t>(eraseEndIndex));
    for (std::size_t i = eraseBeginIndex; i < m_lineStarts.size(); ++i) {
        m_lineStarts[i] -= length;
    }
    m_documentLength -= length;
}

LineNumber LineIndex::offsetToLine(TextPos pos) const noexcept {
    if (m_lineStarts.empty()) {
        return 0;
    }
    // Find the greatest line-start offset <= pos.
    auto it = std::ranges::upper_bound(m_lineStarts, pos);
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
