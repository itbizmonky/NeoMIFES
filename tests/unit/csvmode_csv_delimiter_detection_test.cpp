#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string_view>

#include "neomifes/csvmode/csv_delimiter_detection.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::csvmode::detectCsvDelimiter;
using neomifes::document::Document;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

TEST(CsvDelimiterDetectionTest, DetectsComma) {
    const Document doc    = makeDoc(u"a,b,c\n1,2,3\n4,5,6\n");
    const auto     result = detectCsvDelimiter(doc);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, u',');
}

TEST(CsvDelimiterDetectionTest, DetectsTab) {
    const Document doc    = makeDoc(u"a\tb\tc\n1\t2\t3\n4\t5\t6\n");
    const auto     result = detectCsvDelimiter(doc);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, u'\t');
}

TEST(CsvDelimiterDetectionTest, DetectsSemicolon) {
    const Document doc    = makeDoc(u"a;b;c\n1;2;3\n4;5;6\n");
    const auto     result = detectCsvDelimiter(doc);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, u';');
}

TEST(CsvDelimiterDetectionTest, DetectsPipe) {
    const Document doc    = makeDoc(u"a|b|c\n1|2|3\n4|5|6\n");
    const auto     result = detectCsvDelimiter(doc);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, u'|');
}

TEST(CsvDelimiterDetectionTest, RaggedRowsStillDetectTheModalDelimiterCount) {
    // Four rows share a comma count of 2 (three columns); one row has only
    // 1 comma (two columns) - the modal (most common) non-zero count for
    // ',' is still 2, shared by 4 of 5 lines, clearing the 0.5 threshold.
    const Document doc    = makeDoc(u"a,b,c\n1,2,3\n4,5,6\n7,8,9\nragged,pair\n");
    const auto     result = detectCsvDelimiter(doc);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, u',');
}

TEST(CsvDelimiterDetectionTest, PlainProseWithNoCandidateCharacterReturnsNullopt) {
    const Document doc = makeDoc(u"hello world\nthis is a test\nno delimiters here\n");
    EXPECT_FALSE(detectCsvDelimiter(doc).has_value());
}

TEST(CsvDelimiterDetectionTest, EmptyDocumentReturnsNullopt) {
    const Document doc = makeDoc(u"");
    // lineCount() is 1 for an empty document, but that single empty line
    // contains none of the candidate characters, so every candidate scores
    // zero and the result is still nullopt.
    EXPECT_FALSE(detectCsvDelimiter(doc).has_value());
}

TEST(CsvDelimiterDetectionTest, ZeroSampleLinesReturnsNullopt) {
    const Document doc = makeDoc(u"a,b,c\n1,2,3\n");
    EXPECT_FALSE(detectCsvDelimiter(doc, /*sampleLines=*/0).has_value());
}

TEST(CsvDelimiterDetectionTest, EmptyCandidateListReturnsNullopt) {
    const Document doc = makeDoc(u"a,b,c\n1,2,3\n");
    EXPECT_FALSE(detectCsvDelimiter(doc, /*sampleLines=*/100, std::span<const char16_t>{}).has_value());
}

}  // namespace
