#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/platform/file_mapping.h"

namespace fs = std::filesystem;

namespace {

using neomifes::platform::FileMapping;
using neomifes::platform::FileMappingError;

fs::path tempFileWith(const std::string& bytes) {
    fs::path p = fs::temp_directory_path()
               / (std::string("nmfs_filemap_") + std::to_string(std::rand()) + ".bin");
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

TEST(FileMappingTest, OpensAndReadsPlainContent) {
    auto path = tempFileWith("hello world");
    auto result = FileMapping::open(path);
    ASSERT_TRUE(std::holds_alternative<FileMapping>(result));
    auto& mapping = std::get<FileMapping>(result);

    EXPECT_EQ(mapping.size(), 11u);
    auto data = mapping.data();
    ASSERT_EQ(data.size(), 11u);
    EXPECT_EQ(static_cast<char>(data[0]), 'h');
    EXPECT_EQ(static_cast<char>(data[10]), 'd');

    fs::remove(path);
}

TEST(FileMappingTest, ReturnsNotFoundForMissingFile) {
    auto result = FileMapping::open("Z:\\this\\path\\does\\not\\exist_nmfs.bin");
    ASSERT_TRUE(std::holds_alternative<FileMappingError>(result));
    EXPECT_EQ(std::get<FileMappingError>(result), FileMappingError::NotFound);
}

TEST(FileMappingTest, ReturnsEmptyFileForZeroByteFile) {
    auto path = tempFileWith("");
    auto result = FileMapping::open(path);
    ASSERT_TRUE(std::holds_alternative<FileMappingError>(result));
    EXPECT_EQ(std::get<FileMappingError>(result), FileMappingError::EmptyFile);
    fs::remove(path);
}

TEST(FileMappingTest, MoveConstructorTransfersOwnership) {
    auto path = tempFileWith("movable content");
    auto result = FileMapping::open(path);
    ASSERT_TRUE(std::holds_alternative<FileMapping>(result));

    FileMapping a = std::move(std::get<FileMapping>(result));
    EXPECT_EQ(a.size(), 15u);

    FileMapping b = std::move(a);
    EXPECT_EQ(b.size(), 15u);
    EXPECT_EQ(a.size(), 0u);          // moved-from: size() ties validity to m_view, not stale m_size
    EXPECT_TRUE(a.data().empty());    // moved-from: no dangling view

    auto data = b.data();
    ASSERT_EQ(data.size(), 15u);
    EXPECT_EQ(static_cast<char>(data[0]), 'm');

    fs::remove(path);
}

TEST(FileMappingTest, DataSpanMatchesFileBytesExactly) {
    // A content that includes non-ASCII / high bytes, to make sure the
    // mapping is treated as raw bytes with no encoding assumptions.
    std::string raw;
    for (int i = 0; i < 256; ++i) {
        raw.push_back(static_cast<char>(static_cast<unsigned char>(i)));
    }
    auto path = tempFileWith(raw);
    auto result = FileMapping::open(path);
    ASSERT_TRUE(std::holds_alternative<FileMapping>(result));
    auto& mapping = std::get<FileMapping>(result);

    ASSERT_EQ(mapping.size(), 256u);
    auto data = mapping.data();
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(static_cast<unsigned char>(data[static_cast<std::size_t>(i)]),
                  static_cast<unsigned char>(i))
            << "byte index " << i;
    }
    fs::remove(path);
}

}  // namespace
