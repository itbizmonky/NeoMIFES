#include "neomifes/csvmode/csv_model.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"

namespace neomifes::csvmode {

namespace {

enum class FieldState : std::uint8_t {
    FieldStart,     // at the first character of a field (possibly empty)
    Unquoted,       // accumulating an unquoted field
    Quoted,         // inside "..." - every character, including delimiter/CR/LF, is literal
    QuoteInQuoted,  // just saw a '"' while Quoted - one more character resolves it
};

// Owns the in-progress output vectors plus the small amount of scanning
// state a single character needs (current field's start position, current
// FieldState, and whether an unresolved '\r' is pending - see
// processChar()'s own comment). Deliberately holds `cells`/`rowOffsets` by
// value (not by reference, unlike jsontree::ParseState's reference-bundle
// shape) - CsvBuilder is the sole owner of these vectors for the duration
// of one build() call and moves them into the result CsvModel at the end,
// so there is no aliasing concern a reference would need to solve here.
struct CsvBuilder {
    std::vector<CsvCell>       cells;
    std::vector<std::uint32_t> rowOffsets{0};
    std::size_t                 maxColumnCount = 0;
    document::TextPos           fieldStart     = 0;
    FieldState                  state          = FieldState::FieldStart;
    bool                         crPending      = false;

    // CsvCell::quoted is true iff finalization happens while still in
    // QuoteInQuoted - the state a field can ONLY be in immediately after
    // consuming a closing '"' that was never followed by disqualifying
    // "dirty" content (see handleQuoteInQuoted()). This is why the raw
    // range [fieldStart, endPos) is guaranteed to both start AND end with
    // '"' whenever quoted==true - csvCellValue() below depends on that
    // guarantee holding exactly, not on inferring it from the text itself.
    void finalizeField(document::TextPos endPos) {
        cells.push_back(CsvCell{.startPos = fieldStart, .endPos = endPos, .quoted = (state == FieldState::QuoteInQuoted)});
    }

    void finalizeRow() {
        maxColumnCount = std::max(maxColumnCount, cells.size() - rowOffsets.back());
        rowOffsets.push_back(static_cast<std::uint32_t>(cells.size()));
    }

