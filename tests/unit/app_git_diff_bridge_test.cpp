#include <gtest/gtest.h>

#include "neomifes/app/git_diff_bridge.h"

namespace {

using neomifes::app::buildGitDiffMarkers;
using neomifes::git::LineDiffKind;
using neomifes::git::LineDiffRegion;
using neomifes::render::GitDiffKind;

TEST(AppGitDiffBridgeTest, EmptyInputYieldsEmptyOutput) {
    const auto markers = buildGitDiffMarkers({});
    EXPECT_TRUE(markers.empty());
}

TEST(AppGitDiffBridgeTest, ConvertsAddedRegion) {
    const std::vector<LineDiffRegion> regions{
        LineDiffRegion{.startLine = 3, .lineCount = 2, .kind = LineDiffKind::Added}};

    const auto markers = buildGitDiffMarkers(regions);
    ASSERT_EQ(markers.size(), 1U);
    EXPECT_EQ(markers[0].startLine, 3U);
    EXPECT_EQ(markers[0].lineCount, 2U);
    EXPECT_EQ(markers[0].kind, GitDiffKind::Added);
}

TEST(AppGitDiffBridgeTest, ConvertsModifiedRegion) {
    const std::vector<LineDiffRegion> regions{
        LineDiffRegion{.startLine = 10, .lineCount = 1, .kind = LineDiffKind::Modified}};

    const auto markers = buildGitDiffMarkers(regions);
    ASSERT_EQ(markers.size(), 1U);
    EXPECT_EQ(markers[0].kind, GitDiffKind::Modified);
}

TEST(AppGitDiffBridgeTest, ConvertsDeletedRegionPreservingZeroLineCount) {
    const std::vector<LineDiffRegion> regions{
        LineDiffRegion{.startLine = 7, .lineCount = 0, .kind = LineDiffKind::Deleted}};

    const auto markers = buildGitDiffMarkers(regions);
    ASSERT_EQ(markers.size(), 1U);
    EXPECT_EQ(markers[0].startLine, 7U);
    EXPECT_EQ(markers[0].lineCount, 0U);
    EXPECT_EQ(markers[0].kind, GitDiffKind::Deleted);
}

TEST(AppGitDiffBridgeTest, ConvertsMultipleRegionsPreservingOrder) {
    const std::vector<LineDiffRegion> regions{
        LineDiffRegion{.startLine = 1, .lineCount = 1, .kind = LineDiffKind::Added},
        LineDiffRegion{.startLine = 5, .lineCount = 3, .kind = LineDiffKind::Modified},
        LineDiffRegion{.startLine = 9, .lineCount = 0, .kind = LineDiffKind::Deleted},
    };

    const auto markers = buildGitDiffMarkers(regions);
    ASSERT_EQ(markers.size(), 3U);
    EXPECT_EQ(markers[0].kind, GitDiffKind::Added);
    EXPECT_EQ(markers[1].kind, GitDiffKind::Modified);
    EXPECT_EQ(markers[2].kind, GitDiffKind::Deleted);
}

}  // namespace
