#pragma once

// CsvModel - headless RFC4180-ish CSV parsing (WI-16a, Phase 10.2 core).
// build() parses one document::Document's full text into a flat table of
// cell positions, synchronously, on the calling thread. No background
// worker, no EditorSession integration, no UI, no filter/sort - see
// build_plan.md's WI-16a entry for the full sub-WI breakdown (mirrors
// logmode's WI-14a/jsontree's WI-15a: headless model first, async+
// EditorSession wiring and UI are later sub-WIs).

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/document/text_pos.h"  // document::TextPos

namespace neomifes::document {
class Document;
class BufferSnapshot;
}  // namespace neomifes::document

namespace neomifes::csvmode {

// One cell's raw source range (document::TextPos, UTF-16 code-unit offsets).
// For a field that closed cleanly as "..." (`quoted == true`), the range
// spans from the opening quote through the closing quote inclusive - the
// same "token's own range" convention jsontree::JsonNode uses for its
// leaves. Deliberately does NOT store decoded (or even raw) text: a
// multi-million-row CsvModel must not hold a std::u16string copy per cell -
// see logmode::LogLine's identical "don't duplicate what's cheap to
// recompute" reasoning (log_model.h). Callers needing a cell's value call
// csvCellValue() below.
//
// `quoted` records a fact the parser alone can observe (whether this field
// finalized while still inside a syntactically valid "..." token, as
// opposed to having fallen back to unquoted parsing after malformed
// content such as `"abc"def`) - csvCellValue() relies on this flag rather
// than re-inferring "was this quoted" from the raw text's first/last
// characters, which is not reliable: a dirty fallback field's raw span can
// itself happen to start and end with '"' (e.g. `"abc"def"ghi"`) without
// ever having been a clean quoted token.
struct CsvCell {
    document::TextPos startPos = 0;  // inclusive
    // csv_per_cell_index_memory_scaling.md: stored as a length rather than
    // an absolute endPos - m_cells is one flat vector for the WHOLE
    // document (see CsvModel's own class comment), so shrinking each
    // CsvCell by 8 bytes (24->16, MSVC's 8-byte alignment on startPos
    // rounds this down no further) is a real saving at scale. A single
    // cell's raw text length safely fits uint32_t (an individual CSV field
    // exceeding 4 billion characters is not a realistic input this parser
    // needs to represent exactly).
    std::uint32_t length = 0;
    bool           quoted = false;

    [[nodiscard]] document::TextPos endPos() const noexcept { return startPos + length; }

    friend bool operator==(const CsvCell&, const CsvCell&) = default;
};

// document::Piece (piece.h) has the same guard for the same reason: m_cells
// is one flat vector sized to the whole document's cell count, so a future
// field addition silently regrowing this struct back toward 24 bytes would
// be an easy-to-miss regression at 10GB-file scale.
static_assert(sizeof(CsvCell) <= 16, "CsvCell should stay compact (csv_per_cell_index_memory_scaling.md).");

struct CsvParseOptions {
    char16_t delimiter = u',';
    // Whether row 0 should be treated as a header (headerRow()/dataRow()
    // split accordingly). Deliberately NOT auto-detected - neither the
    // requirements doc §9 nor master_roadmap.md §10.2 calls for header
    // auto-detection (only delimiter auto-detection is a stated
    // requirement), and a "does this look like a header" heuristic has
    // well-known blind spots (all-string-typed CSVs, single-row files).
    bool hasHeader = true;
};

enum class CsvParseError : std::uint8_t { InvalidDelimiter };

// CsvModel is deliberately NOT the roadmap sketch's
// `class CsvModel { document::Document* m_doc; ... }` mutate-in-place
// shape - same reasoning logmode::LogModel::build() already established
// over the roadmap's own LogModel::attach(Document&, rule) sketch: a
// Document*-holding model raises a document-swap lifetime question this
// headless stage doesn't need to answer yet. `m_headers`/`m_visibleRows`
// (the roadmap sketch's other two fields) are likewise omitted: the former
// would duplicate per-cell text against the "don't duplicate" reasoning
// above, and the latter (filtered row order) belongs to filter/sort
// functionality this WI does not implement.
class CsvModel {
public:
    // Parses `snapshot`'s/`doc`'s full text as CSV using `options`. Every
    // row (including a trailing implicit empty row when the document ends
    // in '\n', and the document's own single empty row when it is entirely
    // empty - the same convention document::Document::lineCount() already
    // establishes) always has at least one cell; unterminated quotes and
    // ragged row lengths are absorbed leniently rather than rejected (see
    // this WI's design notes, build_plan.md). The only failure this can
    // return is a caller configuration mistake: `options.delimiter` set to
    // '\r'/'\n'/'"' would break the state machine's assumption that the
    // delimiter, CR, LF, and quote are four mutually exclusive special
    // characters - the same "caller passed a broken configuration" shape
    // as logmode::LogPatternError::InvalidRegex, not a statement about the
    // document's own content.
    [[nodiscard]] static std::expected<CsvModel, CsvParseError> build(
        const document::BufferSnapshot& snapshot, const CsvParseOptions& options = CsvParseOptions{});
    [[nodiscard]] static std::expected<CsvModel, CsvParseError> build(
        const document::Document& doc, const CsvParseOptions& options = CsvParseOptions{});

