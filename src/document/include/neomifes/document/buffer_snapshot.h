#pragma once

// BufferSnapshot - an immutable view of the document at a point in time.
// Snapshots are cheap to create (they only copy the piece list) and are safe to
// hand out to arbitrary threads (search, syntax, plugin workers). The two
// backing buffers are shared via shared_ptr so they outlive every snapshot that
// references them.

#include <cstdint>
#include <memory>
#include <string>
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
