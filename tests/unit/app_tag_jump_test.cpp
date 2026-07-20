#include <gtest/gtest.h>

#include <filesystem>

#include "neomifes/app/tag_jump.h"

namespace {

using neomifes::app::resolveTagJumpPath;

TEST(ResolveTagJumpPathTest, AbsoluteDrivePathPassesThrough) {
    const std::filesystem::path baseDir(L"D:\\build");
    const auto resolved = resolveTagJumpPath(u"C:\\src\\foo.cpp", baseDir);
    EXPECT_EQ(resolved, std::filesystem::path(L"C:\\src\\foo.cpp"));
}

TEST(ResolveTagJumpPathTest, UncPathPassesThrough) {
    const std::filesystem::path baseDir(L"D:\\build");
    const auto resolved = resolveTagJumpPath(u"\\\\server\\share\\foo.cpp", baseDir);
    EXPECT_EQ(resolved, std::filesystem::path(L"\\\\server\\share\\foo.cpp"));
}

TEST(ResolveTagJumpPathTest, RelativePathJoinedWithBaseDir) {
    const std::filesystem::path baseDir(L"D:\\build");
    const auto resolved = resolveTagJumpPath(u"foo.cpp", baseDir);
    EXPECT_EQ(resolved, baseDir / L"foo.cpp");
}

TEST(ResolveTagJumpPathTest, RelativeSubdirWithBackslashResolvesCorrectly) {
    const std::filesystem::path baseDir(L"D:\\build");
    const auto resolved = resolveTagJumpPath(u"src\\foo.cpp", baseDir);
    EXPECT_EQ(resolved, baseDir / L"src\\foo.cpp");
}

TEST(ResolveTagJumpPathTest, RelativeSubdirWithForwardSlashResolvesCorrectly) {
    const std::filesystem::path baseDir(L"D:\\build");
    const auto resolved = resolveTagJumpPath(u"src/foo.cpp", baseDir);
    EXPECT_EQ(resolved, baseDir / L"src/foo.cpp");
}

}  // namespace