    [[nodiscard]] std::size_t rowCount() const noexcept { return m_rowOffsets.size() - 1; }
    [[nodiscard]] std::span<const CsvCell> row(std::size_t rowIndex) const noexcept;

    [[nodiscard]] bool hasHeader() const noexcept { return m_hasHeader; }
    [[nodiscard]] std::span<const CsvCell> headerRow() const noexcept;
    [[nodiscard]] std::size_t dataRowCount() const noexcept;
    [[nodiscard]] std::span<const CsvCell> dataRow(std::size_t dataRowIndex) const noexcept;

    // Largest cell count any single row has - a convenience for a future
    // grid UI sizing its column headers. Ragged rows (a row with fewer
    // cells than another) are not an error; this is purely informational.
    [[nodiscard]] std::size_t maxColumnCount() const noexcept { return m_maxColumnCount; }

private:
    // CSR (compressed sparse row) layout: one flat CsvCell array plus row
    // boundary offsets, NOT a per-row std::vector<CsvCell> (the roadmap
    // sketch's std::vector<std::vector<uint32_t>> m_columnOffsets shape).
    // The requirements doc's own stated scale (10 million rows) makes a
    // per-row heap allocation itself a real cost this WI avoids from the
    // start - unlike LogModel's WI-14a->WI-14b optimization (an internal-
    // only rewrite from O(lines*pieces) to O(document length) that never
    // changed LogModel's public shape), the container shape here is part
    // of the public API and changing it later would be a breaking change
    // for filter/sort/grid-UI callers (WI-16b/c), so it is decided now.
    std::vector<CsvCell>       m_cells;
    std::vector<std::uint32_t> m_rowOffsets;  // size == rowCount()+1; m_rowOffsets[0] == 0 always
    bool                        m_hasHeader     = false;
    std::size_t                 m_maxColumnCount = 0;
};

// A cell's decoded logical value: for a quoted cell (see CsvCell::quoted),
// strips the surrounding quotes and un-escapes "" to " (RFC4180); for an
// unquoted cell, returns the raw text unchanged. Recomputed on every call
// (mirrors document::Document::lineText()'s "cheap to recompute, so don't
// cache" contract) - callers displaying many cells should call this lazily
// (e.g. only for the currently visible grid range), not eagerly for the
// whole model.
[[nodiscard]] std::u16string csvCellValue(const document::BufferSnapshot& snapshot, const CsvCell& cell);
[[nodiscard]] std::u16string csvCellValue(const document::Document& doc, const CsvCell& cell);

// The encode-side counterpart to csvCellValue() above (WI-16f, cell editing):
// given a logical value the user just typed and the delimiter the
// surrounding CsvModel was parsed with, produces the raw text to write back
// into the document at a CsvCell's [startPos, endPos) range. Quotes the
// result (doubling any embedded '"') iff `value` contains `delimiter`, '"',
// '\r', or '\n' (RFC4180 §2.6) - otherwise returns `value` unchanged. An
// empty `value` round-trips to an empty, unquoted cell (matching
// csvCellValue()'s own unquoted branch for `""`).
[[nodiscard]] std::u16string escapeCsvCellText(std::u16string_view value, char16_t delimiter);

}  // namespace neomifes::csvmode
