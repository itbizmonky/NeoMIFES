#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"

namespace fs = std::filesystem;

namespace {

using neomifes::document::LoadError;
using neomifes::document::LoadResult;
using neomifes::document::loadUtf8File;

fs::path tempFileWith(const std::string& bytes) {
    fs::path p = fs::temp_directory_path()
               / (std::string("nmfs_loader_") + std::to_string(std::rand()) + ".txt");
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

TEST(FileLoaderTest, LoadsPlainAscii) {
    auto path = tempFileWith("hello");
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_FALSE(r.hadBom);
    EXPECT_EQ(r.byteLength, 5u);
    EXPECT_EQ(r.document->toU16String(), u"hello");
    fs::remove(path);
}

TEST(FileLoaderTest, StripsBom) {
    auto path = tempFileWith("\xEF\xBB\xBFhi");
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_TRUE(r.hadBom);
    EXPECT_EQ(r.document->toU16String(), u"hi");
    fs::remove(path);
}

TEST(FileLoaderTest, DecodesMultibyteUtf8) {
    // "あ" (U+3042) in UTF-8 is E3 81 82.
    auto path = tempFileWith("\xE3\x81\x82");
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_EQ(r.document->toU16String(), u"あ");
    fs::remove(path);
}

TEST(FileLoaderTest, DecodesSurrogatePair) {
    // U+1F600 in UTF-8 is F0 9F 98 80.
    auto path = tempFileWith("\xF0\x9F\x98\x80");
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_EQ(r.document->toU16String(), (std::u16string(u"\xD83D\xDE00")));
    fs::remove(path);
}

TEST(FileLoaderTest, RejectsMalformedUtf8) {
    // 0xC2 is a lead byte expecting a continuation - a single 0xC2 is invalid.
    auto path = tempFileWith("\xC2");
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::InvalidUtf8);
    fs::remove(path);
}

TEST(FileLoaderTest, ReturnsNotFound) {
    auto result = loadUtf8File("Z:\\this\\path\\does\\not\\exist.txt");
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::NotFound);
}

TEST(FileLoaderTest, EnforcesMaxBytes) {
    auto path = tempFileWith("abcdef");
    auto result = loadUtf8File(path, /*maxBytes=*/3);
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::TooLarge);
    fs::remove(path);
}

}  // namespace
