#include <gtest/gtest.h>

#include <vector>

#include "neomifes/platform/process_metrics.h"

namespace {

using neomifes::platform::currentProcessMemory;

TEST(ProcessMetricsTest, WorkingSetIsNonZero) {
    const auto m = currentProcessMemory();
    EXPECT_GT(m.workingSetBytes, 0u);
    EXPECT_GT(m.privateBytes,    0u);
}

TEST(ProcessMetricsTest, WorkingSetGrowsAfterAllocation) {
    const auto before = currentProcessMemory();
    // 32MB heap allocation - large enough to force resident-set growth.
    std::vector<std::byte> big(32 * 1024 * 1024);
    for (std::size_t i = 0; i < big.size(); i += 4096) {
        big[i] = std::byte{0xAB};  // Touch each page so it becomes resident.
    }
    const auto after = currentProcessMemory();
    EXPECT_GE(after.workingSetBytes, before.workingSetBytes);
}

}  // namespace
