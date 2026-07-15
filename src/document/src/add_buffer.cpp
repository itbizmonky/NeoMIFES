#include "neomifes/document/add_buffer.h"

#include <algorithm>

namespace neomifes::document {

void AddBuffer::ensureCapacity(std::size_t needed) {
    if (needed == 0) {
        return;
    }
    if (!m_chunks.empty()) {
        const Chunk& back = m_chunks.back();
        const std::size_t remaining = back.data.capacity() - back.data.size();
        if (remaining >= needed) {
            return;
        }
        // Current chunk cannot hold the incoming text; seal it (leave as-is,
        // its startOffset / used size are already correct) and open a new one.
    }

    Chunk fresh;
    fresh.startOffset = m_totalSize;
    // Size to the larger of the default and the requested need, so a single
    // append never has to straddle chunks.
    fresh.data.reserve(std::max<std::size_t>(kDefaultChunkCUs, needed));
    m_chunks.push_back(std::move(fresh));
}

std::uint64_t AddBuffer::append(std::u16string_view text) {
    if (text.empty()) {
        return m_totalSize;
    }
    ensureCapacity(text.size());
    Chunk& back = m_chunks.back();

    const std::uint64_t globalOffset = back.startOffset + back.data.size();
    // insert() will NOT reallocate because ensureCapacity guaranteed room.
    back.data.insert(back.data.end(), text.begin(), text.end());
    m_totalSize += text.size();
    return globalOffset;
}

std::u16string_view AddBuffer::view(std::uint64_t offset,
                                    std::uint64_t length) const noexcept {
    if (length == 0 || m_chunks.empty()) {
        return {};
    }
    if (offset >= m_totalSize || length > m_totalSize - offset) {
        return {};
    }

    // Binary search for the chunk whose [startOffset, startOffset + used)
    // contains `offset`. Chunks are strictly ordered by startOffset and never
    // move, so upper_bound / -1 is stable across any concurrent append.
    // (heterogeneous comparator doesn't satisfy std::ranges::upper_bound's
    // indirect_strict_weak_order full-symmetry requirement - see the
    // identical case in original_buffer.cpp's viewMemoryMapped.)
    // NOLINTNEXTLINE(modernize-use-ranges)
    auto it = std::upper_bound(
        m_chunks.begin(), m_chunks.end(), offset,
        [](std::uint64_t o, const Chunk& c) noexcept {
            return o < c.startOffset;
        });
    if (it == m_chunks.begin()) {
        return {};
    }
    --it;

    const std::uint64_t local = offset - it->startOffset;
    // A range that crosses into the next chunk indicates a caller broke the
    // "one append == one contiguous slice" invariant. MVP: return empty
    // instead of undefined behaviour; PieceTable-derived callers never hit
    // this path because a Piece is created inside a single append.
    if (local + length > it->data.size()) {
        return {};
    }
    return {it->data.data() + local, static_cast<std::size_t>(length)};
}

void AddBuffer::reserve(std::size_t codeUnits) {
    if (codeUnits == 0) {
        return;
    }
    if (m_chunks.empty()) {
        Chunk fresh;
        fresh.startOffset = 0;
        fresh.data.reserve(std::max<std::size_t>(kDefaultChunkCUs, codeUnits));
        m_chunks.push_back(std::move(fresh));
        return;
    }
    // Only meaningful when the current chunk is still empty; otherwise
    // growing capacity would invalidate the very pointer stability guarantee
    // we exist to preserve. Silently ignore.
}

}  // namespace neomifes::document
