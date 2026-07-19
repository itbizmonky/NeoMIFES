#include <gtest/gtest.h>

#include "neomifes/util/fuzzy_matcher.h"

namespace {

using neomifes::util::fuzzyMatchScore;

TEST(FuzzyMatcherTest, EmptyQueryMatchesWithZeroScore) {
    EXPECT_EQ(fuzzyMatchScore(u"", u"Find"), 0);
    EXPECT_EQ(fuzzyMatchScore(u"", u""), 0);
}

TEST(FuzzyMatcherTest, SubsequenceMatchReturnsScore) {
    EXPECT_TRUE(fuzzyMatchScore(u"find", u"Find").has_value());
}

TEST(FuzzyMatcherTest, NonSubsequenceReturnsNullopt) {
    EXPECT_FALSE(fuzzyMatchScore(u"xyz", u"Find").has_value());
    EXPECT_FALSE(fuzzyMatchScore(u"dnif", u"Find").has_value());
}

TEST(FuzzyMatcherTest, MatchIsCaseInsensitiveOverAscii) {
    const auto lower = fuzzyMatchScore(u"find", u"Find");
    const auto upper = fuzzyMatchScore(u"FIND", u"Find");
    ASSERT_TRUE(lower.has_value());
    ASSERT_TRUE(upper.has_value());
    EXPECT_EQ(*lower, *upper);
}

TEST(FuzzyMatcherTest, ConsecutiveMatchScoresHigherThanScattered) {
    // "Find" is a consecutive run inside "Find and Replace"; "Frp" only
    // hits scattered word-initial letters, no consecutive-run bonus.
    const auto consecutive = fuzzyMatchScore(u"find", u"Find and Replace");
    const auto scattered   = fuzzyMatchScore(u"frp", u"Find and Replace");
    ASSERT_TRUE(consecutive.has_value());
    ASSERT_TRUE(scattered.has_value());
    EXPECT_GT(*consecutive, *scattered);
}

TEST(FuzzyMatcherTest, WordBoundaryMatchScoresHigherThanMidWordMatch) {
    // Greedy leftmost matching picks the FIRST occurrence of 'r' in each
    // target: index 0 of "Redo" (a word boundary) vs. index 3 of
    // "starting" (mid-word, preceded by lowercase 'a'). Both match exactly
    // one character, so any score difference is solely the boundary bonus.
    const auto boundary = fuzzyMatchScore(u"r", u"Redo");
    const auto midWord   = fuzzyMatchScore(u"r", u"starting");
    ASSERT_TRUE(boundary.has_value());
    ASSERT_TRUE(midWord.has_value());
    EXPECT_GT(*boundary, *midWord);
}

TEST(FuzzyMatcherTest, SubsequenceAcrossMultipleWordsStillMatches) {
    const auto score = fuzzyMatchScore(u"fr", u"Find and Replace");
    ASSERT_TRUE(score.has_value());
}

}  // namespace
