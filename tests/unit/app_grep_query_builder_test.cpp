#include <gtest/gtest.h>

#include <optional>

#include "neomifes/app/grep_query_builder.h"
#include "neomifes/search/grep_service.h"

namespace {

using neomifes::app::buildGrepQueryFromInput;
using neomifes::search::GrepQuery;

TEST(GrepQueryBuilderTest, EmptyQueryReturnsNullopt) {
    EXPECT_FALSE(buildGrepQueryFromInput(u"", u"D:\\src").has_value());
}

TEST(GrepQueryBuilderTest, EmptyFolderReturnsNullopt) {
    EXPECT_FALSE(buildGrepQueryFromInput(u"needle", u"").has_value());
}

TEST(GrepQueryBuilderTest, WhitespaceOnlyInputsAreTreatedAsEmpty) {
    EXPECT_FALSE(buildGrepQueryFromInput(u"  \t", u"D:\\src").has_value());
    EXPECT_FALSE(buildGrepQueryFromInput(u"needle", u" \t \n").has_value());
}

TEST(GrepQueryBuilderTest, TrimsLeadingAndTrailingWhitespaceFromBothFields) {
    const auto result = buildGrepQueryFromInput(u"  needle  ", u"  D:\\src  ");
    ASSERT_TRUE(result.has_value());
    const GrepQuery& query = *result;
    EXPECT_EQ(query.query.pattern, u"needle");
    ASSERT_EQ(query.roots.size(), 1U);
    EXPECT_EQ(query.roots[0].wstring(), L"D:\\src");
}

TEST(GrepQueryBuilderTest, BuildsSingleRootFromFolderText) {
    const auto result = buildGrepQueryFromInput(u"needle", u"D:\\src");
    ASSERT_TRUE(result.has_value());
    const GrepQuery& query = *result;
    ASSERT_EQ(query.roots.size(), 1U);
    EXPECT_EQ(query.roots[0].wstring(), L"D:\\src");
}

TEST(GrepQueryBuilderTest, SetsQueryPatternFromTrimmedQueryText) {
    const auto result = buildGrepQueryFromInput(u"needle", u"D:\\src");
    ASSERT_TRUE(result.has_value());
    const GrepQuery& query = *result;
    EXPECT_EQ(query.query.pattern, u"needle");
}

TEST(GrepQueryBuilderTest, LeavesIncludeAndExcludeGlobsEmpty) {
    const auto result = buildGrepQueryFromInput(u"needle", u"D:\\src");
    ASSERT_TRUE(result.has_value());
    const GrepQuery& query = *result;
    EXPECT_TRUE(query.includeGlobs.empty());
    EXPECT_TRUE(query.excludeGlobs.empty());
}

TEST(GrepQueryBuilderTest, UsesQueryStructsOwnDefaultsForCaseSensitiveWholeWordRegex) {
    const auto result = buildGrepQueryFromInput(u"needle", u"D:\\src");
    ASSERT_TRUE(result.has_value());
    const GrepQuery& query = *result;
    EXPECT_TRUE(query.query.caseSensitive);
    EXPECT_FALSE(query.query.wholeWord);
    EXPECT_FALSE(query.query.regex);
}

TEST(GrepQueryBuilderTest, SucceedsForANonexistentPathSinceItPerformsNoFilesystemAccess) {
    // No exists()/is_directory() check - GrepService::findAll() already
    // skips a root that doesn't exist (grep_service.h), so this function
    // stays a pure computation with no I/O.
    const auto result = buildGrepQueryFromInput(u"needle", u"Z:\\this\\path\\does\\not\\exist");
    ASSERT_TRUE(result.has_value());
    const GrepQuery& query = *result;
    ASSERT_EQ(query.roots.size(), 1U);
    EXPECT_EQ(query.roots[0].wstring(), L"Z:\\this\\path\\does\\not\\exist");
}

}  // namespace
