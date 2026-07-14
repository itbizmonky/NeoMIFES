#include "neomifes/document/piece_table.h"

#include <algorithm>

#include "neomifes/document/add_buffer.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

PieceTable::PieceTable()
    : m_add(std::make_shared<AddBuffer>()) {}

PieceTable::PieceTable(std::shared_ptr<const OriginalBuffer> original)
    : m_original(std::move(original)),
      m_add(std::make_shared<AddBuffer>()) {
    if (m_original && m_original->size() > 0) {
        std::u16string_view v = m_original->view(0, m_original->size());
        Piece p{};
        p.source       = PieceSource::Original;
        p.offset       = 0;
        p.length       = m_original->size();
        p.newlineCount = countNewlines(v);
        m_pieces.push_back(p);
        m_totalLength   = p.length;
        m_totalNewlines = p.newlineCount;
    }
}

std::uint32_t PieceTable::countNewlines(std::u16string_view v) noexcept {
    std::uint32_t n = 0;
    for (const char16_t c : v) {
        if (c == u'\n') {
            ++n;
        }
    }
    return n;
}

std::size_t PieceTable::findPiece(TextPos pos, std::uint64_t& posWithin) const noexcept {
    TextPos cursor = 0;
    for (std::size_t i = 0; i < m_pieces.size(); ++i) {
        const auto& p = m_pieces[i];
        // A position at pieceEnd should belong to the *next* piece so that
        // inserts land after the current piece rather than splitting it.
        if (pos < cursor + p.length) {
            posWithin = pos - cursor;
            return i;
        }
        cursor += p.length;
    }
    posWithin = 0;
    return m_pieces.size();
}

std::size_t PieceTable::splitAt(std::size_t pi, std::uint64_t posWithin) {
    if (pi >= m_pieces.size() || posWithin == 0) {
        return pi;
    }
    Piece& current = m_pieces[pi];
    if (posWithin >= current.length) {
        return pi + 1;
    }

    // Left half becomes `current` (shortened), right half is inserted after it.
    std::u16string_view leftView =
        (current.source == PieceSource::Add)
            ? m_add     ->view(current.offset, posWithin)
            : m_original->view(current.offset, posWithin);
    const std::uint32_t leftNewlines = countNewlines(leftView);

    Piece right{};
    right.source       = current.source;
    right.offset       = current.offset + posWithin;
    right.length       = current.length - posWithin;
    right.newlineCount = current.newlineCount - leftNewlines;

    current.length       = posWithin;
    current.newlineCount = leftNewlines;

    m_pieces.insert(m_pieces.begin() + static_cast<std::ptrdiff_t>(pi + 1), right);
    return pi + 1;
}

void PieceTable::insert(TextPos pos, std::u16string_view text) {
    if (text.empty()) {
        return;
    }
    if (pos > m_totalLength) {
        pos = m_totalLength;
    }

    // Append the incoming text to the Add buffer.
    const std::uint64_t addOffset  = m_add->append(text);
    const std::uint32_t addNewlines = countNewlines(text);

    Piece newPiece{};
    newPiece.source       = PieceSource::Add;
    newPiece.offset       = addOffset;
    newPiece.length       = text.size();
    newPiece.newlineCount = addNewlines;

    // Split the containing piece at `pos` and insert the new piece before the
    // right half.
    std::uint64_t posWithin = 0;
    const std::size_t pi   = findPiece(pos, posWithin);
    const std::size_t splitIdx = splitAt(pi, posWithin);

    m_pieces.insert(m_pieces.begin() + static_cast<std::ptrdiff_t>(splitIdx), newPiece);
    m_totalLength   += newPiece.length;
    m_totalNewlines += newPiece.newlineCount;
}

void PieceTable::erase(TextRange range) {
    if (range.empty()) {
        return;
    }
    if (range.start >= m_totalLength) {
        return;
    }
    const TextPos end = std::min<TextPos>(range.end, m_totalLength);

    // Split at both ends so the range aligns with piece boundaries.
    std::uint64_t startWithin = 0;
    const std::size_t startPi = findPiece(range.start, startWithin);
    const std::size_t startSplit = splitAt(startPi, startWithin);

    std::uint64_t endWithin = 0;
    const std::size_t endPi = findPiece(end, endWithin);
    const std::size_t endSplit = splitAt(endPi, endWithin);

    // Remove pieces in [startSplit, endSplit).
    if (endSplit > startSplit) {
        for (std::size_t i = startSplit; i < endSplit; ++i) {
            m_totalLength   -= m_pieces[i].length;
            m_totalNewlines -= m_pieces[i].newlineCount;
        }
        m_pieces.erase(m_pieces.begin() + static_cast<std::ptrdiff_t>(startSplit),
                       m_pieces.begin() + static_cast<std::ptrdiff_t>(endSplit));
    }
}

void PieceTable::replace(TextRange range, std::u16string_view text) {
    // The two-step form leaves totals consistent even when text or range are
    // empty; no need for a fused fast path at MVP scale.
    erase(range);
    insert(range.start, text);
}

std::shared_ptr<const BufferSnapshot> PieceTable::snapshot() const {
    return std::make_shared<const BufferSnapshot>(m_original, m_add, m_pieces);
}

}  // namespace neomifes::document
