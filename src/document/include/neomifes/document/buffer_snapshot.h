#pragma once

// BufferSnapshot - an immutable view of the document at a point in time.
// Snapshots are cheap to create (they only copy the piece list) and are safe to
// hand out to arbitrary threads (search, syntax, plugin workers). The two
// backing buffers are shared via shared_ptr so they outlive every snapshot that
// references them.

#include <cstdint>
#include <functional>
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

    // Same as extract(), but for a caller that consumes the result once and
    // discards it (write-to-disk, diff-against-HEAD, syntax reparse feeding
    // a fresh full-text buffer to tree-sitter) rather than one that repeats
    // or randomly re-requests overlapping ranges. Does NOT populate
    // OriginalBuffer's decode cache. Prefer extract() when the SAME range
    // (or overlapping ranges, e.g. a viewport scrolled back and forth) is
    // realistically requested again soon; prefer this one when `range`
    // spans most/all of a large document and this call is the only time
    // that text is needed - see original_buffer.h's OriginalBuffer::
    // viewNoCache() and docs/issues/decode_cache_unbounded_growth.md.
    [[nodiscard]] std::u16string extractNoCache(TextRange range) const;

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

    // Same purpose as pieceView(), but for a caller that will visit this
    // piece's text exactly once and move on (a full-document walk such as
    // LineIndex::build(), LogModel::build(), SearchService's scan, or
    // CSV/JSON/XML's whole-document extract()). Returns by value (not
    // _view): for an Original-sourced piece this does NOT populate
    // OriginalBuffer's decode cache, so nothing owns the decoded buffer
    // once this call returns - unlike pieceView()'s view, the result is not
    // stable beyond the current expression. Prefer pieceView() for
    // random/repeated access (e.g. scrolling), where the cache's reuse is
    // exactly what keeps repeat paints cheap; use this one for a
    // sequential, visit-once walk, where that same cache would otherwise
    // retain the entire document's decoded text indefinitely for no
    // benefit. See original_buffer.h's OriginalBuffer::viewNoCache() and
    // docs/issues/decode_cache_unbounded_growth.md.
    [[nodiscard]] std::u16string pieceTextNoCache(const Piece& p) const;

    // Same purpose as pieceTextNoCache(), but for a caller that would
    // otherwise need to hold the piece's ENTIRE text in memory at once just
    // to scan through it once (LineIndex::build()/LogModel::build()/
    // CsvModel::build() only ever look at one character at a time). Invokes
    // `onChunk` with each successive bounded chunk of the piece's text, in
    // order; never holds more than one chunk in memory regardless of the
    // piece's size. An Add-sourced piece is handed to `onChunk` as a single
    // chunk (AddBuffer pieces are already bounded to at most one append's
    // worth - roughly AddBuffer::kDefaultChunkCUs - by construction, so
    // there is nothing to stream there); the streaming only matters for an
    // Original-sourced piece, which can span an entire multi-GB file.
    // Returns false on a page error partway through (mirrors
    // OriginalBuffer::viewStreamed()'s own contract); an Add-sourced piece
    // (or a null backing buffer) always returns true. See
    // docs/issues/decode_cache_unbounded_growth.md.
    [[nodiscard]] bool pieceTextStreamed(const Piece& p,
                                         const std::function<void(std::u16string_view)>& onChunk) const;

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
