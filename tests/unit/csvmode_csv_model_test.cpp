#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "neomifes/csvmode/csv_model.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::csvmode::csvCellValue;
using neomifes::csvmode::CsvModel;
using neomifes::csvmode::CsvParseError;
using neomifes::csvmode::CsvParseOptions;
using neomifes::csvmode::escapeCsvCellText;
using neomifes::document::Document;
using neomifes::document::TextRange;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// --- Structural correctness -------------------------------------------

TEST(CsvModelTest, SimpleTwoByTwoGridWithHeaderAndTrailingImplicitEmptyRow) {
    const Document doc   = makeDoc(u"a,b\n1,2\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());

    // 3, not 2: a trailing '\n' produces an implicit empty final row - same
    // Document::lineCount() convention LogModel already relies on.
    EXPECT_EQ(model->rowCount(), 3U);
    EXPECT_TRUE(model->hasHeader());
    ASSERT_EQ(model->headerRow().size(), 2U);
    EXPECT_EQ(model->dataRowCount(), 2U);
    ASSERT_EQ(model->dataRow(0).size(), 2U);
    ASSERT_EQ(model->dataRow(1).size(), 1U);  // the trailing implicit empty row
    EXPECT_EQ(model->maxColumnCount(), 2U);
}

TEST(CsvModelTest, HasHeaderFalseTreatsEveryRowAsData) {
    const Document doc   = makeDoc(u"1,2\n3,4\n");
    CsvParseOptions options;
    options.hasHeader   = false;
    const auto model    = CsvModel::build(doc, options);
    ASSERT_TRUE(model.has_value());
    EXPECT_FALSE(model->hasHeader());
    EXPECT_TRUE(model->headerRow().empty());
    EXPECT_EQ(model->dataRowCount(), model->rowCount());
    ASSERT_EQ(model->dataRow(0).size(), 2U);
}

TEST(CsvModelTest, RaggedRowsAreNotAnErrorAndMaxColumnCountReflectsTheWidestRow) {
    const Document doc   = makeDoc(u"a,b,c\nd,e\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->rowCount(), 3U);
    EXPECT_EQ(model->row(0).size(), 3U);
    EXPECT_EQ(model->row(1).size(), 2U);
    EXPECT_EQ(model->row(2).size(), 1U);  // trailing implicit empty row
    EXPECT_EQ(model->maxColumnCount(), 3U);
}

// --- Quote handling ------------------------------------------------------

TEST(CsvModelTest, QuotedFieldWithEmbeddedNewlineSpansMultipleDocumentLines) {
    // a , "b\nc" , d \n   <- one CSV row, but the middle field's raw span
    // covers a literal newline, so this ONE row occupies TWO document lines.
    const Document doc   = makeDoc(u"a,\"b\nc\",d\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->rowCount(), 2U);
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 3U);
    EXPECT_EQ(csvCellValue(doc, row0[0]), u"a");
    EXPECT_TRUE(row0[1].quoted);
    EXPECT_EQ(csvCellValue(doc, row0[1]), u"b\nc");
    EXPECT_EQ(csvCellValue(doc, row0[2]), u"d");

    // The invariant a future grid UI's "cursor position -> cell" mapping
    // depends on: this cell's own end lands on a later document line than
    // its start, because the quoted content itself contains a '\n'.
    EXPECT_GT(doc.offsetToLine(row0[1].endPos), doc.offsetToLine(row0[1].startPos));
}

TEST(CsvModelTest, DoubledQuoteInsideQuotedFieldUnescapesToOneQuote) {
    const Document doc   = makeDoc(u"\"a\"\"b\"\n");  // CSV source: "a""b"
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 1U);
    EXPECT_TRUE(row0[0].quoted);
    EXPECT_EQ(csvCellValue(doc, row0[0]), u"a\"b");
}

TEST(CsvModelTest, EmptyQuotedFieldDecodesToEmptyString) {
    const Document doc   = makeDoc(u"\"\",b\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 2U);
    EXPECT_TRUE(row0[0].quoted);
    EXPECT_EQ(csvCellValue(doc, row0[0]), u"");
}

TEST(CsvModelTest, CrlfRowTerminatorExcludesTheCrFromTheLastCellsSpan) {
    const Document doc   = makeDoc(u"a,b\r\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 2U);
    EXPECT_EQ(row0[1].startPos, 2U);
    EXPECT_EQ(row0[1].endPos, 3U);  // excludes the '\r' at position 3
    EXPECT_EQ(csvCellValue(doc, row0[1]), u"b");
}

// --- Lenient absorption of syntactically loose input ----------------------

TEST(CsvModelTest, EmptyDocumentProducesOneRowWithOneEmptyCell) {
    const Document doc   = makeDoc(u"");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->rowCount(), 1U);
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 1U);
    EXPECT_EQ(row0[0].startPos, 0U);
    EXPECT_EQ(row0[0].endPos, 0U);
    EXPECT_FALSE(row0[0].quoted);
}

TEST(CsvModelTest, TrailingDelimiterProducesAnAdditionalEmptyCell) {
    const Document doc   = makeDoc(u"a,\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 2U);
    EXPECT_EQ(csvCellValue(doc, row0[0]), u"a");
    EXPECT_EQ(csvCellValue(doc, row0[1]), u"");
}

TEST(CsvModelTest, UnterminatedQuoteExtendsTheFieldToEndOfDocumentWithoutError) {
    const Document doc   = makeDoc(u"\"abc");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->rowCount(), 1U);
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 1U);
    // Never reached a closing quote, so this is NOT flagged as a clean
    // quoted token - csvCellValue() must not try to strip/unescape it.
    EXPECT_FALSE(row0[0].quoted);
    EXPECT_EQ(csvCellValue(doc, row0[0]), u"\"abc");
}

