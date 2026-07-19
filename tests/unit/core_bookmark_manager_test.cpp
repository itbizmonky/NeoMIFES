#include <gtest/gtest.h>

#include "neomifes/core/bookmark_manager.h"

namespace {

using neomifes::core::BookmarkManager;

TEST(BookmarkManagerTest, TogglingAnUnbookmarkedLineAddsIt) {
    BookmarkManager manager;
    manager.toggle(5);
    EXPECT_TRUE(manager.isBookmarked(5));
    ASSERT_EQ(manager.lines().size(), 1U);
    EXPECT_EQ(manager.lines()[0], 5U);
}

TEST(BookmarkManagerTest, TogglingAnAlreadyBookmarkedLineRemovesIt) {
    BookmarkManager manager;
    manager.toggle(5);
    manager.toggle(5);
    EXPECT_FALSE(manager.isBookmarked(5));
    EXPECT_TRUE(manager.lines().empty());
}

TEST(BookmarkManagerTest, LinesAreKeptSortedRegardlessOfToggleOrder) {
    BookmarkManager manager;
    manager.toggle(10);
    manager.toggle(2);
    manager.toggle(7);
    ASSERT_EQ(manager.lines().size(), 3U);
    EXPECT_EQ(manager.lines()[0], 2U);
    EXPECT_EQ(manager.lines()[1], 7U);
    EXPECT_EQ(manager.lines()[2], 10U);
}

TEST(BookmarkManagerTest, IsBookmarkedIsFalseForUntouchedLine) {
    BookmarkManager manager;
    manager.toggle(5);
    EXPECT_FALSE(manager.isBookmarked(6));
}

TEST(BookmarkManagerTest, NextReturnsNulloptWithNoBookmarks) {
    const BookmarkManager manager;
    EXPECT_FALSE(manager.next(0).has_value());
}

TEST(BookmarkManagerTest, PreviousReturnsNulloptWithNoBookmarks) {
    const BookmarkManager manager;
    EXPECT_FALSE(manager.previous(0).has_value());
}

TEST(BookmarkManagerTest, NextFindsTheNearestBookmarkAfterFrom) {
    BookmarkManager manager;
    manager.toggle(2);
    manager.toggle(7);
    manager.toggle(10);
    const auto result = manager.next(5);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 7U);
}

TEST(BookmarkManagerTest, NextWrapsAroundToTheFirstBookmarkPastTheLast) {
    BookmarkManager manager;
    manager.toggle(2);
    manager.toggle(7);
    const auto result = manager.next(10);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2U);
}

TEST(BookmarkManagerTest, PreviousFindsTheNearestBookmarkBeforeFrom) {
    BookmarkManager manager;
    manager.toggle(2);
    manager.toggle(7);
    manager.toggle(10);
    const auto result = manager.previous(8);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 7U);
}

TEST(BookmarkManagerTest, PreviousWrapsAroundToTheLastBookmarkBeforeTheFirst) {
    BookmarkManager manager;
    manager.toggle(2);
    manager.toggle(7);
    const auto result = manager.previous(0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 7U);
}

TEST(BookmarkManagerTest, NextAndPreviousCycleThroughASingleBookmark) {
    BookmarkManager manager;
    manager.toggle(5);
    const auto nextResult = manager.next(5);
    ASSERT_TRUE(nextResult.has_value());
    EXPECT_EQ(*nextResult, 5U);
    const auto previousResult = manager.previous(5);
    ASSERT_TRUE(previousResult.has_value());
    EXPECT_EQ(*previousResult, 5U);
}

}  // namespace
