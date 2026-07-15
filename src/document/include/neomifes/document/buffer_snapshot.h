#pragma once

// BufferSnapshot - an immutable view of the document at a point in time.
// Snapshots are cheap to create (they only copy the piece list) and are safe to
// hand out to arbitrary threads (search, syntax, plugin workers). The two
// backing buffers are shared via shared_ptr so they outlive every snapshot that
// references them.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/document/piece.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {

class AddBuffer;
class OriginalBuffer;

class BufferSnapshot {
public:
    BufferSnapshot(std::shared_ptr<const OriginalBuffer> original,
                   std::shared_ptr<const AddBuffer>      add,
                   std::vector<Piece>                    pieces) noexcept;

    // Total length of the document in UTF-16 code units.
    [[nodiscard]] std::uint64_t length() const noexcept { return m_totalLength; }

    // Total number of '\n' characters. Line count = newlineCount + 1
    // (documents without a trailing newline still have a final line).
    [[nodiscard]] std::uint64_t newlineCount() const noexcept { return m_totalNewlines; }
    [[nodiscard]] std::uint64_t lineCount()    const noexcept { return m_totalNewlines + 1; }

    // Materialises the given range as a UTF-16 string. Empty on out-of-range.
    // Cost: proportional to range length + number of pieces the range spans.
    [[nodiscard]] std::u16string extract(TextRange range) const;

    // Returns a UTF-16 view of the piece's own text (its full [offset,
    // offset+length) slice inside the correct backing buffer). O(1) for
    // AddBuffer-sourced pieces; for OriginalBuffer-sourced pieces (Phase
    // 2b3 mmap + Lazy Decode) this may decode-and-cache on first access, so
    // it is NOT noexcept (a genuine std::bad_alloc is allowed to propagate
    // rather than being swallowed - CLAUDE.md forbids unconditional
    // catch(...)). Behaviour is defined only for pieces that belong to
    // THIS snapshot's piece list; passing an unrelated Piece is UB.
    //
    // This is the intended primitive for O(N) traversal helpers such as
    // LineIndex. Prefer this over extract() when scanning by piece,
    // because extract() re-walks the full piece list from cursor=0.
    [[nodiscard]] std::u16string_view pieceView(const Piece& p) const;

    // Access to the immutable piece list for tests / benchmarks / debug.
    [[nodiscard]] const std::vector<Piece>& pieces() const noexcept { return m_pieces; }

private:
    std::shared_ptr<const OriginalBuffer> m_original;
    std::shared_ptr<const AddBuffer>      m_add;
    std::vector<Piece>                    m_pieces;
    std::uint64_t                         m_totalLength   = 0;
    std::uint64_t                         m_totalNewlines = 0;
};

}  // namespace neomifes::document
