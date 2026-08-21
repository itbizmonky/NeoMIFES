#include "neomifes/csvmode/csv_row_order.h"

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "neomifes/csvmode/csv_model.h"
#include "neomifes/document/document.h"

namespace neomifes::csvmode {

namespace {

// ASCII-only casefold - same "std::towlower per char16_t" convention this
// codebase already established for syntax_language.h's detectLanguage()
// and log_pattern_file.cpp's hasJsonExtension() (see csv_row_order.h's
// CsvFilterOptions::query comment for why full Unicode case-folding is
// deliberately not attempted here).
[[nodiscard]] std::u16string asciiToLower(std::u16string_view s) {
    std::u16string out(s);
    for (char16_t& ch : out) {
        if (ch <= u'\x7f') {
            ch = static_cast<char16_t>(std::towlower(static_cast<wint_t>(ch)));
        }
    }
    return out;
}

// Naive O(haystack*needle) substring scan, casefolding `haystack` on the
// fly rather than allocating a lowercased copy of every cell - `needleLower`
// is already lowercased once by the caller (asciiToLower(filter.query)),
// so only one side of each comparison needs the towlower() call here.
[[nodiscard]] bool asciiCaseInsensitiveContains(std::u16string_view haystack, std::u16string_view needleLower) {
    if (needleLower.empty()) {
        return true;
    }
    if (needleLower.size() > haystack.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needleLower.size() <= haystack.size(); ++i) {
        bool matched = true;
        for (std::size_t j = 0; j < needleLower.size(); ++j) {
            char16_t hc = haystack[i + j];
            if (hc <= u'\x7f') {
                hc = static_cast<char16_t>(std::towlower(static_cast<wint_t>(hc)));
            }
            if (hc != needleLower[j]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool rowMatchesFilter(const CsvModel& model, const document::Document& doc, std::size_t dataRowIndex,
                                     std::u16string_view queryLower) {
    if (queryLower.empty()) {
        return true;
    }
    for (const CsvCell& cell : model.dataRow(dataRowIndex)) {
        if (asciiCaseInsensitiveContains(csvCellValue(doc, cell), queryLower)) {
            return true;
        }
    }
    return false;
}

// Ragged row shorter than `column`, or `column` past a row's own cell
// count: reads as empty text - the same "out of range reads as empty"
// convention csv_grid_bridge.h's csvGridCellText() already established.
[[nodiscard]] std::u16string sortKeyText(const CsvModel& model, const document::Document& doc,
                                          std::size_t dataRowIndex, std::size_t column) {
    const std::span<const CsvCell> row = model.dataRow(dataRowIndex);
    if (column >= row.size()) {
        return u"";
    }
    return csvCellValue(doc, row[column]);
}

// A cell parses as a CSV number only if its ENTIRE text is ASCII and
// std::from_chars consumes all of it (e.g. "12" -> 12.0, but "12px" or a
// value containing non-ASCII digits does not parse) - a bounded stack
// buffer (goto_line_parser.h's own char16_t->char narrowing pattern),
// oversized values (>=32 chars, far past any realistic numeric CSV field)
// simply fall back to lexicographic comparison rather than growing a heap
// buffer for a rare case.
[[nodiscard]] std::optional<double> tryParseCsvNumber(std::u16string_view value) {
    if (value.empty() || value.size() >= 32) {
        return std::nullopt;
    }
    char buf[32];
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] > u'\x7f') {
            return std::nullopt;
        }
        buf[i] = static_cast<char>(value[i]);
    }
    double     result = 0.0;
    const auto parsed  = std::from_chars(buf, buf + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != buf + value.size()) {
        return std::nullopt;
    }
    return result;
}

// Numeric comparison when both sides parse cleanly as a CSV number (keeps
// "9" sorting before "10" instead of after it), lexicographic
// std::u16string comparison otherwise - see csv_row_order.h's
// computeCsvRowOrder() doc comment for the rationale.
[[nodiscard]] bool lessForSort(const std::u16string& lhs, const std::u16string& rhs) {
    const std::optional<double> lhsNumber = tryParseCsvNumber(lhs);
    const std::optional<double> rhsNumber = tryParseCsvNumber(rhs);
    if (lhsNumber.has_value() && rhsNumber.has_value()) {
        return *lhsNumber < *rhsNumber;
    }
    return lhs < rhs;
}

}  // namespace

std::vector<std::size_t> computeCsvRowOrder(const CsvModel& model, const document::Document& doc,
                                             const CsvFilterOptions& filter, const CsvSortOptions& sort) {
    const std::u16string queryLower = asciiToLower(filter.query);

    std::vector<std::size_t> order;
    order.reserve(model.dataRowCount());
    for (std::size_t dataRowIndex = 0; dataRowIndex < model.dataRowCount(); ++dataRowIndex) {
        if (rowMatchesFilter(model, doc, dataRowIndex, queryLower)) {
            order.push_back(dataRowIndex);
        }
    }

    if (sort.direction == CsvSortDirection::None) {
        return order;
    }

    // Precompute each surviving row's sort key once - std::stable_sort's
    // comparator runs O(n log n) times, and re-decoding the same cell that
    // often would multiply csvCellValue()'s per-call cost for no reason.
    std::vector<std::pair<std::u16string, std::size_t>> keyed;
    keyed.reserve(order.size());
    for (std::size_t dataRowIndex : order) {
        keyed.emplace_back(sortKeyText(model, doc, dataRowIndex, sort.column), dataRowIndex);
    }

    const bool ascending = sort.direction == CsvSortDirection::Ascending;
    std::stable_sort(keyed.begin(), keyed.end(), [ascending](const auto& lhs, const auto& rhs) {
        return ascending ? lessForSort(lhs.first, rhs.first) : lessForSort(rhs.first, lhs.first);
    });

    order.clear();
    for (const auto& [key, dataRowIndex] : keyed) {
        order.push_back(dataRowIndex);
    }
    return order;
}

}  // namespace neomifes::csvmode
