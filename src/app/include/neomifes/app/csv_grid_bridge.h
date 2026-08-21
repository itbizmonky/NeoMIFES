#pragma once

// buildCsvGridColumnLabels()/csvGridCellText() - converts a csvmode::CsvModel
// (WI-16a) into the plain std::u16string labels/cell text ui::CsvGridPane's
// LVN_GETDISPINFOW callback needs (WI-16c). Header-only, pure, and free of
// Windows-SDK includes so it stays unit-testable without a live HWND,
// mirroring json_tree_bridge.h's rationale - this lives under src/app/
// rather than src/ui/ because it depends on neomifes::csvmode, and ui:: is
// deliberately kept free of that dependency.
//
// Both functions take the model AND the source document::Document: CsvCell
// stores only startPos/endPos (WI-16a's "don't duplicate what's cheap to
// recompute" design, csv_model.h's own header comment), so decoding a
// cell's text always requires the original document alongside the model.

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "neomifes/csvmode/csv_model.h"
#include "neomifes/document/document.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

// One label per column, always exactly model.maxColumnCount() entries
// (never per-row-length - ragged rows are a data-row concern, not a
// header concern). hasHeader()==true decodes headerRow() via
// csvmode::csvCellValue(); a header row shorter than maxColumnCount()
// (ragged) gets synthesized "Column N" labels for its missing trailing
// columns, same fallback as the hasHeader()==false case.
[[nodiscard]] inline std::vector<std::u16string> buildCsvGridColumnLabels(const csvmode::CsvModel&   model,
                                                                            const document::Document& doc) {
    const std::size_t columnCount = model.maxColumnCount();
    const auto         header      = model.hasHeader() ? model.headerRow() : std::span<const csvmode::CsvCell>{};

    std::vector<std::u16string> labels;
    labels.reserve(columnCount);
    for (std::size_t i = 0; i < columnCount; ++i) {
        if (i < header.size()) {
            labels.push_back(csvmode::csvCellValue(doc, header[i]));
        } else {
            // Same std::to_wstring()+util::fromWstringView() bridge
            // grep_result_formatting.h's formatGrepResultRow() already uses
            // for a decimal-number-into-u16string conversion - std::u16string
            // has no direct std::to_string()/to_wstring() equivalent of its
            // own on this toolchain.
            const std::wstring   ordinalWide(std::to_wstring(i + 1));
            const std::u16string ordinal(util::fromWstringView(ordinalWide));
            labels.push_back(u"Column " + ordinal);
        }
    }
    return labels;
}

// Empty string for any out-of-range access (rowIndex >= dataRowCount(), or
// colIndex beyond that particular row's own cell count - ragged rows are
// absorbed leniently, matching csv_model.h's own parsing contract) -
// never asserts or throws, same "harmless empty result" convention
// buildGrepQueryFromInput() established for this codebase.
[[nodiscard]] inline std::u16string csvGridCellText(const csvmode::CsvModel& model, const document::Document& doc,
                                                     std::size_t rowIndex, std::size_t colIndex) noexcept {
    if (rowIndex >= model.dataRowCount()) {
        return u"";
    }
    const auto row = model.dataRow(rowIndex);
    if (colIndex >= row.size()) {
        return u"";
    }
    return csvmode::csvCellValue(doc, row[colIndex]);
}

}  // namespace neomifes::app
