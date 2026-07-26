#include <gtest/gtest.h>

#include <vector>

#include "neomifes/core/folding_model.h"

namespace {

using neomifes::core::FoldingModel;
using neomifes::core::FoldRegion;

TEST(FoldingModelTest, EmptyModelHasNoHiddenOrHeaderLines) {
    FoldingModel model;
    EXPECT_FALSE(model.isLineHidden(5));
    EXPECT_FALSE(model.isFoldHeader(5));
    EXPECT_FALSE(model.regionAt(5).has_value());
}

TEST(FoldingModelTest, ToggleFoldHidesLinesStrictlyAfterHeaderThroughEndInclusive) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    model.toggleFold(10);

    EXPECT_TRUE(model.regionAt(10)->folded);
    EXPECT_FALSE(model.isLineHidden(10));  // header itself always visible
    EXPECT_TRUE(model.isLineHidden(11));
    EXPECT_TRUE(model.isLineHidden(15));   // endLineInclusive itself is hidden
    EXPECT_FALSE(model.isLineHidden(16));  // just past the region
}

TEST(FoldingModelTest, TogglingAFoldedRegionAgainUnfoldsIt) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    model.toggleFold(10);
    model.toggleFold(10);
    EXPECT_FALSE(model.regionAt(10)->folded);
    EXPECT_FALSE(model.isLineHidden(12));
}

TEST(FoldingModelTest, ToggleFoldOnUnknownHeaderLineIsNoOp) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    model.toggleFold(999);
    EXPECT_FALSE(model.regionAt(10)->folded);
}

TEST(FoldingModelTest, IsFoldHeaderIsTrueOnlyForRegionHeaderLines) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    EXPECT_TRUE(model.isFoldHeader(10));
    EXPECT_FALSE(model.isFoldHeader(11));
}

TEST(FoldingModelTest, FoldingOuterRegionHidesNestedInnerHeaderToo) {
    FoldingModel model;
    // outer: 0..20 (e.g. class), inner: 5..10 (e.g. a method inside it).
    model.setFoldableRegions({
        FoldRegion{.headerLine = 0, .endLineInclusive = 20, .folded = false},
        FoldRegion{.headerLine = 5, .endLineInclusive = 10, .folded = false},
    });
    model.toggleFold(0);

    EXPECT_FALSE(model.isLineHidden(0));   // outer header stays visible
    EXPECT_TRUE(model.isLineHidden(5));    // inner header now hidden by outer fold
    EXPECT_TRUE(model.isLineHidden(10));
    EXPECT_TRUE(model.isLineHidden(20));
    EXPECT_FALSE(model.isLineHidden(21));
}

TEST(FoldingModelTest, FoldedRegionContainingReturnsOutermostMatch) {
    FoldingModel model;
    model.setFoldableRegions({
        FoldRegion{.headerLine = 0, .endLineInclusive = 20, .folded = false},
        FoldRegion{.headerLine = 5, .endLineInclusive = 10, .folded = false},
    });
    model.toggleFold(0);
    model.toggleFold(5);

    const auto found = model.foldedRegionContaining(7);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->headerLine, 0U);  // outer, not the nested inner region
}

TEST(FoldingModelTest, FoldedRegionContainingIsNulloptWhenLineNotHidden) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    EXPECT_FALSE(model.foldedRegionContaining(12).has_value());
}

TEST(FoldingModelTest, RevealLineUnfoldsEveryEnclosingNestedFold) {
    FoldingModel model;
    model.setFoldableRegions({
        FoldRegion{.headerLine = 0, .endLineInclusive = 20, .folded = false},
        FoldRegion{.headerLine = 5, .endLineInclusive = 10, .folded = false},
    });
    model.toggleFold(0);
    model.toggleFold(5);
    ASSERT_TRUE(model.isLineHidden(7));

    const bool changed = model.revealLine(7);

    EXPECT_TRUE(changed);
    EXPECT_FALSE(model.isLineHidden(7));
    EXPECT_FALSE(model.regionAt(0)->folded);
    EXPECT_FALSE(model.regionAt(5)->folded);
}

TEST(FoldingModelTest, RevealLineOnAlreadyVisibleLineReturnsFalse) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    EXPECT_FALSE(model.revealLine(12));
}

TEST(FoldingModelTest, SetFoldableRegionsCarriesOverFoldedStateByMatchingHeaderLine) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    model.toggleFold(10);
    ASSERT_TRUE(model.regionAt(10)->folded);

    // Re-supplied region list still has a region at headerLine=10 (e.g. after
    // re-parsing the outline) - its folded state should be preserved.
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 18, .folded = false}});
    ASSERT_TRUE(model.regionAt(10).has_value());
    EXPECT_TRUE(model.regionAt(10)->folded);
    EXPECT_EQ(model.regionAt(10)->endLineInclusive, 18U);
}

TEST(FoldingModelTest, SetFoldableRegionsDropsFoldedStateWhenHeaderLineDisappears) {
    FoldingModel model;
    model.setFoldableRegions({FoldRegion{.headerLine = 10, .endLineInclusive = 15, .folded = false}});
    model.toggleFold(10);

    // The old region at headerLine=10 is gone from the new list entirely.
    model.setFoldableRegions({FoldRegion{.headerLine = 20, .endLineInclusive = 25, .folded = false}});
    EXPECT_FALSE(model.regionAt(10).has_value());
    EXPECT_FALSE(model.regionAt(20)->folded);
}

}  // namespace
