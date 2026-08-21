#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "neomifes/app/csv_grid_bridge.h"

namespace {

using neomifes::app::buildCsvGridColumnLabels;
using neomifes::app::csvGridCellText;
using neomifes::csvmode::CsvModel;
using neomifes::csvmode::CsvParseOptions;
using neomifes::csvmode::CsvSortDirection;
using neomifes::csvmode::CsvSortOptions;
using neomifes::document::Document;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// --- buildCsvGridColumnLabels() -------------------------------------------

TEST(AppCsvGridBridgeTest, HeaderRowDecodesToColumnLabels) {
    const Document doc   = makeDoc(u"name,age\nAlice,30\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    const auto labels = buildCsvGridColumnLabels(*model, doc);
    ASSERT_EQ(labels.size(), 2U);
    EXPECT_EQ(labels[0], u"name");
    EXPECT_EQ(labels[1], u"age");
}

TEST(AppCsvGridBridgeTest, NoHeaderSynthesizesColumnNLabels) {
    const Document  doc = makeDoc(u"1,2,3\n4,5,6\n");
    CsvParseOptions options;
    options.hasHeader = false;
    const auto model   = CsvModel::build(doc, options);
    ASSERT_TRUE(model.has_value());

    const auto labels = buildCsvGridColumnLabels(*model, doc);
    ASSERT_EQ(labels.size(), 3U);
    EXPECT_EQ(labels[0], u"Column 1");
    EXPECT_EQ(labels[1], u"Column 2");
    EXPECT_EQ(labels[2], u"Column 3");
}

TEST(AppCsvGridBridgeTest, RaggedHeaderRowSynthesizesTrailingColumnLabels) {
    // Header row has only 2 cells but a later data row has 3 - maxColumnCount()
    // is 3, so the header-derived labels must be padded with a synthesized
    // "Column 3" for the column the header row itself doesn't cover.
    const Document doc   = makeDoc(u"a,b\n1,2,3\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->maxColumnCount(), 3U);

    const auto labels = buildCsvGridColumnLabels(*model, doc);
    ASSERT_EQ(labels.size(), 3U);
    EXPECT_EQ(labels[0], u"a");
    EXPECT_EQ(labels[1], u"b");
    EXPECT_EQ(labels[2], u"Column 3");
}

TEST(AppCsvGridBridgeTest, EmptyDocumentProducesOneEmptyColumnLabel) {
    // CsvModel::build()'s own contract: an entirely empty document still
    // produces one row with one (empty) cell - which becomes the header row
    // under the default hasHeader=true, so there is exactly one column, not
    // zero.
    const Document doc   = makeDoc(u"");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->maxColumnCount(), 1U);

    const auto labels = buildCsvGridColumnLabels(*model, doc);
    ASSERT_EQ(labels.size(), 1U);
    EXPECT_EQ(labels[0], u"");
}

// --- buildCsvGridColumnLabels() sort-arrow suffix (WI-16e) ----------------

TEST(AppCsvGridBridgeTest, AscendingSortAppendsUpArrowToSortedColumnOnly) {
    const Document doc   = makeDoc(u"name,age\nAlice,30\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvSortOptions sort;
    sort.column    = 1;
    sort.direction = CsvSortDirection::Ascending;
    const auto labels = buildCsvGridColumnLabels(*model, doc, sort);
    ASSERT_EQ(labels.size(), 2U);
    EXPECT_EQ(labels[0], u"name");
    EXPECT_EQ(labels[1], u"age ▲");
}

TEST(AppCsvGridBridgeTest, DescendingSortAppendsDownArrow) {
    const Document doc   = makeDoc(u"name,age\nAlice,30\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvSortOptions sort;
    sort.column    = 0;
    sort.direction = CsvSortDirection::Descending;
    const auto labels = buildCsvGridColumnLabels(*model, doc, sort);
    EXPECT_EQ(labels[0], u"name ▼");
    EXPECT_EQ(labels[1], u"age");
}

TEST(AppCsvGridBridgeTest, NoneDirectionAppendsNoArrow) {
    const Document doc   = makeDoc(u"name,age\nAlice,30\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    const auto labels = buildCsvGridColumnLabels(*model, doc, CsvSortOptions{});
    EXPECT_EQ(labels[0], u"name");
    EXPECT_EQ(labels[1], u"age");
}

TEST(AppCsvGridBridgeTest, SortColumnBeyondLabelCountIsIgnoredWithoutCrashing) {
    const Document doc   = makeDoc(u"name,age\nAlice,30\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvSortOptions sort;
    sort.column    = 5;
    sort.direction = CsvSortDirection::Ascending;
    const auto labels = buildCsvGridColumnLabels(*model, doc, sort);
    EXPECT_EQ(labels[0], u"name");
    EXPECT_EQ(labels[1], u"age");
}

// --- csvGridCellText() -----------------------------------------------------

TEST(AppCsvGridBridgeTest, ValidCellReturnsDecodedText) {
    const Document doc   = makeDoc(u"name,age\nAlice,30\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    EXPECT_EQ(csvGridCellText(*model, doc, 0, 0), u"Alice");
    EXPECT_EQ(csvGridCellText(*model, doc, 0, 1), u"30");
}

TEST(AppCsvGridBridgeTest, RowIndexBeyondDataRowCountReturnsEmptyString) {
    const Document doc   = makeDoc(u"a,b\n1,2\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    EXPECT_EQ(csvGridCellText(*model, doc, model->dataRowCount() + 10, 0), u"");
}

TEST(AppCsvGridBridgeTest, ColumnIndexBeyondRaggedRowsOwnCellCountReturnsEmptyString) {
    // Row 0 (data) has 3 cells; row 1 (data) has only 1 - a ragged row.
    // Column index 2 is valid for row 0 but out of range for row 1.
    const Document  doc = makeDoc(u"1,2,3\n4\n");
    CsvParseOptions options;
    options.hasHeader = false;
    const auto model   = CsvModel::build(doc, options);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->maxColumnCount(), 3U);

    EXPECT_EQ(csvGridCellText(*model, doc, 0, 2), u"3");
    EXPECT_EQ(csvGridCellText(*model, doc, 1, 2), u"");
}

TEST(AppCsvGridBridgeTest, EmptyCsvModelHasNoDataRows) {
    const Document doc   = makeDoc(u"");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    EXPECT_EQ(csvGridCellText(*model, doc, 0, 0), u"");
}

}  // namespace
