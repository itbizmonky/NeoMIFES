#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/logmode/log_pattern_file.h"

namespace fs = std::filesystem;

namespace {

using neomifes::logmode::loadLogPatternRuleFromFile;
using neomifes::logmode::loadUserLogPatternsFromDirectory;

fs::path tempJsonPath(std::string_view suffix = "") {
    return fs::temp_directory_path() /
           (std::string("nmfs_logpattern_") + std::to_string(std::rand()) + std::string(suffix) + ".json");
}

fs::path tempDirPath() {
    return fs::temp_directory_path() / (std::string("nmfs_logpatterns_") + std::to_string(std::rand()));
}

void writeRaw(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

constexpr std::string_view kValidRule =
    R"({"version": 1, "id": "my_app", "displayName": "My App", )"
    R"("pattern": "^(?P<timestamp>\\d{4}) (?P<level>\\w+) (?P<message>.*)$", )"
    R"("timestampFormat": "%Y"})";

TEST(LogPatternFileTest, LoadFromValidFileRoundTripsEveryField) {
    const auto path = tempJsonPath();
    writeRaw(path, kValidRule);
    const auto rule = loadLogPatternRuleFromFile(path);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->id, u"my_app");
    EXPECT_EQ(rule->displayName, u"My App");
    EXPECT_EQ(rule->pattern, u"^(?P<timestamp>\\d{4}) (?P<level>\\w+) (?P<message>.*)$");
    EXPECT_EQ(rule->timestampFormat, u"%Y");
    fs::remove(path);
}

TEST(LogPatternFileTest, LoadFromMissingFileReturnsNullopt) {
    EXPECT_FALSE(loadLogPatternRuleFromFile(tempJsonPath()).has_value());
}

TEST(LogPatternFileTest, LoadFromMalformedJsonReturnsNullopt) {
    const auto path = tempJsonPath();
    writeRaw(path, "{not valid json");
    EXPECT_FALSE(loadLogPatternRuleFromFile(path).has_value());
    fs::remove(path);
}

TEST(LogPatternFileTest, LoadFromMissingRequiredFieldReturnsNullopt) {
    const auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "id": "x", "displayName": "X"})");  // no "pattern"
    EXPECT_FALSE(loadLogPatternRuleFromFile(path).has_value());
    fs::remove(path);
}

TEST(LogPatternFileTest, LoadFromWrongVersionReturnsNullopt) {
    const auto path = tempJsonPath();
    writeRaw(path, R"({"version": 2, "id": "x", "displayName": "X", "pattern": ".*"})");
    EXPECT_FALSE(loadLogPatternRuleFromFile(path).has_value());
    fs::remove(path);
}

TEST(LogPatternFileTest, LoadFromInvalidRe2PatternReturnsNullopt) {
    const auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "id": "x", "displayName": "X", "pattern": "(unbalanced"})");
    EXPECT_FALSE(loadLogPatternRuleFromFile(path).has_value());
    fs::remove(path);
}

TEST(LogPatternFileTest, LoadFromOmittedTimestampFormatDefaultsToEmpty) {
    const auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "id": "x", "displayName": "X", "pattern": ".*"})");
    const auto rule = loadLogPatternRuleFromFile(path);
    ASSERT_TRUE(rule.has_value());
    EXPECT_TRUE(rule->timestampFormat.empty());
    fs::remove(path);
}

TEST(LogPatternFileDirectoryTest, ScansAllValidFilesInSortedOrder) {
    const auto dir = tempDirPath();
    fs::create_directories(dir);
    writeRaw(dir / "a.json", R"({"version": 1, "id": "first", "displayName": "First", "pattern": ".*"})");
    writeRaw(dir / "b.json", R"({"version": 1, "id": "second", "displayName": "Second", "pattern": ".*"})");
    const auto rules = loadUserLogPatternsFromDirectory(dir);
    ASSERT_EQ(rules.size(), 2U);
    EXPECT_EQ(rules[0].id, u"first");
    EXPECT_EQ(rules[1].id, u"second");
    fs::remove_all(dir);
}

TEST(LogPatternFileDirectoryTest, SkipsMalformedFilesButKeepsValidOnes) {
    const auto dir = tempDirPath();
    fs::create_directories(dir);
    writeRaw(dir / "good.json", R"({"version": 1, "id": "good", "displayName": "Good", "pattern": ".*"})");
    writeRaw(dir / "bad.json", "{not valid json");
    const auto rules = loadUserLogPatternsFromDirectory(dir);
    ASSERT_EQ(rules.size(), 1U);
    EXPECT_EQ(rules[0].id, u"good");
    fs::remove_all(dir);
}

TEST(LogPatternFileDirectoryTest, EarliestFilenameWinsOnIdCollision) {
    const auto dir = tempDirPath();
    fs::create_directories(dir);
    writeRaw(dir / "a.json", R"({"version": 1, "id": "dup", "displayName": "First", "pattern": ".*"})");
    writeRaw(dir / "b.json", R"({"version": 1, "id": "dup", "displayName": "Second", "pattern": ".*"})");
    const auto rules = loadUserLogPatternsFromDirectory(dir);
    ASSERT_EQ(rules.size(), 1U);
    EXPECT_EQ(rules[0].displayName, u"First");
    fs::remove_all(dir);
}

TEST(LogPatternFileDirectoryTest, NonExistentDirectoryReturnsEmptyVector) {
    EXPECT_TRUE(loadUserLogPatternsFromDirectory(tempDirPath()).empty());
}

TEST(LogPatternFileDirectoryTest, IgnoresNonJsonFiles) {
    const auto dir = tempDirPath();
    fs::create_directories(dir);
    writeRaw(dir / "readme.txt", "not a pattern file");
    writeRaw(dir / "pattern.json", R"({"version": 1, "id": "only", "displayName": "Only", "pattern": ".*"})");
    const auto rules = loadUserLogPatternsFromDirectory(dir);
    ASSERT_EQ(rules.size(), 1U);
    EXPECT_EQ(rules[0].id, u"only");
    fs::remove_all(dir);
}

}  // namespace
