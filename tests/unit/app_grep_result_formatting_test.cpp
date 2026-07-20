#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "neomifes/app/grep_result_formatting.h"
#include "neomifes/search/grep_service.h"

namespace {

using neomifes::app::formatGrepResultRow;
using neomifes::search::GrepMatch;

TEST(GrepResultFormattingTest, FormatsLineNumberAsOneBasedNotZeroBased) {
    const GrepMatch match{.path     = std::filesystem::path(L"D:\\src\\foo.cpp"),
                          .line     = 0,
                          .columnRange = {},
                          .lineText = u"if (error) {"};
    const std::u16string row = formatGrepResultRow(match);
    EXPECT_NE(row.find(u"(1)"), std::u16string::npos);
    EXPECT_EQ(row.find(u"(0)"), std::u16string::npos);
}

TEST(GrepResultFormattingTest, IncludesFullPathVerbatim) {
    const GrepMatch match{.path     = std::filesystem::path(L"D:\\src\\foo.cpp"),
                          .line     = 11,
                          .columnRange = {},
                          .lineText = u"if (error) {"};
    const std::u16string row = formatGrepResultRow(match);
    EXPECT_NE(row.find(u"D:\\src\\foo.cpp"), std::u16string::npos);
}

TEST(GrepResultFormattingTest, IncludesLineTextVerbatim) {
    const GrepMatch match{.path     = std::filesystem::path(L"D:\\src\\foo.cpp"),
                          .line     = 11,
                          .columnRange = {},
                          .lineText = u"if (error) {"};
    const std::u16string row = formatGrepResultRow(match);
    EXPECT_NE(row.find(u"if (error) {"), std::u16string::npos);
}

TEST(GrepResultFormattingTest, HandlesEmptyLineText) {
    const GrepMatch match{.path = std::filesystem::path(L"D:\\src\\foo.cpp"),
                          .line = 0,
                          .columnRange = {},
                          .lineText = u""};
    const std::u16string row = formatGrepResultRow(match);
    EXPECT_EQ(row, u"D:\\src\\foo.cpp(1): ");
}

TEST(GrepResultFormattingTest, ColumnRangeDoesNotAffectFormattedRow) {
    const GrepMatch withRange{.path        = std::filesystem::path(L"D:\\src\\foo.cpp"),
                              .line        = 0,
                              .columnRange = {.start = 3, .end = 8},
                              .lineText    = u"if (error) {"};
    const GrepMatch withoutRange{.path        = std::filesystem::path(L"D:\\src\\foo.cpp"),
                                 .line        = 0,
                                 .columnRange = {},
                                 .lineText    = u"if (error) {"};
    EXPECT_EQ(formatGrepResultRow(withRange), formatGrepResultRow(withoutRange));
}

}  // namespace
