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
using neomifes::document::loadFile;
using neomifes::document::loadUtf8File;
using neomifes::encoding::Encoding;

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

// -----------------------------------------------------------------------------
// Phase 6d: loadFile() - auto-detected multi-encoding loading, on top of the
// generalized OriginalBuffer::openMemoryMapped()/fromU16String() paths.
// -----------------------------------------------------------------------------

TEST(LoadFileTest, DetectsUtf16LeBomAndDecodesCorrectly) {
    // BOM (FF FE) + "hi" as UTF-16LE (68 00 69 00). Explicit-length
    // std::string constructor: the literal contains embedded \x00 bytes,
    // which the implicit const char*->std::string (strlen-based) conversion
    // would truncate at.
    auto path = tempFileWith(std::string("\xFF\xFE\x68\x00\x69\x00", 6));
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_TRUE(r.hadBom);
    EXPECT_EQ(r.detectedEncoding, Encoding::Utf16LeBom);
    EXPECT_EQ(r.document->toU16String(), u"hi");
    fs::remove(path);
}

TEST(LoadFileTest, DetectsUtf16BeBomAndDecodesCorrectly) {
    // BOM (FE FF) + "hi" as UTF-16BE (00 68 00 69). Explicit length - see
    // DetectsUtf16LeBomAndDecodesCorrectly's comment on embedded \x00 bytes.
    auto path = tempFileWith(std::string("\xFE\xFF\x00\x68\x00\x69", 6));
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_TRUE(r.hadBom);
    EXPECT_EQ(r.detectedEncoding, Encoding::Utf16BeBom);
    EXPECT_EQ(r.document->toU16String(), u"hi");
    fs::remove(path);
}

TEST(LoadFileTest, DetectsUtf32LeBomAndDecodesCorrectly) {
    // BOM (FF FE 00 00) + "hi" as UTF-32LE (68 00 00 00 / 69 00 00 00).
    auto path = tempFileWith(std::string(
        "\xFF\xFE\x00\x00" "\x68\x00\x00\x00" "\x69\x00\x00\x00", 12));
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_TRUE(r.hadBom);
    EXPECT_EQ(r.detectedEncoding, Encoding::Utf32LeBom);
    EXPECT_EQ(r.document->toU16String(), u"hi");
    fs::remove(path);
}

TEST(LoadFileTest, DetectsUtf32BeBomAndDecodesCorrectly) {
    // BOM (00 00 FE FF) + "hi" as UTF-32BE (00 00 00 68 / 00 00 00 69).
    auto path = tempFileWith(std::string(
        "\x00\x00\xFE\xFF" "\x00\x00\x00\x68" "\x00\x00\x00\x69", 12));
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_TRUE(r.hadBom);
    EXPECT_EQ(r.detectedEncoding, Encoding::Utf32BeBom);
    EXPECT_EQ(r.document->toU16String(), u"hi");
    fs::remove(path);
}

TEST(LoadFileTest, DecodesSurrogatePairFromUtf16LeSource) {
    // BOM + U+1F600 as UTF-16LE surrogate pair (high D83D -> 3D D8, low
    // DE00 -> 00 DE), matching FileLoaderTest.DecodesSurrogatePair's
    // expected UTF-8 result for the same code point.
    auto path = tempFileWith(std::string("\xFF\xFE\x3D\xD8\x00\xDE", 6));
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_EQ(r.document->toU16String(), (std::u16string(u"\xD83D\xDE00")));
    fs::remove(path);
}

TEST(LoadFileTest, DecodesNonBmpFromUtf32LeSource) {
    // BOM + U+1F600 as a single UTF-32LE unit (00 F6 01 00) - decode()
    // expands this to the same UTF-16 surrogate pair as the UTF-16 source
    // case above, since this project's internal representation is always
    // UTF-16 CU regardless of source encoding.
    auto path = tempFileWith(std::string(
        "\xFF\xFE\x00\x00" "\x00\xF6\x01\x00", 8));
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_EQ(r.document->toU16String(), (std::u16string(u"\xD83D\xDE00")));
    fs::remove(path);
}

TEST(LoadFileTest, RejectsUnpairedHighSurrogateInUtf16Source) {
    // BOM + a lone high surrogate (3D D8) with no following low surrogate.
    auto path = tempFileWith("\xFF\xFE\x3D\xD8");
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::InvalidEncoding);
    fs::remove(path);
}

TEST(LoadFileTest, RejectsOddByteCountUtf16Source) {
    // BOM (2 bytes, valid) + 1 stray content byte - odd total content length.
    auto path = tempFileWith("\xFF\xFE\x68");
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::InvalidEncoding);
    fs::remove(path);
}

TEST(LoadFileTest, RejectsByteCountNotMultipleOf4Utf32Source) {
    // BOM (4 bytes, valid) + 3 stray content bytes.
    auto path = tempFileWith(std::string("\xFF\xFE\x00\x00\x68\x00\x00", 7));
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::InvalidEncoding);
    fs::remove(path);
}

TEST(LoadFileTest, DetectsShiftJisWithoutBom) {
    // Known Shift-JIS bytes for "亜" (Phase 6b1), decisively in Shift-JIS's
    // 0x81-0x9F lead-byte range (see encoding_encoding_test.cpp's
    // ShiftJisByteInDecisiveRangeIsDetectedAsShiftJis).
    auto path = tempFileWith("\x88\x9F");
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_FALSE(r.hadBom);
    EXPECT_EQ(r.detectedEncoding, Encoding::ShiftJis);
    EXPECT_EQ(r.document->toU16String(), u"亜");
    fs::remove(path);
}

