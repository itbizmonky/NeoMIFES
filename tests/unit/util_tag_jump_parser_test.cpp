#include <gtest/gtest.h>

#include <optional>

#include "neomifes/util/tag_jump_parser.h"

namespace {

using neomifes::util::parseTagJumpReference;
using neomifes::util::TagJumpReference;

TEST(TagJumpParserTest, ParsesPathAndLineOnly) {
    const auto result = parseTagJumpReference(u"foo.cpp(12)");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"foo.cpp");
    EXPECT_EQ(reference.line, 12U);
    EXPECT_FALSE(reference.column.has_value());
}

TEST(TagJumpParserTest, ParsesPathLineAndColumn) {
    const auto result = parseTagJumpReference(u"foo.cpp(12,5)");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"foo.cpp");
    EXPECT_EQ(reference.line, 12U);
    ASSERT_TRUE(reference.column.has_value());
    EXPECT_EQ(*reference.column, 5U);
}

TEST(TagJumpParserTest, FindsReferenceEmbeddedInLargerLine) {
    const auto result =
        parseTagJumpReference(u"1>foo.cpp(12,5): error C2065: 'x': undeclared identifier");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"foo.cpp");
    EXPECT_EQ(reference.line, 12U);
    ASSERT_TRUE(reference.column.has_value());
    EXPECT_EQ(*reference.column, 5U);
}

TEST(TagJumpParserTest, FindsReferenceWithAbsoluteDrivePath) {
    const auto result = parseTagJumpReference(u"C:\\src\\foo\\bar.cpp(12,5): error");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"C:\\src\\foo\\bar.cpp");
}

TEST(TagJumpParserTest, FindsReferenceWithUncPath) {
    const auto result = parseTagJumpReference(u"\\\\server\\share\\foo.cpp(12): error");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"\\\\server\\share\\foo.cpp");
}

TEST(TagJumpParserTest, FindsReferenceWithRelativeSubdirPathBackslash) {
    const auto result = parseTagJumpReference(u"subdir\\foo.cpp(12): error");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"subdir\\foo.cpp");
}

TEST(TagJumpParserTest, FindsReferenceWithRelativeSubdirPathForwardSlash) {
    const auto result = parseTagJumpReference(u"subdir/foo.cpp(12): error");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"subdir/foo.cpp");
}

TEST(TagJumpParserTest, NoMatchWhenNoParens) {
    EXPECT_FALSE(parseTagJumpReference(u"just a plain line of text").has_value());
}

TEST(TagJumpParserTest, NoMatchForIfStatement) {
    EXPECT_FALSE(parseTagJumpReference(u"if (x) {").has_value());
}

TEST(TagJumpParserTest, NoMatchForFunctionCallWithNonNumericArg) {
    EXPECT_FALSE(parseTagJumpReference(u"Foo(bar)").has_value());
}

TEST(TagJumpParserTest, NoMatchWhenPathHasNoExtension) {
    EXPECT_FALSE(parseTagJumpReference(u"Makefile(12)").has_value());
    EXPECT_FALSE(parseTagJumpReference(u"SomeClass::Method(12)").has_value());
}

TEST(TagJumpParserTest, NoMatchForThreeArgParenGroup) {
    EXPECT_FALSE(parseTagJumpReference(u"foo.cpp(1,2,3)").has_value());
}

TEST(TagJumpParserTest, NoMatchForEmptyParens) {
    EXPECT_FALSE(parseTagJumpReference(u"foo.cpp()").has_value());
}

TEST(TagJumpParserTest, NoMatchForTrailingDotNoExtension) {
    EXPECT_FALSE(parseTagJumpReference(u"foo.(12)").has_value());
}

TEST(TagJumpParserTest, NoMatchForLineZero) {
    EXPECT_FALSE(parseTagJumpReference(u"foo.cpp(0)").has_value());
}

TEST(TagJumpParserTest, NoMatchForColumnZero) {
    EXPECT_FALSE(parseTagJumpReference(u"foo.cpp(12,0)").has_value());
}

TEST(TagJumpParserTest, FirstMatchWinsWithTwoValidGroups) {
    const auto result =
        parseTagJumpReference(u"foo.cpp(12,5): error C2065 (declared at bar.h(30))");
    ASSERT_TRUE(result.has_value());
    const TagJumpReference& reference = *result;
    EXPECT_EQ(reference.path, u"foo.cpp");
    EXPECT_EQ(reference.line, 12U);
    ASSERT_TRUE(reference.column.has_value());
    EXPECT_EQ(*reference.column, 5U);
}

TEST(TagJumpParserTest, EqualityOperatorWorks) {
    const TagJumpReference a{.path = u"foo.cpp", .line = 12, .column = 5};
    const TagJumpReference b{.path = u"foo.cpp", .line = 12, .column = 5};
    const TagJumpReference c{.path = u"foo.cpp", .line = 13, .column = 5};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

}  // namespace
