#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "neomifes/platform/app_data_dir.h"

namespace fs = std::filesystem;

namespace {

using neomifes::platform::resolveAppDataDir;

TEST(AppDataDirTest, ResolvesToAnExistingDirectory) {
    const auto result = resolveAppDataDir();
    ASSERT_TRUE(result.has_value());
    const auto& dir = *result;
    EXPECT_TRUE(fs::is_directory(dir));
    EXPECT_EQ(dir.filename(), L"NeoMIFES");
}

TEST(AppDataDirTest, ResolvedDirectoryIsWritable) {
    const auto dir = resolveAppDataDir();
    ASSERT_TRUE(dir.has_value());

    const auto probePath = *dir / L"nmfs_app_data_dir_write_probe.tmp";
    {
        std::ofstream out(probePath, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "probe";
    }
    EXPECT_TRUE(fs::exists(probePath));
    fs::remove(probePath);
}

TEST(AppDataDirTest, RepeatedCallsResolveToTheSamePath) {
    const auto first  = resolveAppDataDir();
    const auto second = resolveAppDataDir();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
}

}  // namespace
