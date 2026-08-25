#include <gtest/gtest.h>

#include "neomifes/app/git_diff_view_bridge.h"

namespace {

using neomifes::app::buildDiffViewDocumentText;
using neomifes::app::buildDiffViewLineMarkers;
using neomifes::git::UnifiedDiffLine;
using neomifes::git::UnifiedDiffLineKind;
using neomifes::render::GitDiffKind;

TEST(AppGitDiffViewBridgeTest, EmptyInputYieldsEmptyText) {
    EXPECT_EQ(buildDiffViewDocumentText({}), u"");
}

TEST(AppGitDiffViewBridgeTest, JoinsLinesWithNewlineNoTrailingNewline) {
    const std::vector<UnifiedDiffLine> lines{
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line1"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Added, .text = u"line2"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line3"},
    };
    EXPECT_EQ(buildDiffViewDocumentText(lines), u"line1\nline2\nline3");
}

TEST(AppGitDiffViewBridgeTest, EmptyInputYieldsEmptyMarkers) {
    const auto markers = buildDiffViewLineMarkers({});
    EXPECT_TRUE(markers.empty());
}

TEST(AppGitDiffViewBridgeTest, ContextOnlyLinesProduceNoMarkers) {
    const std::vector<UnifiedDiffLine> lines{
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line1"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line2"},
    };
    const auto markers = buildDiffViewLineMarkers(lines);
    EXPECT_TRUE(markers.empty());
}

TEST(AppGitDiffViewBridgeTest, CompressesConsecutiveAddedLinesIntoOneRange) {
    const std::vector<UnifiedDiffLine> lines{
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line1"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Added, .text = u"line2"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Added, .text = u"line3"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line4"},
    };
    const auto markers = buildDiffViewLineMarkers(lines);
    ASSERT_EQ(markers.size(), 1U);
    EXPECT_EQ(markers[0].startLine, 1U);
    EXPECT_EQ(markers[0].lineCount, 2U);
    EXPECT_EQ(markers[0].kind, GitDiffKind::Added);
}

TEST(AppGitDiffViewBridgeTest, RemovedMapsToDeletedKind) {
    const std::vector<UnifiedDiffLine> lines{
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Removed, .text = u"gone"},
    };
    const auto markers = buildDiffViewLineMarkers(lines);
    ASSERT_EQ(markers.size(), 1U);
    EXPECT_EQ(markers[0].startLine, 0U);
    EXPECT_EQ(markers[0].lineCount, 1U);
    EXPECT_EQ(markers[0].kind, GitDiffKind::Deleted);
}

// Mirrors the exact real-world shape unifiedDiffAgainstHead() produces for a
// single-line modification: Removed immediately followed by Added must stay
// as TWO SEPARATE one-line ranges (different kinds), not merge into one.
TEST(AppGitDiffViewBridgeTest, AdjacentRemovedThenAddedStayAsSeparateRanges) {
    const std::vector<UnifiedDiffLine> lines{
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line1"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Removed, .text = u"old"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Added, .text = u"new"},
        UnifiedDiffLine{.kind = UnifiedDiffLineKind::Context, .text = u"line4"},
    };
    const auto markers = buildDiffViewLineMarkers(lines);
    ASSERT_EQ(markers.size(), 2U);
    EXPECT_EQ(markers[0].startLine, 1U);
    EXPECT_EQ(markers[0].lineCount, 1U);
    EXPECT_EQ(markers[0].kind, GitDiffKind::Deleted);
    EXPECT_EQ(markers[1].startLine, 2U);
    EXPECT_EQ(markers[1].lineCount, 1U);
    EXPECT_EQ(markers[1].kind, GitDiffKind::Added);
}

}  // namespace
