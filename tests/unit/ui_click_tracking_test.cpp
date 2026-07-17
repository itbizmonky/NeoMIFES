#include <gtest/gtest.h>

#include "neomifes/ui/click_tracking.h"

namespace {

using neomifes::ui::ClickPoint;
using neomifes::ui::ClickTrackerState;
using neomifes::ui::nextClickState;

constexpr std::uint32_t kThresholdMs = 500;
constexpr std::int32_t  kMaxDx       = 4;
constexpr std::int32_t  kMaxDy       = 4;

TEST(ClickTrackingTest, FirstClickIsCountOne) {
    const ClickTrackerState previous{};  // count == 0, no prior click
    const auto next =
        nextClickState(previous, ClickPoint{.x = 10, .y = 10}, 1000, kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(next.count, 1);
}

TEST(ClickTrackingTest, SecondClickWithinTimeAndDistanceIsCountTwo) {
    const ClickTrackerState first{.lastPos = {.x = 10, .y = 10}, .lastTimeMs = 1000, .count = 1};
    const auto second =
        nextClickState(first, ClickPoint{.x = 11, .y = 9}, 1200, kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(second.count, 2);
}

TEST(ClickTrackingTest, ThirdClickWithinTimeAndDistanceIsCountThree) {
    const ClickTrackerState first{.lastPos = {.x = 10, .y = 10}, .lastTimeMs = 1000, .count = 1};
    const auto second =
        nextClickState(first, ClickPoint{.x = 10, .y = 10}, 1200, kThresholdMs, kMaxDx, kMaxDy);
    ASSERT_EQ(second.count, 2);
    const auto third =
        nextClickState(second, ClickPoint{.x = 10, .y = 10}, 1400, kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(third.count, 3);
}

TEST(ClickTrackingTest, FourthRapidClickStaysAtThree) {
    ClickTrackerState state{.lastPos = {.x = 10, .y = 10}, .lastTimeMs = 1000, .count = 3};
    state = nextClickState(state, ClickPoint{.x = 10, .y = 10}, 1200, kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(state.count, 3);
}

TEST(ClickTrackingTest, ClickAfterTimeoutResetsToOne) {
    const ClickTrackerState first{.lastPos = {.x = 10, .y = 10}, .lastTimeMs = 1000, .count = 2};
    const auto next = nextClickState(first, ClickPoint{.x = 10, .y = 10},
                                     1000 + kThresholdMs + 1, kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(next.count, 1);
}

TEST(ClickTrackingTest, ClickWithinTimeoutBoundaryStillCounts) {
    const ClickTrackerState first{.lastPos = {.x = 10, .y = 10}, .lastTimeMs = 1000, .count = 1};
    const auto next = nextClickState(first, ClickPoint{.x = 10, .y = 10}, 1000 + kThresholdMs,
                                     kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(next.count, 2);  // exactly at the threshold still counts (<=)
}

TEST(ClickTrackingTest, ClickTooFarAwayResetsToOne) {
    const ClickTrackerState first{.lastPos = {.x = 10, .y = 10}, .lastTimeMs = 1000, .count = 2};
    const auto next = nextClickState(first, ClickPoint{.x = 10 + kMaxDx + 1, .y = 10}, 1100,
                                     kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(next.count, 1);
}

TEST(ClickTrackingTest, ClickJustWithinDistanceBoundaryStillCounts) {
    const ClickTrackerState first{.lastPos = {.x = 10, .y = 10}, .lastTimeMs = 1000, .count = 1};
    const auto next = nextClickState(first, ClickPoint{.x = 10 + kMaxDx, .y = 10 - kMaxDy}, 1100,
                                     kThresholdMs, kMaxDx, kMaxDy);
    EXPECT_EQ(next.count, 2);
}

}  // namespace
