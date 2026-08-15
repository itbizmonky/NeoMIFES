#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/core/autosave_index.h"

namespace fs = std::filesystem;

namespace {

using neomifes::core::AutosaveIndex;

fs::path tempJsonPath() {
    return fs::temp_directory_path() /
           (std::string("nmfs_autosave_index_") + std::to_string(std::rand()) + ".json");
}

void writeRaw(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

TEST(AutosaveIndexTest, LoadFromMissingFileIsEmpty) {
    const AutosaveIndex index = AutosaveIndex::loadFrom(tempJsonPath());
    EXPECT_TRUE(index.entries().empty());
}

TEST(AutosaveIndexTest, LoadFromMalformedJsonIsEmpty) {
    auto path = tempJsonPath();
    writeRaw(path, "{not valid json");
    const AutosaveIndex index = AutosaveIndex::loadFrom(path);
    EXPECT_TRUE(index.entries().empty());
    fs::remove(path);
}

TEST(AutosaveIndexTest, LoadFromWrongVersionIsEmpty) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 2, "entries": [{"hash": "abc", "path": "C:/a.txt"}]})");
    const AutosaveIndex index = AutosaveIndex::loadFrom(path);
    EXPECT_TRUE(index.entries().empty());
    fs::remove(path);
}

TEST(AutosaveIndexTest, LoadFromToleratesAStrayMalformedEntry) {
    auto path = tempJsonPath();
    writeRaw(path,
             R"({"version": 1, "entries": [{"hash": "abc", "path": "C:/a.txt"}, {"hash": "missing_path"}, )"
             R"({"hash": "def", "path": "C:/b.txt"}]})");
    const AutosaveIndex index = AutosaveIndex::loadFrom(path);
    ASSERT_EQ(index.entries().size(), 2u);
    EXPECT_EQ(index.entries()[0].hash, u"abc");
    EXPECT_EQ(index.entries()[0].originalPath, fs::path("C:/a.txt"));
    EXPECT_EQ(index.entries()[1].hash, u"def");
    EXPECT_EQ(index.entries()[1].originalPath, fs::path("C:/b.txt"));
    fs::remove(path);
}

TEST(AutosaveIndexTest, RecordAppendsNewEntry) {
    AutosaveIndex index;
    index.record(u"hash1", "C:/a.txt");
    ASSERT_EQ(index.entries().size(), 1u);
    EXPECT_EQ(index.entries()[0].hash, u"hash1");
    EXPECT_EQ(index.entries()[0].originalPath, fs::path("C:/a.txt"));
}

TEST(AutosaveIndexTest, RecordUpsertsExistingHashRatherThanDuplicating) {
    AutosaveIndex index;
    index.record(u"hash1", "C:/a.txt");
    index.record(u"hash1", "C:/a-renamed.txt");
    ASSERT_EQ(index.entries().size(), 1u);
    EXPECT_EQ(index.entries()[0].originalPath, fs::path("C:/a-renamed.txt"));
}

TEST(AutosaveIndexTest, RemoveDropsMatchingEntry) {
    AutosaveIndex index;
    index.record(u"hash1", "C:/a.txt");
    index.record(u"hash2", "C:/b.txt");
    index.remove(u"hash1");
    ASSERT_EQ(index.entries().size(), 1u);
    EXPECT_EQ(index.entries()[0].hash, u"hash2");
}

TEST(AutosaveIndexTest, RemoveOfUnknownHashIsANoOp) {
    AutosaveIndex index;
    index.record(u"hash1", "C:/a.txt");
    index.remove(u"does_not_exist");
    EXPECT_EQ(index.entries().size(), 1u);
}

TEST(AutosaveIndexTest, SaveThenLoadRoundTripsIncludingJapaneseText) {
    AutosaveIndex index;
    index.record(u"hash1", fs::path(L"C:/hello.txt"));
    index.record(u"hash2", fs::path(L"C:/\u65e5\u672c\u8a9e/\u30c6\u30b9\u30c8.txt"));  // 日本語/テスト.txt

    auto path = tempJsonPath();
    index.saveTo(path);

    const AutosaveIndex loaded = AutosaveIndex::loadFrom(path);
    ASSERT_EQ(loaded.entries().size(), 2u);
    EXPECT_EQ(loaded.entries()[0].hash, u"hash1");
    EXPECT_EQ(loaded.entries()[0].originalPath, fs::path(L"C:/hello.txt"));
    EXPECT_EQ(loaded.entries()[1].hash, u"hash2");
    EXPECT_EQ(loaded.entries()[1].originalPath, fs::path(L"C:/\u65e5\u672c\u8a9e/\u30c6\u30b9\u30c8.txt"));

    fs::remove(path);
}

TEST(AutosaveIndexTest, SaveToNonExistentDirectoryFailsSilently) {
    AutosaveIndex index;
    index.record(u"hash1", "C:/a.txt");
    const fs::path unwritablePath =
        fs::temp_directory_path() / "nmfs_this_directory_does_not_exist" / "index.json";
    EXPECT_NO_THROW(index.saveTo(unwritablePath));
    EXPECT_FALSE(fs::exists(unwritablePath));
}

}  // namespace
