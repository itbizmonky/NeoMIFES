// Verifies toUtf8WithOffsets() produces standard UTF-8 bytes and that
// byteToUtf16 correctly maps every byte (including the one-past-the-end
// sentinel) back to the originating UTF-16 code-unit offset, across ASCII,
// BMP CJK, and surrogate-pair (astral) characters.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "neomifes/util/utf8_convert.h"

namespace {

using neomifes::util::toUtf8WithOffsets;
using neomifes::util::Utf8Conversion;

TEST(Utf8ConvertTest, EmptyInputProducesEmptyOutputWithSentinelOnly) {
    const Utf8Conversion conv = toUtf8WithOffsets(u"");
    EXPECT_TRUE(conv.utf8.empty());
    ASSERT_EQ(conv.byteToUtf16.size(), 1U);
    EXPECT_EQ(conv.byteToUtf16[0], 0U);
}

TEST(Utf8ConvertTest, AsciiRoundTripsOneByteAndOffsetPerChar) {
    const Utf8Conversion conv = toUtf8WithOffsets(u"abc");
    EXPECT_EQ(conv.utf8, "abc");
    ASSERT_EQ(conv.byteToUtf16.size(), 4U);  // 3 bytes + sentinel
    EXPECT_EQ(conv.byteToUtf16[0], 0U);
    EXPECT_EQ(conv.byteToUtf16[1], 1U);
    EXPECT_EQ(conv.byteToUtf16[2], 2U);
    EXPECT_EQ(conv.byteToUtf16[3], 3U);  // one-past-end sentinel = UTF-16 length
}

TEST(Utf8ConvertTest, BmpCjkCharacterEncodesToThreeUtf8BytesAllMappingToSameOffset) {
    // U+65E5 ("日") -> UTF-8 E6 97 A5.
    const Utf8Conversion conv = toUtf8WithOffsets(u"日");
    ASSERT_EQ(conv.utf8.size(), 3U);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[0]), 0xE6);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[1]), 0x97);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[2]), 0xA5);
    ASSERT_EQ(conv.byteToUtf16.size(), 4U);
    EXPECT_EQ(conv.byteToUtf16[0], 0U);
    EXPECT_EQ(conv.byteToUtf16[1], 0U);
    EXPECT_EQ(conv.byteToUtf16[2], 0U);
    EXPECT_EQ(conv.byteToUtf16[3], 1U);  // sentinel: 1 UTF-16 code unit total
}

TEST(Utf8ConvertTest, MixedAsciiAndCjkOffsetsPointToCorrectUtf16Index) {
    // "a" + "日" + "b": UTF-16 offsets 0, 1, 2 respectively.
    const Utf8Conversion conv = toUtf8WithOffsets(u"a日b");
    ASSERT_EQ(conv.utf8.size(), 5U);  // 1 + 3 + 1
    EXPECT_EQ(conv.byteToUtf16[0], 0U);  // 'a'
    EXPECT_EQ(conv.byteToUtf16[1], 1U);  // first byte of 日
    EXPECT_EQ(conv.byteToUtf16[2], 1U);
    EXPECT_EQ(conv.byteToUtf16[3], 1U);
    EXPECT_EQ(conv.byteToUtf16[4], 2U);  // 'b'
    EXPECT_EQ(conv.byteToUtf16[5], 3U);  // sentinel
}

TEST(Utf8ConvertTest, SurrogatePairEncodesToFourUtf8BytesAndConsumesTwoUtf16Units) {
    // U+1F600 GRINNING FACE = surrogate pair D83D DE00 -> UTF-8 F0 9F 98 80.
    const std::u16string src(u"\xD83D\xDE00");
    const Utf8Conversion conv = toUtf8WithOffsets(src);
    ASSERT_EQ(conv.utf8.size(), 4U);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[0]), 0xF0);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[1]), 0x9F);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[2]), 0x98);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[3]), 0x80);
    ASSERT_EQ(conv.byteToUtf16.size(), 5U);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(conv.byteToUtf16[i], 0U) << "byte " << i;  // whole pair starts at UTF-16 index 0
    }
    EXPECT_EQ(conv.byteToUtf16[4], 2U);  // sentinel: 2 UTF-16 code units consumed
}

TEST(Utf8ConvertTest, LoneHighSurrogateReplacedWithReplacementCharacter) {
    const std::u16string src(u"\xD83D");  // high surrogate with nothing following
    const Utf8Conversion conv = toUtf8WithOffsets(src);
    // U+FFFD -> UTF-8 EF BF BD.
    ASSERT_EQ(conv.utf8.size(), 3U);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[0]), 0xEF);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[1]), 0xBF);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[2]), 0xBD);
    ASSERT_EQ(conv.byteToUtf16.size(), 4U);
    EXPECT_EQ(conv.byteToUtf16[3], 1U);  // sentinel: 1 UTF-16 unit consumed (the lone surrogate itself)
}

TEST(Utf8ConvertTest, LoneLowSurrogateReplacedWithReplacementCharacter) {
    const std::u16string src(u"\xDE00");  // low surrogate with nothing preceding
    const Utf8Conversion conv = toUtf8WithOffsets(src);
    ASSERT_EQ(conv.utf8.size(), 3U);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[0]), 0xEF);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[1]), 0xBF);
    EXPECT_EQ(static_cast<unsigned char>(conv.utf8[2]), 0xBD);
}

}  // namespace
