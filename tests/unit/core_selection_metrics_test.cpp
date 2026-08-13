#include "neomifes/core/selection_metrics.h"

#include <gtest/gtest.h>

#include <vector>

namespace neomifes::core {
namespace {

TEST(SelectionMetricsTest, NoSelectionReturnsZero) {
    SelectionModel model;
    model.moveAllTo(5);
    EXPECT_EQ(totalSelectedLength(model), 0U);
}

TEST(SelectionMetricsTest, SingleSelectionReturnsItsLength) {
    SelectionModel model;
    model.setCursors(std::vector<Cursor>{Cursor{.position = 10, .anchor = 3, .isPrimary = true}});
    EXPECT_EQ(totalSelectedLength(model), 7U);
}

TEST(SelectionMetricsTest, ReversedSelectionStillReturnsPositiveLength) {
    // position < anchor (selection extended backwards) - the metric must not
    // depend on which end is which.
    SelectionModel model;
    model.setCursors(std::vector<Cursor>{Cursor{.position = 3, .anchor = 10, .isPrimary = true}});
    EXPECT_EQ(totalSelectedLength(model), 7U);
}

TEST(SelectionMetricsTest, MultipleCursorsSumTheirSelections) {
    SelectionModel model;
    model.setCursors(std::vector<Cursor>{Cursor{.position = 5, .anchor = 2, .isPrimary = true},
                                         Cursor{.position = 20, .anchor = 15, .isPrimary = false}});
    EXPECT_EQ(totalSelectedLength(model), 8U);  // 3 + 5
}

TEST(SelectionMetricsTest, MixOfSelectedAndCollapsedCursorsOnlyCountsSelected) {
    SelectionModel model;
    model.setCursors(std::vector<Cursor>{Cursor{.position = 5, .anchor = 5, .isPrimary = true},
                                         Cursor{.position = 12, .anchor = 8, .isPrimary = false}});
    EXPECT_EQ(totalSelectedLength(model), 4U);
}

}  // namespace
}  // namespace neomifes::core
