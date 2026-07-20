#include <gtest/gtest.h>

#include "neomifes/util/glob_match.h"

namespace {

using neomifes::util::globMatch;

TEST(GlobMatchTest, ExactLiteralMatches) {
    EXPECT_TRUE(globMatch(u"foo.cpp", u"foo.cpp"));
}

TEST(GlobMatchTest, ExactLiteralMismatch) {
    EXPECT_FALSE(globMatch(u"foo.cpp", u"bar.cpp"));
}

TEST(GlobMatchTest, IsAnchoredNotSubstring) {
    // A prefix match must not be treated as a full match - a bare "foo"
    // pattern does not match "foobar".
    EXPECT_FALSE(globMatch(u"foo", u"foobar"));
    EXPECT_FALSE(globMatch(u"bar", u"foobar"));
}

TEST(GlobMatchTest, StarAloneMatchesAnythingIncludingEmpty) {
    EXPECT_TRUE(globMatch(u"*", u"foo.cpp"));
    EXPECT_TRUE(globMatch(u"*", u""));
}

TEST(GlobMatchTest, StarAsSuffixWildcard) {
    EXPECT_TRUE(globMatch(u"*.cpp", u"foo.cpp"));
    EXPECT_TRUE(globMatch(u"*.cpp", u"a/b/foo.cpp"));  // '*' has no path-separator meaning here
    EXPECT_FALSE(globMatch(u"*.cpp", u"foo.h"));
}

TEST(GlobMatchTest, StarAsPrefixWildcard) {
    EXPECT_TRUE(globMatch(u"foo*", u"foo.cpp"));
    EXPECT_TRUE(globMatch(u"foo*", u"foo"));
    EXPECT_FALSE(globMatch(u"foo*", u"bar.cpp"));
}

TEST(GlobMatchTest, QuestionMarkMatchesExactlyOneCharacter) {
    EXPECT_TRUE(globMatch(u"foo.?pp", u"foo.cpp"));
    EXPECT_FALSE(globMatch(u"foo.?pp", u"foo.hpp2"));
    EXPECT_FALSE(globMatch(u"foo.?pp", u"foo.pp"));  // '?' requires exactly one char, not zero
}

TEST(GlobMatchTest, CombinedStarAndQuestionMarkPattern) {
    EXPECT_TRUE(globMatch(u"*foo*bar?.txt", u"xxfooyybarZ.txt"));
    EXPECT_FALSE(globMatch(u"*foo*bar?.txt", u"xxfooyybar.txt"));
}

TEST(GlobMatchTest, IsCaseInsensitiveOverAscii) {
    EXPECT_TRUE(globMatch(u"*.CPP", u"foo.cpp"));
    EXPECT_TRUE(globMatch(u"*.cpp", u"FOO.CPP"));
}

TEST(GlobMatchTest, EmptyPatternOnlyMatchesEmptyText) {
    EXPECT_TRUE(globMatch(u"", u""));
    EXPECT_FALSE(globMatch(u"", u"foo"));
}

}  // namespace
