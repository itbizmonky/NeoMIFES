#include "neomifes/document/buffer_snapshot.h"

#include <algorithm>
#include <string_view>

#include "neomifes/document/add_buffer.h"
#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

BufferSnapshot::BufferSnapshot(std::shared_ptr<const OriginalBuffer> original,
                               std::shared_ptr<const AddBuffer>      add,
                               std::vector<Piece>                    pieces) noexcept
    : m_original(std::move(original)), m_add(std::move(add)),
      m_pieces(std::move(pieces)) {
    for (const auto& p : m_pieces) {
        m_totalLength   += p.length;
        m_totalNewlines += p.newlineCount;
    }
}

std::u16string_view BufferSnapshot::pieceView(const Piece& p) const {
    if (p.source == PieceSource::Add) {
        return m_add ? m_add->view(p.offset, p.length) : std::u16string_view{};
    }
    return m_original ? m_original->view(p.offset, p.length) : std::u16string_view{};
}

std::u16string BufferSnapshot::extract(TextRange range) const {
    if (range.start >= range.end || range.start >= m_totalLength) {
        return {};
    }
    const TextPos requestedEnd = std::min<TextPos>(range.end, m_totalLength);

    std::u16string out;
    out.reserve(static_cast<std::size_t>(requestedEnd - range.start));

    TextPos cursor = 0;
    for (const auto& p : m_pieces) {
        const TextPos pieceStart = cursor;
        const TextPos pieceEnd   = cursor + p.length;
        cursor = pieceEnd;

        if (pieceEnd <= range.start) {
            continue;
        }
        if (pieceStart >= requestedEnd) {
            break;
        }
        // Overlap of [pieceStart, pieceEnd) and [range.start, requestedEnd).
        const std::uint64_t overlapStart = std::max<TextPos>(pieceStart, range.start);
        const std::uint64_t overlapEnd   = std::min<TextPos>(pieceEnd,   requestedEnd);
        const std::uint64_t withinPiece  = overlapStart - pieceStart;
        const std::uint64_t take         = overlapEnd - overlapStart;

        const std::u16string_view chunk =
            (p.source == PieceSource::Add)
                ? m_add     ->view(p.offset + withinPiece, take)
                : m_original->view(p.offset + withinPiece, take);
        out.append(chunk);
    }
    return out;
}

}  // namespace neomifes::document
