#pragma once

// computeCsvRowOrder() - headless filter+sort computation over a CsvModel's
// data rows (WI-16d, Phase 10.2 continuation). Pure function, no UI, no
// EditorSession wiring, no async worker - see this header's build_plan.md
// WI-16d entry for why (mirrors WI-14a/WI-15a/WI-16a's own "headless first"
// staging: async wiring and UI, if ever needed for this specific
// computation, are later sub-WIs).
//
// Requirements doc §9 lists "フィルタ" and "検索" as separate CSV-mode
// items; this WI deliberately serves both with ONE mechanism - a
// case-insensitive substring match against any cell in a row - rather than
// a column-scoped equality filter (the roadmap's own `[Filter: City ==
// Tokyo]` UI mockup). A column-picker filter-builder is a materially
// bigger UI surface for a 1000-万-row grid than this codebase's other
// WI-16 sub-steps have taken on at once; see build_plan.md's WI-16d design
// notes for the full reasoning. A column-scoped equality filter can be
// added later without breaking this function's shape (CsvFilterOptions
// would simply grow a new field).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::csvmode {

class CsvModel;

struct CsvFilterOptions {
    // Empty = no filtering (every data row passes). Non-empty: a data row
    // passes iff at least one of its cells' DECODED text (csvCellValue())
    // contains `query` as a substring. ASCII-only case-insensitive
    // comparison - the same "std::towlower per char16_t" convention this
    // codebase already established for syntax_language.h's
    // detectLanguage() and log_pattern_file.cpp's hasJsonExtension();
    // non-ASCII characters compare case-sensitively (documented
    // limitation, not a bug - full Unicode case-folding is not attempted).
    std::u16string query;
};

enum class CsvSortDirection : std::uint8_t { None, Ascending, Descending };

struct CsvSortOptions {
    // Which CSV column (0-based, into a data row's own cell span - NOT
    // CsvGridPane's shifted "#"-column-inclusive index space) to sort by.
    // Ignored when direction == None.
    std::size_t      column    = 0;
    CsvSortDirection direction = CsvSortDirection::None;
};

// Returns the data-row indices (each a valid argument to
// CsvModel::dataRow()) that survive `filter`, in the order `sort`
// specifies. With filter.query empty and sort.direction == None, this
// returns 0..model.dataRowCount()-1 unchanged (an identity permutation) -
// callers building a CsvGridPane-facing indirection layer can call this
// unconditionally without special-casing "no filter/sort configured".
//
// A row whose own cell count doesn't reach `sort.column` (a ragged row
// shorter than the sort column) sorts as if that cell were empty text -
// the same "out of range reads as empty" convention csv_grid_bridge.h's
// csvGridCellText() already established.
//
// Sorting compares each row's sort-column cell as a NUMBER when both sides
// parse cleanly as one (see tryParseCsvNumber() in the .cpp), falling back
// to plain std::u16string comparison otherwise - this keeps a numeric
// column ("9" before "10") from sorting lexicographically ("10" before
// "9"), matching what the roadmap's own `[Sort: Score desc]` mockup
// implies. The sort itself is stable (std::stable_sort): rows that compare
// equal keep their filtered-order relative position.
[[nodiscard]] std::vector<std::size_t> computeCsvRowOrder(const CsvModel& model, const document::Document& doc,
                                                            const CsvFilterOptions& filter,
                                                            const CsvSortOptions&   sort);

}  // namespace neomifes::csvmode
