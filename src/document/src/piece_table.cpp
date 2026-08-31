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
        // newlineCount() is precomputed once by OriginalBuffer's streaming
        // byte-level scan at load time (Phase 2b3). Deliberately NOT calling
        // m_original->view(0, size()) here: under the mmap-backed Lazy
        // Decode implementation that would force-decode the entire file to
        // UTF-16 immediately on open, defeating the point of laziness.
        Piece p{};
        p.source       = PieceSource::Original;
        p.offset       = 0;
        p.length       = m_original->size();
        p.newlineCount = m_original->newlineCount();
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

    // m_original->viewNoCache(), not view(): this only needs a newline
    // count for the split, then discards the text - and `withinLen` can be
    // most of a large Original piece the first time an edit lands inside
    // it (e.g. the very first edit to a freshly opened 10GB file), so the
    // caching view() would permanently retain that much decoded UTF-16 in
    // OriginalBuffer just to compute a count - see
    // docs/issues/decode_cache_unbounded_growth.md. m_add->view() is
    // unaffected (AddBuffer holds already-decoded text; no cache exists to
    // avoid populating there), so only the Original-sourced branch changes.
    const std::uint32_t leftNewlines =
        (piece.source == PieceSource::Add)
            ? countNewlines(m_add->view(piece.offset, withinLen))
            : countNewlines(m_original->viewNoCache(piece.offset, withinLen));

    m_tree.splitPieceAt(pos, leftNewlines);
}

void PieceTable::insert(TextPos pos, std::u16string_view text) {
    if (text.empty()) {
        return;
    }
    const auto total = m_tree.totalLength();
    pos = std::min(pos, total);

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

    m_tree.eraseRange(TextRange{.start = range.start, .end = end});
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
