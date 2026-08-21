#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "neomifes/csvmode/csv_model.h"
#include "neomifes/csvmode/csv_row_order.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::csvmode::computeCsvRowOrder;
using neomifes::csvmode::CsvFilterOptions;
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

// 4 data rows (header excluded): name, score, city.
// score is numeric-but-out-of-lexicographic-order ("30","9","10","5") so a
// naive string sort would put "10" before "9" and "30" before "5" - the
// numeric-aware comparator this WI adds must not do that.
// city deliberately mixes case ("Tokyo"/"Osaka"/"Kyoto"/"tokyo") to exercise
// both the filter's case-INsensitivity and the sort's case-SENSITIVITY
// (lessForSort() never casefolds - only the filter path does).
// Deliberately no trailing '\n' - CsvModel treats a trailing '\n' as
// introducing one MORE implicit empty row (see
// csvmode_csv_model_test.cpp's SimpleTwoByTwoGridWithHeaderAndTrailing-
// EmptyRow), which would make dataRowCount() 5, not 4.
[[nodiscard]] Document makeSampleDoc() {
    return makeDoc(u"name,score,city\nAlice,30,Tokyo\nBob,9,Osaka\nCarol,10,Kyoto\nDave,5,tokyo");
}

// --- No filter / no sort --------------------------------------------------

TEST(ComputeCsvRowOrderTest, EmptyFilterAndNoneDirectionReturnsIdentityOrder) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->dataRowCount(), 4U);

    const auto order = computeCsvRowOrder(*model, doc, CsvFilterOptions{}, CsvSortOptions{});
    EXPECT_EQ(order, (std::vector<std::size_t>{0, 1, 2, 3}));
}

// --- Filter ----------------------------------------------------------------

TEST(ComputeCsvRowOrderTest, FilterMatchesSubstringAcrossAnyCellCaseInsensitively) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvFilterOptions filter;
    filter.query   = u"tokyo";  // lowercase query, should match both "Tokyo" and "tokyo"
    const auto order = computeCsvRowOrder(*model, doc, filter, CsvSortOptions{});
    EXPECT_EQ(order, (std::vector<std::size_t>{0, 3}));  // Alice (Tokyo), Dave (tokyo)
}

TEST(ComputeCsvRowOrderTest, FilterWithNoMatchesReturnsEmptyOrder) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvFilterOptions filter;
    filter.query      = u"nonexistent";
    const auto order = computeCsvRowOrder(*model, doc, filter, CsvSortOptions{});
    EXPECT_TRUE(order.empty());
}

TEST(ComputeCsvRowOrderTest, RaggedRowsDoNotCrashTheFilterPass) {
    // hasHeader=false: row 0 has 3 cells, row 1 has only 1, and the trailing
    // '\n' produces a 3rd, implicit, empty row (CsvModel's own documented
    // convention - see csvmode_csv_model_test.cpp's
    // SimpleTwoByTwoGridWithHeaderAndTrailingImplicitEmptyRow). A filter
    // query that would only ever match a cell past row 1/2's own width must
    // not read out of bounds, and must still find row 0's match.
    const Document doc   = makeDoc(u"a,b,c\nd\n");
    CsvParseOptions options;
    options.hasHeader = false;
    const auto model  = CsvModel::build(doc, options);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->dataRowCount(), 3U);

    CsvFilterOptions filter;
    filter.query      = u"c";
    const auto order = computeCsvRowOrder(*model, doc, filter, CsvSortOptions{});
    EXPECT_EQ(order, (std::vector<std::size_t>{0}));
}

// --- Sort --------------------------------------------------------------

TEST(ComputeCsvRowOrderTest, AscendingSortOnNumericColumnOrdersByValueNotLexicographically) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvSortOptions sort;
    sort.column    = 1;  // score
    sort.direction = CsvSortDirection::Ascending;
    const auto order = computeCsvRowOrder(*model, doc, CsvFilterOptions{}, sort);
    // Dave=5, Bob=9, Carol=10, Alice=30 - a lexicographic sort would put
    // "10" and "30" before "5"/"9".
    EXPECT_EQ(order, (std::vector<std::size_t>{3, 1, 2, 0}));
}

TEST(ComputeCsvRowOrderTest, DescendingSortOnNumericColumnReversesOrder) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvSortOptions sort;
    sort.column    = 1;  // score
    sort.direction = CsvSortDirection::Descending;
    const auto order = computeCsvRowOrder(*model, doc, CsvFilterOptions{}, sort);
    EXPECT_EQ(order, (std::vector<std::size_t>{0, 2, 1, 3}));
}

TEST(ComputeCsvRowOrderTest, AscendingSortOnNonNumericColumnUsesCaseSensitiveLexicographicOrder) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvSortOptions sort;
    sort.column    = 2;  // city: "Tokyo", "Osaka", "Kyoto", "tokyo"
    sort.direction = CsvSortDirection::Ascending;
    const auto order = computeCsvRowOrder(*model, doc, CsvFilterOptions{}, sort);
    // ASCII order: 'K' < 'O' < 'T' < 't', so Kyoto < Osaka < Tokyo < tokyo -
    // lessForSort() itself never casefolds (only the filter path does).
    EXPECT_EQ(order, (std::vector<std::size_t>{2, 1, 0, 3}));
}

TEST(ComputeCsvRowOrderTest, SortColumnBeyondRowWidthTreatsAllRowsAsEqualAndPreservesOriginalOrder) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvSortOptions sort;
    sort.column    = 5;  // past the 3 real columns
    sort.direction = CsvSortDirection::Ascending;
    const auto order = computeCsvRowOrder(*model, doc, CsvFilterOptions{}, sort);
    EXPECT_EQ(order, (std::vector<std::size_t>{0, 1, 2, 3}));  // stable: original order preserved
}

// --- Filter + sort combined -------------------------------------------

TEST(ComputeCsvRowOrderTest, FilterThenSortPreservesOriginalDataRowIndicesThroughBoth) {
    const Document doc   = makeSampleDoc();
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    CsvFilterOptions filter;
    filter.query = u"tokyo";  // matches Alice (0, score 30) and Dave (3, score 5)
    CsvSortOptions sort;
    sort.column    = 1;
    sort.direction = CsvSortDirection::Ascending;
    const auto order = computeCsvRowOrder(*model, doc, filter, sort);
    EXPECT_EQ(order, (std::vector<std::size_t>{3, 0}));  // Dave (5) before Alice (30)
}

// --- Empty model ---------------------------------------------------------

TEST(ComputeCsvRowOrderTest, ZeroDataRowsReturnsEmptyOrderRegardlessOfFilterOrSort) {
    const Document doc   = makeDoc(u"a,b");  // header only, no trailing newline -> 0 data rows
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->dataRowCount(), 0U);

    CsvSortOptions sort;
    sort.direction = CsvSortDirection::Ascending;
    const auto order = computeCsvRowOrder(*model, doc, CsvFilterOptions{}, sort);
    EXPECT_TRUE(order.empty());
}

}  // namespace
