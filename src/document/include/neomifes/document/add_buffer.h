#pragma once

// AddBuffer - append-only UTF-16 storage for all inserted text.
//
// Storage model (Phase 2b): a std::deque of fixed-capacity chunks. Every
// chunk pre-reserves its char16_t buffer to kDefaultChunkCUs and never
// reallocates, so any offset returned by append() stays pointer-valid for
// the lifetime of the AddBuffer, even under concurrent snapshot readers.
//
// This matches design.detailed_design.md sec.3.1: pieces are cheap to
// snapshot precisely because the buffer they index into is stable.
//
// Invariant respected by PieceTable and BufferSnapshot::pieceView:
//   a single append() call produces exactly one Piece, therefore any
//   [offset, offset+length) range that comes from an existing Piece is
//   guaranteed to lie within one chunk and view() can return a contiguous
//   u16string_view. Ranges that span chunks return an empty view rather
//   than assert.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>
#include <vector>

namespace neomifes::document {

class AddBuffer {
public:
    // Default UTF-16 CU count per chunk (128 KiB). Chosen to comfortably
    // absorb keystroke bursts while keeping snapshot pointer stability cheap.
    static constexpr std::size_t kDefaultChunkCUs = 64 * 1024;

    AddBuffer() = default;

    // Appends `text` and returns the global UTF-16 CU offset where it landed.
    // Never invalidates offsets returned from previous append() calls (chunks
    // pre-reserve capacity, and std::deque never invalidates existing element
    // addresses on push_back). An append that would overflow the current chunk
    // seals it and starts a new chunk sized max(kDefaultChunkCUs, text.size()),
    // so a single append never straddles a chunk boundary.
    std::uint64_t append(std::u16string_view text);

    // Returns a view over [offset, offset+length). Returns an empty view when:
    //   - length == 0
    //   - the range is out of bounds
    //   - the range spans a chunk boundary (should not happen for ranges
    //     derived from a real Piece; see invariant above)
    [[nodiscard]] std::u16string_view view(std::uint64_t offset,
                                           std::uint64_t length) const noexcept;

    [[nodiscard]] std::uint64_t size() const noexcept { return m_totalSize; }

    // Hint: enlarge the first chunk's capacity when the buffer is still
    // empty. After any append() this is a no-op.
    void reserve(std::size_t codeUnits);

    // Diagnostics for tests and Phase 2b bench-tuning.
    [[nodiscard]] std::size_t chunkCount() const noexcept { return m_chunks.size(); }

private:
    struct Chunk {
        std::vector<char16_t> data;         // capacity is pinned at construction
        std::uint64_t         startOffset;  // global offset where this chunk starts
    };

    // Ensures m_chunks.back() has at least `needed` remaining CU capacity.
    // Seals the current chunk (if any) and pushes a new one when necessary.
    void ensureCapacity(std::size_t needed);

    std::deque<Chunk> m_chunks;
    std::uint64_t     m_totalSize = 0;
};

}  // namespace neomifes::document
