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

// -----------------------------------------------------------------------------
// Phase 2b3 (mmap + Lazy Decode): OriginalBuffer checkpoints every 64KiB of
// UTF-8 bytes (kCheckpointBytes) so view() can resume decoding without
// starting from byte 0 every time. These tests specifically exercise content
// that spans / straddles that boundary - the classic bug class this design
// has to get right (see docs/issues/lazy_decode_mmap.md).
// -----------------------------------------------------------------------------

TEST(FileLoaderTest, EmptyFileProducesEmptyDocument) {
    auto path = tempFileWith("");
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_EQ(r.byteLength, 0u);
    EXPECT_FALSE(r.hadBom);
    EXPECT_EQ(r.document->length(), 0u);
    EXPECT_EQ(r.document->toU16String(), u"");
    fs::remove(path);
}

TEST(FileLoaderTest, BomOnlyFileProducesEmptyDocument) {
    auto path = tempFileWith("\xEF\xBB\xBF");
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_TRUE(r.hadBom);
    EXPECT_EQ(r.document->length(), 0u);
    fs::remove(path);
}

TEST(FileLoaderTest, MultibyteCharacterStraddlingCheckpointBoundaryDecodesCorrectly) {
    // 65535 ASCII bytes place the next character's first byte at byte offset
    // 65535 - one byte before the 64KiB (65536) checkpoint mark, so the
    // 2-byte UTF-8 character that follows straddles the boundary a naive
    // fixed-size-chunk scheme would use. OriginalBuffer's checkpoints are
    // recorded only after a complete code point finishes, so this must
    // still decode correctly with no split character.
    std::string content(65535, 'a');
    content += "\xC3\xA9";  // U+00E9 (e with acute accent), 2 bytes UTF-8, 1 CU
    content += std::string(10, 'b');

    auto path = tempFileWith(content);
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);

    const std::u16string full = r.document->toU16String();
    ASSERT_EQ(full.size(), 65535u + 1u + 10u);
    EXPECT_EQ(full[65534], u'a');
    EXPECT_EQ(full[65535], static_cast<char16_t>(0x00E9));  // U+00E9, avoids relying on source-file encoding for a literal
    EXPECT_EQ(full[65536], u'b');
    EXPECT_EQ(r.document->lineCount(), 1u);  // no newlines in this content

    fs::remove(path);
}

TEST(FileLoaderTest, SplitAtCheckpointBoundaryPreservesContent) {
    // Same straddling layout as above, but this time force a PieceTable
    // split exactly at the boundary (CU offset 65536), which requires
    // PieceTable::ensureBoundary to call OriginalBuffer::view(0, 65536) -
    // decoding all the way across the checkpoint in one call - and then a
    // second full extract that walks both the split Original piece and the
    // newly inserted Add piece together.
    std::string content(65535, 'a');
    content += "\xC3\xA9";
    content += std::string(10, 'b');

    auto path = tempFileWith(content);
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);

    const std::u16string before = r.document->toU16String();
    ASSERT_EQ(before.size(), 65535u + 1u + 10u);

    r.document->insertText(65536, u"|");

    const std::u16string after = r.document->toU16String();
    ASSERT_EQ(after.size(), before.size() + 1);
    EXPECT_EQ(after.substr(0, 65535), before.substr(0, 65535));  // unchanged 'a' run
    EXPECT_EQ(after[65535], static_cast<char16_t>(0x00E9));      // U+00E9
    EXPECT_EQ(after[65536], u'|');
    EXPECT_EQ(after.substr(65537), before.substr(65536));  // unchanged 'b' run, shifted by 1

    fs::remove(path);
}

TEST(FileLoaderTest, ContentSpanningMultipleCheckpointsDecodesCorrectly) {
    // ~200KB of content spans 3+ checkpoints (kCheckpointBytes = 64KiB),
    // exercising the binary search over the checkpoint index rather than
    // only ever hitting checkpoint[0].
    std::string content;
    content.reserve(200 * 1024);
    for (int i = 0; i < 200 * 1024; ++i) {
        // A repeating but position-identifiable pattern (digits 0-9) so a
        // mismatch anywhere is easy to localize by its offset mod 10.
        content.push_back(static_cast<char>('0' + (i % 10)));
    }

    auto path = tempFileWith(content);
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);

    ASSERT_EQ(r.document->length(), content.size());
    const std::u16string full = r.document->toU16String();
    for (std::size_t i = 0; i < content.size(); i += 4093) {  // prime stride, spot-check
        ASSERT_EQ(static_cast<char>(full[i]), content[i]) << "mismatch at offset " << i;
    }

    fs::remove(path);
}

TEST(FileLoaderTest, NewlineCountPrecomputedWithoutFullDecode) {
    // lineCount()/newlineCount() must be answerable from the Piece's
    // precomputed count alone (Phase 2b3: PieceTable's constructor reads
    // OriginalBuffer::newlineCount(), never calling view() for the whole
    // file). This test doesn't prove "no decode happened" directly, but it
    // does confirm the count is correct immediately after construction,
    // before any explicit read of the content.
    std::string content = "line1\nline2\nline3";  // 2 newlines, 3 lines
    auto path = tempFileWith(content);
    auto result = loadUtf8File(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);

    EXPECT_EQ(r.document->lineCount(), 3u);
}

}  // namespace
