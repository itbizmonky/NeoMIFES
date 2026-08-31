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

std::u16string BufferSnapshot::pieceTextNoCache(const Piece& p) const {
    if (p.source == PieceSource::Add) {
        // AddBuffer holds already-decoded UTF-16 directly - no decode cache
        // exists for it, so there is nothing to avoid populating. Copied
        // into a std::u16string purely to give this function one return
        // type for both piece sources.
        return m_add ? std::u16string(m_add->view(p.offset, p.length)) : std::u16string{};
    }
    return m_original ? m_original->viewNoCache(p.offset, p.length) : std::u16string{};
}

bool BufferSnapshot::pieceTextStreamed(const Piece& p,
                                       const std::function<void(std::u16string_view)>& onChunk) const {
    if (p.source == PieceSource::Add) {
        if (m_add) {
            onChunk(m_add->view(p.offset, p.length));
        }
        return true;
    }
    return m_original ? m_original->viewStreamed(p.offset, p.length, onChunk) : true;
}

namespace {
// Shared piece-walking skeleton for extract()/extractNoCache(): both need
// the exact same overlap arithmetic against `pieces`, differing only in how
// each overlapping chunk is turned into text (cached OriginalBuffer::view()
// vs non-caching viewNoCache()) - factored out so that arithmetic can't
// drift out of sync between the two copies.
// Takes `fetchChunk` by const& rather than as a forwarding reference: the
// loop below calls it once per piece, so a forwarding reference here would
// invite a well-intentioned std::forward<FetchChunk>(fetchChunk) at the
// call site that moves-from an rvalue-bound callable on the FIRST call,
// leaving subsequent calls in this same loop invoking a moved-from object -
// there is no legitimate reason to forward a callback that outlives and is
// invoked multiple times by the function it's passed to.
template <typename FetchChunk>
std::u16string extractWalk(TextRange range, TextPos totalLength, const std::vector<Piece>& pieces,
                           const FetchChunk& fetchChunk) {
    if (range.start >= range.end || range.start >= totalLength) {
        return {};
    }
    const TextPos requestedEnd = std::min<TextPos>(range.end, totalLength);

    std::u16string out;
    out.reserve(static_cast<std::size_t>(requestedEnd - range.start));

    TextPos cursor = 0;
    for (const auto& p : pieces) {
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

        fetchChunk(p, withinPiece, take, out);
    }
    return out;
}
}  // namespace

std::u16string BufferSnapshot::extract(TextRange range) const {
    return extractWalk(range, m_totalLength, m_pieces,
                       [this](const Piece& p, std::uint64_t withinPiece, std::uint64_t take,
                              std::u16string& out) {
        const std::u16string_view chunk =
            (p.source == PieceSource::Add)
                ? m_add     ->view(p.offset + withinPiece, take)
                : m_original->view(p.offset + withinPiece, take);
        out.append(chunk);
    });
}

std::u16string BufferSnapshot::extractNoCache(TextRange range) const {
    return extractWalk(range, m_totalLength, m_pieces,
                       [this](const Piece& p, std::uint64_t withinPiece, std::uint64_t take,
                              std::u16string& out) {
        if (p.source == PieceSource::Add) {
            out.append(m_add->view(p.offset + withinPiece, take));
        } else {
            out.append(m_original->viewNoCache(p.offset + withinPiece, take));
        }
    });
}

}  // namespace neomifes::document
