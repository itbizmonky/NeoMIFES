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
        m_tree.insertAt(0, p);
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

void PieceTable::ensureBoundary(TextPos pos) {
    const auto lookup = m_tree.pieceContainingStrictly(pos);
    if (!lookup) {
        return;  // already a boundary (or empty tree / out-of-range)
    }
    const Piece& piece      = lookup->piece;
    const TextPos withinLen = pos - lookup->pieceStart;

    std::u16string_view leftView =
        (piece.source == PieceSource::Add)
            ? m_add     ->view(piece.offset, withinLen)
            : m_original->view(piece.offset, withinLen);
    const std::uint32_t leftNewlines = countNewlines(leftView);

    m_tree.splitPieceAt(pos, leftNewlines);
}

void PieceTable::insert(TextPos pos, std::u16string_view text) {
    if (text.empty()) {
        return;
    }
    const auto total = m_tree.totalLength();
    if (pos > total) {
        pos = total;
    }

    ensureBoundary(pos);

    const std::uint64_t addOffset   = m_add->append(text);
    const std::uint32_t addNewlines = countNewlines(text);

    Piece newPiece{};
    newPiece.source       = PieceSource::Add;
    newPiece.offset       = addOffset;
    newPiece.length       = text.size();
    newPiece.newlineCount = addNewlines;

    m_tree.insertAt(pos, newPiece);
}

void PieceTable::erase(TextRange range) {
    if (range.empty()) {
        return;
    }
    const auto total = m_tree.totalLength();
    if (range.start >= total) {
        return;
    }
    const TextPos end = std::min<TextPos>(range.end, total);

    // Split at both ends so the range aligns with piece boundaries, then let
    // the tree remove the now boundary-aligned pieces in one pass.
    ensureBoundary(range.start);
    ensureBoundary(end);

    m_tree.eraseRange({range.start, end});
}

void PieceTable::replace(TextRange range, std::u16string_view text) {
    // The two-step form leaves totals consistent even when text or range are
    // empty; no need for a fused fast path at MVP scale.
    erase(range);
    insert(range.start, text);
}

std::shared_ptr<const BufferSnapshot> PieceTable::snapshot() const {
    return std::make_shared<const BufferSnapshot>(m_original, m_add, m_tree.collectPieces());
}

}  // namespace neomifes::document
