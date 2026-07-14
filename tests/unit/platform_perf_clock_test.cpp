#include <gtest/gtest.h>

#include <thread>

#include "neomifes/platform/perf_clock.h"

namespace {

using neomifes::platform::PerfClock;

TEST(PerfClockTest, NowIsMonotonic) {
    const auto t1 = PerfClock::now();
    const auto t2 = PerfClock::now();
    EXPECT_LE(t1, t2);
}

TEST(PerfClockTest, NanosSinceStartAdvances) {
    PerfClock::markProcessStart();
    const auto a = PerfClock::nanosSinceProcessStart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto b = PerfClock::nanosSinceProcessStart();
    EXPECT_GT(b, a);
    // At least ~1ms has passed - be conservative for CI jitter.
    EXPECT_GT(b - a, 500'000);  // 0.5 ms
}

TEST(PerfClockTest, ReMarkResetsOrigin) {
    PerfClock::markProcessStart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto before = PerfClock::nanosSinceProcessStart();
    PerfClock::markProcessStart();
    const auto after = PerfClock::nanosSinceProcessStart();
    EXPECT_LT(after, before);
}

}  // namespace