TEST(CsvModelTest, GarbageAfterClosingQuoteFallsBackToUnquotedLeniently) {
    const Document doc   = makeDoc(u"\"abc\"def,\n");  // CSV source: "abc"def,
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 2U);
    EXPECT_FALSE(row0[0].quoted);
    EXPECT_EQ(csvCellValue(doc, row0[0]), u"\"abc\"def");
}

// --- Decoding via csvCellValue() ------------------------------------------

TEST(CsvModelTest, UnquotedFieldValueIsReturnedVerbatim) {
    const Document doc   = makeDoc(u"hello world\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(csvCellValue(doc, model->row(0)[0]), u"hello world");
}

// --- Encoding via escapeCsvCellText() (WI-16f, cell editing) --------------

TEST(EscapeCsvCellTextTest, PlainValueIsReturnedUnquoted) {
    EXPECT_EQ(escapeCsvCellText(u"hello world", u','), u"hello world");
}

TEST(EscapeCsvCellTextTest, EmptyValueRoundTripsToEmptyUnquotedCell) {
    EXPECT_EQ(escapeCsvCellText(u"", u','), u"");
}

TEST(EscapeCsvCellTextTest, ValueContainingDelimiterIsQuoted) {
    EXPECT_EQ(escapeCsvCellText(u"a,b", u','), u"\"a,b\"");
}

TEST(EscapeCsvCellTextTest, ValueContainingTabDelimiterIsQuoted) {
    EXPECT_EQ(escapeCsvCellText(u"a\tb", u'\t'), u"\"a\tb\"");
}

TEST(EscapeCsvCellTextTest, ValueContainingQuoteIsQuotedAndQuoteIsDoubled) {
    EXPECT_EQ(escapeCsvCellText(u"a\"b", u','), u"\"a\"\"b\"");
}

TEST(EscapeCsvCellTextTest, ValueContainingCrIsQuoted) {
    EXPECT_EQ(escapeCsvCellText(u"a\rb", u','), u"\"a\rb\"");
}

TEST(EscapeCsvCellTextTest, ValueContainingLfIsQuoted) {
    EXPECT_EQ(escapeCsvCellText(u"a\nb", u','), u"\"a\nb\"");
}

TEST(EscapeCsvCellTextTest, ValueContainingOnlyANonDelimiterCommaIsNotQuotedWhenDelimiterIsSemicolon) {
    EXPECT_EQ(escapeCsvCellText(u"a,b", u';'), u"a,b");
}

TEST(EscapeCsvCellTextTest, RoundTripsWithCsvCellValueForUnquotedField) {
    const Document doc   = makeDoc(u"hello world\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    const auto& cell  = model->row(0)[0];
    const auto  value = csvCellValue(doc, cell);
    EXPECT_EQ(escapeCsvCellText(value, u','), u"hello world");
}

TEST(EscapeCsvCellTextTest, RoundTripsWithCsvCellValueForQuotedFieldWithEmbeddedQuote) {
    const Document doc   = makeDoc(u"\"a\"\"b\"\n");
    const auto     model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    const auto& cell  = model->row(0)[0];
    const auto  value = csvCellValue(doc, cell);
    EXPECT_EQ(value, u"a\"b");
    EXPECT_EQ(escapeCsvCellText(value, u','), u"\"a\"\"b\"");
}

// --- Piece-boundary regression guard --------------------------------------

TEST(CsvModelTest, ParsingAcrossMultiplePiecesMatchesSinglePieceResult) {
    // Forces the CRLF terminator and the quoted field's content to actually
    // span multiple PieceTable pieces (not just the one insertText() call
    // every other test in this file exercises via makeDoc()) - mirrors
    // LogModelTest.LineContentSpanningMultiplePiecesMatchesCorrectly and
    // JsonTreeTest.ParseJsonTreeAcrossMultiplePiecesMatchesSinglePieceResult.
    const std::u16string text = u"a,\"b\nc\",d\r\ne,f\n";
    Document              doc;
    doc.insertText(0, text);
    constexpr std::uint64_t kSplitPos = 6;  // inside the quoted field's content
    doc.insertText(kSplitPos, u"XXXX");
    doc.eraseRange(TextRange{.start = kSplitPos, .end = kSplitPos + 4});

    ASSERT_GT(doc.snapshot()->pieces().size(), 1U);

    const auto model = CsvModel::build(doc);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->rowCount(), 3U);
    const auto row0 = model->row(0);
    ASSERT_EQ(row0.size(), 3U);
    EXPECT_EQ(csvCellValue(doc, row0[0]), u"a");
    EXPECT_EQ(csvCellValue(doc, row0[1]), u"b\nc");
    EXPECT_EQ(csvCellValue(doc, row0[2]), u"d");
    const auto row1 = model->row(1);
    ASSERT_EQ(row1.size(), 2U);
    EXPECT_EQ(csvCellValue(doc, row1[0]), u"e");
    EXPECT_EQ(csvCellValue(doc, row1[1]), u"f");
}

// --- BufferSnapshot overload -----------------------------------------------

// Extracted from BuildFromBufferSnapshotMatchesDocumentOverload below purely
// to keep that test under clang-tidy's cognitive-complexity threshold - the
// nested loop plus its own ASSERT/EXPECT macros pushed the single-function
// form over the limit. No behavior change.
void expectRowsMatch(std::size_t rowCount, const CsvModel& lhsModel, const CsvModel& rhsModel) {
    for (std::size_t r = 0; r < rowCount; ++r) {
        const auto lhs = lhsModel.row(r);
        const auto rhs = rhsModel.row(r);
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t c = 0; c < lhs.size(); ++c) {
            EXPECT_EQ(lhs[c], rhs[c]);
        }
    }
}

TEST(CsvModelTest, BuildFromBufferSnapshotMatchesDocumentOverload) {
    const Document doc         = makeDoc(u"a,b\n\"c,d\",e\n");
    const auto     viaDocument = CsvModel::build(doc);
    const auto     viaSnapshot = CsvModel::build(*doc.snapshot());
    ASSERT_TRUE(viaDocument.has_value());
    ASSERT_TRUE(viaSnapshot.has_value());
    ASSERT_EQ(viaDocument->rowCount(), viaSnapshot->rowCount());
    expectRowsMatch(viaDocument->rowCount(), *viaDocument, *viaSnapshot);
}

// --- Failure contract -------------------------------------------------

TEST(CsvModelTest, DelimiterEqualToCrLfOrQuoteIsRejected) {
    CsvParseOptions options;
    const Document  doc = makeDoc(u"a,b\n");

    options.delimiter = u'\r';
    EXPECT_FALSE(CsvModel::build(doc, options).has_value());

    options.delimiter = u'\n';
    EXPECT_FALSE(CsvModel::build(doc, options).has_value());

    options.delimiter = u'"';
    const auto result = CsvModel::build(doc, options);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CsvParseError::InvalidDelimiter);
}

}  // namespace
