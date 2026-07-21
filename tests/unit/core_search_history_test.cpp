#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/core/search_history.h"

namespace fs = std::filesystem;

namespace {

using neomifes::core::SearchHistory;

fs::path tempJsonPath() {
    return fs::temp_directory_path()
         / (std::string("nmfs_search_history_") + std::to_string(std::rand()) + ".json");
}

void writeRaw(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

TEST(SearchHistoryTest, LoadFromMissingFileIsEmpty) {
    const SearchHistory history = SearchHistory::loadFrom(tempJsonPath());
    EXPECT_TRUE(history.entries().empty());
}

TEST(SearchHistoryTest, LoadFromMalformedJsonIsEmpty) {
    auto path = tempJsonPath();
    writeRaw(path, "{not valid json");
    const SearchHistory history = SearchHistory::loadFrom(path);
    EXPECT_TRUE(history.entries().empty());
    fs::remove(path);
}

TEST(SearchHistoryTest, LoadFromWrongVersionIsEmpty) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 2, "entries": ["hello"]})");
    const SearchHistory history = SearchHistory::loadFrom(path);
    EXPECT_TRUE(history.entries().empty());
    fs::remove(path);
}

TEST(SearchHistoryTest, LoadFromMissingEntriesFieldIsEmpty) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1})");
    const SearchHistory history = SearchHistory::loadFrom(path);
    EXPECT_TRUE(history.entries().empty());
    fs::remove(path);
}

TEST(SearchHistoryTest, LoadFromToleratesAStrayNonStringEntry) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "entries": ["a", 42, "b"]})");
    const SearchHistory history = SearchHistory::loadFrom(path);
    ASSERT_EQ(history.entries().size(), 2u);
    EXPECT_EQ(history.entries()[0], u"a");
    EXPECT_EQ(history.entries()[1], u"b");
    fs::remove(path);
}

TEST(SearchHistoryTest, RecordIsMruOrder) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    history.record(u"third");
    ASSERT_EQ(history.entries().size(), 3u);
    EXPECT_EQ(history.entries()[0], u"third");
    EXPECT_EQ(history.entries()[1], u"second");
    EXPECT_EQ(history.entries()[2], u"first");
}

TEST(SearchHistoryTest, RecordDedupesExistingEntryToFront) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    history.record(u"first");  // re-search - should move to front, not duplicate
    ASSERT_EQ(history.entries().size(), 2u);
    EXPECT_EQ(history.entries()[0], u"first");
    EXPECT_EQ(history.entries()[1], u"second");
}

TEST(SearchHistoryTest, RecordIgnoresEmptyQuery) {
    SearchHistory history;
    history.record(u"");
    EXPECT_TRUE(history.entries().empty());
}

TEST(SearchHistoryTest, RecordCapsAt50EntriesDroppingOldest) {
    SearchHistory history;
    for (int i = 0; i < 55; ++i) {
        history.record(std::u16string(u"query") + std::u16string(1, static_cast<char16_t>(u'0' + (i % 10))) +
                       std::u16string(1, static_cast<char16_t>(u'a' + (i / 10))));
    }
    EXPECT_EQ(history.entries().size(), 50u);
}

TEST(SearchHistoryTest, OlderReturnsNextOlderEntryWhenCurrentTextMatches) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    history.record(u"third");
    // entries() = [third, second, first] (MRU order)
    const auto older = history.older(u"third");
    ASSERT_TRUE(older.has_value());
    EXPECT_EQ(*older, u"second");
}

TEST(SearchHistoryTest, OlderClampsAtOldestEntry) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    EXPECT_FALSE(history.older(u"first").has_value());  // "first" is already the oldest
}

TEST(SearchHistoryTest, OlderStartsAtMostRecentWhenCurrentTextDoesNotMatch) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    const auto older = history.older(u"something the user is mid-typing");
    ASSERT_TRUE(older.has_value());
    EXPECT_EQ(*older, u"second");  // most recent
}

TEST(SearchHistoryTest, OlderOnEmptyHistoryReturnsNullopt) {
    const SearchHistory history;
    EXPECT_FALSE(history.older(u"anything").has_value());
    EXPECT_FALSE(history.older(u"").has_value());
}

TEST(SearchHistoryTest, NewerReturnsNextNewerEntryWhenCurrentTextMatches) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    history.record(u"third");
    // entries() = [third, second, first]
    const auto newer = history.newer(u"first");
    ASSERT_TRUE(newer.has_value());
    EXPECT_EQ(*newer, u"second");
}

TEST(SearchHistoryTest, NewerAtMostRecentEntryReturnsNullopt) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    EXPECT_FALSE(history.newer(u"second").has_value());  // "second" is already the newest
}

TEST(SearchHistoryTest, NewerWhenCurrentTextDoesNotMatchReturnsNullopt) {
    SearchHistory history;
    history.record(u"first");
    history.record(u"second");
    EXPECT_FALSE(history.newer(u"something the user is mid-typing").has_value());
}

TEST(SearchHistoryTest, NewerOnEmptyHistoryReturnsNullopt) {
    const SearchHistory history;
    EXPECT_FALSE(history.newer(u"anything").has_value());
}

TEST(SearchHistoryTest, SaveThenLoadRoundTripsIncludingJapaneseText) {
    SearchHistory history;
    history.record(u"hello");
    history.record(u"こんにちは世界");
    history.record(u"TODO");

    auto path = tempJsonPath();
    history.saveTo(path);

    const SearchHistory loaded = SearchHistory::loadFrom(path);
    ASSERT_EQ(loaded.entries().size(), 3u);
    EXPECT_EQ(loaded.entries()[0], u"TODO");
    EXPECT_EQ(loaded.entries()[1], u"こんにちは世界");
    EXPECT_EQ(loaded.entries()[2], u"hello");

    fs::remove(path);
}

TEST(SearchHistoryTest, SaveToNonExistentDirectoryFailsSilently) {
    SearchHistory history;
    history.record(u"hello");
    const fs::path unwritablePath =
        fs::temp_directory_path() / "nmfs_this_directory_does_not_exist" / "search_history.json";
    // Should not throw, crash, or otherwise propagate the failure - saveTo()
    // is documented best-effort.
    EXPECT_NO_THROW(history.saveTo(unwritablePath));
    EXPECT_FALSE(fs::exists(unwritablePath));
}

}  // namespace