TEST(LoadFileTest, DetectsEucJpWithoutBom) {
    // Decisive EUC-JP bytes (trail byte 0xFD exceeds Shift-JIS's max 0xFC
    // trail byte) - see encoding_encoding_test.cpp's
    // EucJpTrailByteOutsideShiftJisRangeIsDetectedAsEucJp.
    auto path = tempFileWith("\xA1\xFD");
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_FALSE(r.hadBom);
    EXPECT_EQ(r.detectedEncoding, Encoding::EucJp);
    EXPECT_EQ(r.document->length(), 1u);  // one double-byte EUC-JP character -> 1 CU
    fs::remove(path);
}

TEST(LoadFileTest, PlainAsciiIsDetectedAsUtf8) {
    auto path = tempFileWith("hello");
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_FALSE(r.hadBom);
    EXPECT_EQ(r.detectedEncoding, Encoding::Utf8);
    EXPECT_EQ(r.document->toU16String(), u"hello");
    fs::remove(path);
}

TEST(LoadFileTest, UnrecognizedBytesFallBackToUtf8ThenFailValidation) {
    // 0xFF alone matches no BOM, is outside Shift-JIS's and EUC-JP's valid
    // lead-byte ranges, and is not a valid UTF-8 lead byte either -
    // detectEncoding() returns nullopt, loadFile() falls back to its Utf8
    // default (matching loadUtf8File()'s existing implicit assumption), and
    // that fallback decode correctly fails too rather than silently
    // accepting garbage.
    auto path = tempFileWith("\xFF");
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::InvalidEncoding);
    fs::remove(path);
}

TEST(LoadFileTest, ReturnsNotFound) {
    auto result = loadFile("Z:\\this\\path\\does\\not\\exist.txt");
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::NotFound);
}

TEST(LoadFileTest, EmptyFileProducesEmptyDocument) {
    auto path = tempFileWith("");
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    EXPECT_EQ(r.byteLength, 0u);
    EXPECT_FALSE(r.hadBom);
    EXPECT_EQ(r.document->length(), 0u);
    fs::remove(path);
}

TEST(LoadFileTest, EnforcesMaxBytes) {
    auto path = tempFileWith("abcdef");
    auto result = loadFile(path, /*maxBytes=*/3);
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::TooLarge);
    fs::remove(path);
}

TEST(LoadFileTest, Utf16SourceMultibyteContentSpansLargeRange) {
    // ~100,000 UTF-16 code units (200,000 bytes), well past kDetectionHeadBytes
    // (64KiB) - exercises the O(1) byte<->CU math viewMemoryMappedUtf16()
    // relies on (no checkpoint index) across a range no single detection-head
    // read would cover.
    std::string content = "\xFF\xFE";  // UTF-16LE BOM
    content.reserve(2 + (100000 * 2));
    for (int i = 0; i < 100000; ++i) {
        content.push_back(static_cast<char>('0' + (i % 10)));
        content.push_back('\0');
    }

    auto path = tempFileWith(content);
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);
    ASSERT_EQ(r.document->length(), 100000u);

    const std::u16string full = r.document->toU16String();
    for (std::size_t i = 0; i < 100000; i += 4093) {  // prime stride, spot-check
        ASSERT_EQ(static_cast<char>(full[i]), static_cast<char>('0' + (i % 10)))
            << "mismatch at offset " << i;
    }

    fs::remove(path);
}

TEST(LoadFileTest, Utf16SourceSplitAtArbitraryOffsetPreservesContent) {
    // Same large-range UTF-16LE content as
    // Utf16SourceMultibyteContentSpansLargeRange, but forces a PieceTable
    // split partway through - exercises OriginalBuffer::view() being asked
    // for a UTF-16-sourced range that doesn't start at offset 0.
    std::string content = "\xFF\xFE";  // UTF-16LE BOM
    content.reserve(2 + (100000 * 2));
    for (int i = 0; i < 100000; ++i) {
        content.push_back(static_cast<char>('0' + (i % 10)));
        content.push_back('\0');
    }

    auto path = tempFileWith(content);
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);

    r.document->insertText(50000, u"|");
    const std::u16string full = r.document->toU16String();
    ASSERT_EQ(full.size(), 100001u);
    EXPECT_EQ(full[49999], u'9');  // unchanged, just before the insertion
    EXPECT_EQ(full[50000], u'|');
    EXPECT_EQ(full[50001], u'0');  // shifted by 1, was at 50000 before insertion

    fs::remove(path);
}

TEST(LoadFileTest, Utf32SourceContentSpanningMultipleCheckpointsDecodesCorrectly) {
    // ~40,000 UTF-32 units (160,000 bytes) spans 2+ of OriginalBuffer's
    // 64KiB checkpoints, exercising scanUtf32()/viewMemoryMappedUtf32()'s
    // checkpoint index the same way the UTF-8
    // ContentSpanningMultipleCheckpointsDecodesCorrectly test does.
    // Explicit length: the literal's embedded \x00 bytes would truncate an
    // implicit const char*->std::string (strlen-based) conversion.
    std::string content(std::string("\xFF\xFE\x00\x00", 4));  // UTF-32LE BOM
    content.reserve(4 + (40000 * 4));
    for (int i = 0; i < 40000; ++i) {
        content.push_back(static_cast<char>('0' + (i % 10)));
        content.push_back('\0');
        content.push_back('\0');
        content.push_back('\0');
    }

    auto path = tempFileWith(content);
    auto result = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(result));
    auto& r = std::get<LoadResult>(result);

    ASSERT_EQ(r.document->length(), 40000u);
    const std::u16string full = r.document->toU16String();
    for (std::size_t i = 0; i < 40000; i += 4093) {  // prime stride, spot-check
        ASSERT_EQ(static_cast<char>(full[i]), static_cast<char>('0' + (i % 10)))
            << "mismatch at offset " << i;
    }

    fs::remove(path);
}

}  // namespace
