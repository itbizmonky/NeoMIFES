#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/core/recent_files.h"

namespace fs = std::filesystem;

namespace {

using neomifes::core::RecentFiles;

fs::path tempJsonPath() {
    return fs::temp_directory_path() /
           (std::string("nmfs_recent_files_") + std::to_string(std::rand()) + ".json");
}

void writeRaw(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

TEST(RecentFilesTest, LoadFromMissingFileIsEmpty) {
    const RecentFiles recent = RecentFiles::loadFrom(tempJsonPath());
    EXPECT_TRUE(recent.entries().empty());
}

TEST(RecentFilesTest, LoadFromMalformedJsonIsEmpty) {
    auto path = tempJsonPath();
    writeRaw(path, "{not valid json");
    const RecentFiles recent = RecentFiles::loadFrom(path);
    EXPECT_TRUE(recent.entries().empty());
    fs::remove(path);
}

TEST(RecentFilesTest, LoadFromWrongVersionIsEmpty) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 2, "entries": ["C:/a.txt"]})");
    const RecentFiles recent = RecentFiles::loadFrom(path);
    EXPECT_TRUE(recent.entries().empty());
    fs::remove(path);
}

TEST(RecentFilesTest, LoadFromToleratesAStrayNonStringEntry) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "entries": ["C:/a.txt", 42, "C:/b.txt"]})");
    const RecentFiles recent = RecentFiles::loadFrom(path);
    ASSERT_EQ(recent.entries().size(), 2u);
    EXPECT_EQ(recent.entries()[0], fs::path("C:/a.txt"));
    EXPECT_EQ(recent.entries()[1], fs::path("C:/b.txt"));
    fs::remove(path);
}

TEST(RecentFilesTest, RecordIsMruOrder) {
    RecentFiles recent;
    recent.record("C:/first.txt");
    recent.record("C:/second.txt");
    recent.record("C:/third.txt");
    ASSERT_EQ(recent.entries().size(), 3u);
    EXPECT_EQ(recent.entries()[0], fs::path("C:/third.txt"));
    EXPECT_EQ(recent.entries()[1], fs::path("C:/second.txt"));
    EXPECT_EQ(recent.entries()[2], fs::path("C:/first.txt"));
}

TEST(RecentFilesTest, RecordDedupesExistingEntryToFront) {
    RecentFiles recent;
    recent.record("C:/first.txt");
    recent.record("C:/second.txt");
    recent.record("C:/first.txt");  // re-opened - should move to front, not duplicate
    ASSERT_EQ(recent.entries().size(), 2u);
    EXPECT_EQ(recent.entries()[0], fs::path("C:/first.txt"));
    EXPECT_EQ(recent.entries()[1], fs::path("C:/second.txt"));
}

TEST(RecentFilesTest, RecordCapsAt20EntriesDroppingOldest) {
    RecentFiles recent;
    for (int i = 0; i < 25; ++i) {
        recent.record(fs::path("C:/file" + std::to_string(i) + ".txt"));
    }
    EXPECT_EQ(recent.entries().size(), 20u);
    // The most recently recorded entry survives.
    EXPECT_EQ(recent.entries().front(), fs::path("C:/file24.txt"));
}

TEST(RecentFilesTest, SaveThenLoadRoundTripsIncludingJapaneseText) {
    RecentFiles recent;
    recent.record(fs::path(L"C:/hello.txt"));
    recent.record(fs::path(L"C:/\u65e5\u672c\u8a9e/\u30c6\u30b9\u30c8.txt"));  // 日本語/テスト.txt

    auto path = tempJsonPath();
    recent.saveTo(path);

    const RecentFiles loaded = RecentFiles::loadFrom(path);
    ASSERT_EQ(loaded.entries().size(), 2u);
    EXPECT_EQ(loaded.entries()[0], fs::path(L"C:/\u65e5\u672c\u8a9e/\u30c6\u30b9\u30c8.txt"));
    EXPECT_EQ(loaded.entries()[1], fs::path(L"C:/hello.txt"));

    fs::remove(path);
}

TEST(RecentFilesTest, SaveToNonExistentDirectoryFailsSilently) {
    RecentFiles recent;
    recent.record("C:/hello.txt");
    const fs::path unwritablePath =
        fs::temp_directory_path() / "nmfs_this_directory_does_not_exist" / "recent.json";
    EXPECT_NO_THROW(recent.saveTo(unwritablePath));
    EXPECT_FALSE(fs::exists(unwritablePath));
}

}  // namespace
