#pragma once

// A Piece is a half-open [offset, offset+length) slice of one of two backing
// buffers (Original = read-only, Add = append-only, both UTF-16). PieceTable
// stores an ordered sequence of pieces that together form the logical document.
//
// Design constraints (see docs/design/detailed_design.md sec.3.1):
//   - offset / length are in UTF-16 code units, NOT bytes.
//   - newlineCount is cached so LineIndex can answer line queries in O(log n)
//     when the piece container becomes a tree (Phase 2b).
//   - The struct is trivially copyable so pieces can be duplicated for snapshots.

#include <cstdint>

namespace neomifes::document {

enum class PieceSource : std::uint8_t {
    Original = 0,
    Add      = 1,
};

struct Piece {
    PieceSource   source       = PieceSource::Add;
    std::uint64_t offset       = 0;  // UTF-16 CU offset within the source buffer
    std::uint64_t length       = 0;  // UTF-16 CU count
    std::uint32_t newlineCount = 0;  // cached count of '\n' inside this slice

    [[nodiscard]] constexpr bool empty() const noexcept { return length == 0; }
};

static_assert(sizeof(Piece) <= 32, "Piece should stay compact (fits in a cache line pair).");

}  // namespace neomifes::document
