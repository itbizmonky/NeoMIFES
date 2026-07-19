#include <gtest/gtest.h>

#include <optional>

#include "neomifes/ui/goto_line_parser.h"

namespace {

using neomifes::ui::GotoTarget;
using neomifes::ui::parseGotoLineInput;

TEST(GotoLineParserTest, ParsesLineOnly) {
    const auto result = parseGotoLineInput(u"123");
    ASSERT_TRUE(result.has_value());
    const GotoTarget target = *result;
    EXPECT_EQ(target.line, 123U);
    EXPECT_FALSE(target.column.has_value());
}

TEST(GotoLineParserTest, ParsesLineAndColumn) {
    const auto result = parseGotoLineInput(u"123:45");
    ASSERT_TRUE(result.has_value());
    const GotoTarget target = *result;
    EXPECT_EQ(target.line, 123U);
    ASSERT_TRUE(target.column.has_value());
    EXPECT_EQ(*target.column, 45U);
}

TEST(GotoLineParserTest, EmptyInputReturnsNullopt) {
    EXPECT_FALSE(parseGotoLineInput(u"").has_value());
}

TEST(GotoLineParserTest, NonNumericInputReturnsNullopt) {
    EXPECT_FALSE(parseGotoLineInput(u"abc").has_value());
    EXPECT_FALSE(parseGotoLineInput(u"12a").has_value());
    EXPECT_FALSE(parseGotoLineInput(u"12:3a").has_value());
}

TEST(GotoLineParserTest, LineZeroReturnsNullopt) {
    // 1-based input - "line 0" has no meaning to the user-facing Ctrl+G prompt.
    EXPECT_FALSE(parseGotoLineInput(u"0").has_value());
    EXPECT_FALSE(parseGotoLineInput(u"0:5").has_value());
}

TEST(GotoLineParserTest, ColumnZeroReturnsNullopt) {
    EXPECT_FALSE(parseGotoLineInput(u"5:0").has_value());
}

TEST(GotoLineParserTest, TrailingColonWithNoColumnReturnsNullopt) {
    EXPECT_FALSE(parseGotoLineInput(u"123:").has_value());
}

TEST(GotoLineParserTest, LeadingColonWithNoLineReturnsNullopt) {
    EXPECT_FALSE(parseGotoLineInput(u":45").has_value());
}

TEST(GotoLineParserTest, NegativeSignReturnsNullopt) {
    EXPECT_FALSE(parseGotoLineInput(u"-5").has_value());
}

TEST(GotoLineParserTest, WhitespaceIsNotStripped) {
    EXPECT_FALSE(parseGotoLineInput(u" 123").has_value());
    EXPECT_FALSE(parseGotoLineInput(u"123 ").has_value());
}

TEST(GotoLineParserTest, EqualityOperatorWorks) {
    EXPECT_EQ((GotoTarget{.line = 1, .column = std::nullopt}),
             (GotoTarget{.line = 1, .column = std::nullopt}));
    EXPECT_NE((GotoTarget{.line = 1, .column = std::nullopt}), (GotoTarget{.line = 2, .column = std::nullopt}));
}

}  // namespace