    // Every delimiter/row-terminator transition ends up here: the next
    // field starts right after the character just consumed, in FieldStart
    // state (even for a field that itself started with '"' - the '"'
    // handling in handleFieldStart() overwrites `state` again immediately
    // afterward for that specific case).
    void startField(document::TextPos pos) {
        fieldStart = pos;
        state      = FieldState::FieldStart;
    }
};

void handleFieldStart(CsvBuilder& b, char16_t ch, document::TextPos pos, char16_t delimiter) {
    if (ch == delimiter) {
        b.finalizeField(pos);
        b.startField(pos + 1);
    } else if (ch == u'"') {
        b.fieldStart = pos;
        b.state      = FieldState::Quoted;
    } else if (ch == u'\r') {
        b.crPending = true;
        b.state     = FieldState::Unquoted;
    } else if (ch == u'\n') {
        b.finalizeField(pos);
        b.finalizeRow();
        b.startField(pos + 1);
    } else {
        b.state = FieldState::Unquoted;
    }
}

void handleUnquoted(CsvBuilder& b, char16_t ch, document::TextPos pos, char16_t delimiter) {
    if (ch == delimiter) {
        b.finalizeField(pos);
        b.startField(pos + 1);
    } else if (ch == u'\r') {
        b.crPending = true;
    } else if (ch == u'\n') {
        b.finalizeField(pos);
        b.finalizeRow();
        b.startField(pos + 1);
    }
    // Any other character, including '"' (RFC4180 says a bare '"' inside an
    // unquoted field is not strictly legal, but real-world CSV producers
    // emit it often enough that rejecting it would be more surprising than
    // accepting it - see this WI's lenient-absorption design notes),
    // simply stays Unquoted.
}

void handleQuoted(CsvBuilder& b, char16_t ch) {
    if (ch == u'"') {
        b.state = FieldState::QuoteInQuoted;
    }
    // Everything else - including the delimiter, '\r', and '\n' - is
    // literal content inside a quoted field per RFC4180; this is what lets
    // one logical CSV row span multiple document lines.
}

void handleQuoteInQuoted(CsvBuilder& b, char16_t ch, document::TextPos pos, char16_t delimiter) {
    if (ch == u'"') {
        b.state = FieldState::Quoted;  // "" escape - back inside the quoted field
    } else if (ch == delimiter) {
        b.finalizeField(pos);
        b.startField(pos + 1);
    } else if (ch == u'\r') {
        b.crPending = true;
        b.state     = FieldState::Unquoted;
    } else if (ch == u'\n') {
        b.finalizeField(pos);
        b.finalizeRow();
        b.startField(pos + 1);
    } else {
        // Dirty trailing content right after a closing quote (e.g.
        // `"abc"def,`) - lenient absorption: keep accumulating the SAME
        // field as an unquoted one rather than treating this as an error.
        b.state = FieldState::Unquoted;
    }
}

// Row/field terminator detection needs one character of lookback to tell
// CRLF from a lone '\r' or a lone '\n', but never true lookahead: seeing
// '\r' outside Quoted state only sets `crPending` (the field/row is NOT
// finalized yet); the NEXT character then either confirms it was a CRLF
// pair (next char is '\n') or reveals the '\r' was ordinary content (any
// other next character, including EOF - handled by build()'s unconditional
// final flush never having shortened the field for a pending '\r' that was
// never confirmed). This mirrors document::LineIndex's own convention
// (line_index.cpp) that only '\n' is a line terminator - a lone '\r' is
// just a regular character there too.
void processChar(CsvBuilder& b, char16_t ch, document::TextPos pos, char16_t delimiter) {
    if (b.crPending) {
        b.crPending = false;
        if (ch == u'\n') {
            // Confirmed CRLF: the field ends one position before this '\n',
            // excluding the '\r' captured at pos-1.
            b.finalizeField(pos - 1);
            b.finalizeRow();
            b.startField(pos + 1);
            return;
        }
        // Not a CRLF after all - the '\r' was ordinary content, already
        // implicitly included in whatever field span is still open (its
        // position was never excluded from anything). Fall through and
        // process `ch` normally in the current state.
    }

    switch (b.state) {
        case FieldState::FieldStart:
            handleFieldStart(b, ch, pos, delimiter);
            break;
        case FieldState::Unquoted:
            handleUnquoted(b, ch, pos, delimiter);
            break;
        case FieldState::Quoted:
            handleQuoted(b, ch);
            break;
        case FieldState::QuoteInQuoted:
            handleQuoteInQuoted(b, ch, pos, delimiter);
            break;
    }
}

}  // namespace

std::expected<CsvModel, CsvParseError> CsvModel::build(const document::BufferSnapshot& snapshot,
                                                         const CsvParseOptions&           options) {
    if (options.delimiter == u'\r' || options.delimiter == u'\n' || options.delimiter == u'"') {
        return std::unexpected(CsvParseError::InvalidDelimiter);
    }

    CsvBuilder builder;
    // Single linear pass over the piece list, index-based (not the
    // range-for pieceView() walk logmode::LogModel::build() uses) because,
    // unlike LogModel, every character's absolute document::TextPos is
    // needed to record CsvCell::startPos/endPos - mirrors
    // document::LineIndex::build()'s own cursor+i indexing (line_index.cpp).
    document::TextPos cursor = 0;
    for (const document::Piece& piece : snapshot.pieces()) {
        const std::u16string_view v = snapshot.pieceView(piece);
        for (std::size_t i = 0; i < v.size(); ++i) {
            processChar(builder, v[i], cursor + i, options.delimiter);
        }
        cursor += piece.length;
    }
    // Final field/row, unconditionally - mirrors document::Document's own
    // "an empty document still has one line" and "a trailing '\n' produces
    // an implicit empty final line" conventions (logmode_log_model_test.cpp
    // pins the latter down for LogModel; the same reasoning applies here
    // since this parser's row terminator is likewise only ever '\n').
    builder.finalizeField(cursor);
    builder.finalizeRow();

    CsvModel model;
    model.m_cells          = std::move(builder.cells);
    model.m_rowOffsets     = std::move(builder.rowOffsets);
    model.m_maxColumnCount = builder.maxColumnCount;
    model.m_hasHeader      = options.hasHeader;
    return model;
}

std::expected<CsvModel, CsvParseError> CsvModel::build(const document::Document& doc,
                                                         const CsvParseOptions&    options) {
    return build(*doc.snapshot(), options);
}

std::span<const CsvCell> CsvModel::row(std::size_t rowIndex) const noexcept {
    const std::uint32_t begin = m_rowOffsets[rowIndex];
    const std::uint32_t end   = m_rowOffsets[rowIndex + 1];
    return std::span<const CsvCell>(m_cells).subspan(begin, end - begin);
}

std::span<const CsvCell> CsvModel::headerRow() const noexcept {
    return m_hasHeader ? row(0) : std::span<const CsvCell>{};
}

std::size_t CsvModel::dataRowCount() const noexcept {
    return m_hasHeader ? rowCount() - 1 : rowCount();
}

std::span<const CsvCell> CsvModel::dataRow(std::size_t dataRowIndex) const noexcept {
    return m_hasHeader ? row(dataRowIndex + 1) : row(dataRowIndex);
}

std::u16string csvCellValue(const document::BufferSnapshot& snapshot, const CsvCell& cell) {
    std::u16string raw = snapshot.extract(document::TextRange{.start = cell.startPos, .end = cell.endPos});
    if (!cell.quoted) {
        return raw;
    }
    // Guaranteed by CsvCell::quoted's contract (see CsvBuilder::finalizeField()):
    // raw both starts and ends with '"' here, so stripping one character
    // from each end and un-escaping "" -> " is always well-defined - no
    // need to re-derive that guarantee from the text itself.
    std::u16string decoded;
    decoded.reserve(raw.size() - 2);
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        decoded.push_back(raw[i]);
        if (raw[i] == u'"') {
            ++i;  // the second '"' of an escaped pair - already accounted for
        }
    }
    return decoded;
}

std::u16string csvCellValue(const document::Document& doc, const CsvCell& cell) {
    return csvCellValue(*doc.snapshot(), cell);
}

std::u16string escapeCsvCellText(std::u16string_view value, char16_t delimiter) {
    const bool needsQuoting = value.find(delimiter) != std::u16string_view::npos ||
                              value.find(u'"') != std::u16string_view::npos ||
                              value.find(u'\r') != std::u16string_view::npos ||
                              value.find(u'\n') != std::u16string_view::npos;
    if (!needsQuoting) {
        return std::u16string(value);
    }
    std::u16string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(u'"');
    for (const char16_t ch : value) {
        if (ch == u'"') {
            escaped.push_back(u'"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back(u'"');
    return escaped;
}

}  // namespace neomifes::csvmode
