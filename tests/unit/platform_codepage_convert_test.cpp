#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <variant>
#include <vector>

#include "neomifes/platform/codepage_convert.h"

namespace {

using neomifes::platform::CodepageConvertError;
using neomifes::platform::convertFromUtf16;
using neomifes::platform::convertFromUtf16Lenient;
using neomifes::platform::convertToUtf16;
using neomifes::platform::convertToUtf16Lenient;

constexpr unsigned kShiftJis   = 932;
constexpr unsigned kEucJp      = 20932;
constexpr unsigned kIso2022Jp  = 50220;

std::vector<std::byte> bytesOf(std::initializer_list<unsigned char> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const unsigned char v : values) {
        result.push_back(static_cast<std::byte>(v));
    }
    return result;
}

// --- Known byte sequences (external ground truth, not self-round-trip) ----
// "あ" (U+3042, HIRAGANA LETTER A) and "亜" (U+4E9C, the first JIS X 0208
// kanji, ku-ten 16-01) are two of the most widely documented Shift-JIS/EUC-JP
// example characters. Asserting against these fixed byte values (rather than
// only encode(decode(x)) == x) catches a bug where encode/decode are
// internally consistent with each other but both disagree with the real
// codepage table.

TEST(CodepageConvertTest, ShiftJisDecodesKnownHiragana) {
    const auto result = convertToUtf16(bytesOf({0x82, 0xA0}), kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"あ");
}

TEST(CodepageConvertTest, ShiftJisDecodesKnownKanji) {
    const auto result = convertToUtf16(bytesOf({0x88, 0x9F}), kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"亜");
}

TEST(CodepageConvertTest, ShiftJisEncodesKnownHiragana) {
    const auto result = convertFromUtf16(u"あ", kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    EXPECT_EQ(std::get<std::vector<std::byte>>(result), bytesOf({0x82, 0xA0}));
}

TEST(CodepageConvertTest, ShiftJisEncodesKnownKanji) {
    const auto result = convertFromUtf16(u"亜", kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    EXPECT_EQ(std::get<std::vector<std::byte>>(result), bytesOf({0x88, 0x9F}));
}

TEST(CodepageConvertTest, EucJpDecodesKnownHiragana) {
    const auto result = convertToUtf16(bytesOf({0xA4, 0xA2}), kEucJp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"あ");
}

TEST(CodepageConvertTest, EucJpDecodesKnownKanji) {
    const auto result = convertToUtf16(bytesOf({0xB0, 0xA1}), kEucJp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"亜");
}

TEST(CodepageConvertTest, EucJpEncodesKnownHiragana) {
    const auto result = convertFromUtf16(u"あ", kEucJp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    EXPECT_EQ(std::get<std::vector<std::byte>>(result), bytesOf({0xA4, 0xA2}));
}

TEST(CodepageConvertTest, EucJpEncodesKnownKanji) {
    const auto result = convertFromUtf16(u"亜", kEucJp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    EXPECT_EQ(std::get<std::vector<std::byte>>(result), bytesOf({0xB0, 0xA1}));
}

// --- Round-trip (multi-character sentence) ---------------------------------

TEST(CodepageConvertTest, ShiftJisRoundTripsSentence) {
    const std::u16string_view text    = u"こんにちは世界";
    const auto                 encoded = convertFromUtf16(text, kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
    const auto& bytes   = std::get<std::vector<std::byte>>(encoded);
    const auto  decoded = convertToUtf16(bytes, kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(decoded));
    EXPECT_EQ(std::get<std::u16string>(decoded), text);
}

TEST(CodepageConvertTest, EucJpRoundTripsSentence) {
    const std::u16string_view text    = u"こんにちは世界";
    const auto                 encoded = convertFromUtf16(text, kEucJp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
    const auto& bytes   = std::get<std::vector<std::byte>>(encoded);
    const auto  decoded = convertToUtf16(bytes, kEucJp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(decoded));
    EXPECT_EQ(std::get<std::u16string>(decoded), text);
}

// --- Empty input -------------------------------------------------------

TEST(CodepageConvertTest, EmptyBytesDecodeToEmptyString) {
    const auto result = convertToUtf16({}, kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"");
}

TEST(CodepageConvertTest, EmptyTextEncodesToEmptyBytes) {
    const auto result = convertFromUtf16(u"", kShiftJis);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    EXPECT_TRUE(std::get<std::vector<std::byte>>(result).empty());
}

// --- Error paths ---------------------------------------------------------
// MB_ERR_INVALID_CHARS/WC_ERR_INVALID_CHARS mean malformed/unmappable input
// fails rather than being lossily replaced, matching neomifes::encoding's
// "reject ambiguous input" convention (established in Phase 6a).

TEST(CodepageConvertTest, ShiftJisRejectsLeadByteWithNoTrailByte) {
    // 0x82 is a valid Shift-JIS lead byte but is not followed by anything.
    const auto result = convertToUtf16(bytesOf({0x82}), kShiftJis);
    ASSERT_TRUE(std::holds_alternative<CodepageConvertError>(result));
    EXPECT_EQ(std::get<CodepageConvertError>(result), CodepageConvertError::InvalidSequence);
}

TEST(CodepageConvertTest, ShiftJisRejectsInvalidLeadByte) {
    // 0xFD/0xFE/0xFF are never valid Shift-JIS lead bytes.
    const auto result = convertToUtf16(bytesOf({0xFF, 0xFF}), kShiftJis);
    ASSERT_TRUE(std::holds_alternative<CodepageConvertError>(result));
    EXPECT_EQ(std::get<CodepageConvertError>(result), CodepageConvertError::InvalidSequence);
}

TEST(CodepageConvertTest, EucJpRejectsLeadByteWithNoTrailByte) {
    const auto result = convertToUtf16(bytesOf({0xA4}), kEucJp);
    ASSERT_TRUE(std::holds_alternative<CodepageConvertError>(result));
    EXPECT_EQ(std::get<CodepageConvertError>(result), CodepageConvertError::InvalidSequence);
}

TEST(CodepageConvertTest, ShiftJisRejectsEmojiWithNoCodepageRepresentation) {
    const auto result = convertFromUtf16(u"\U0001F600", kShiftJis);
    ASSERT_TRUE(std::holds_alternative<CodepageConvertError>(result));
    EXPECT_EQ(std::get<CodepageConvertError>(result), CodepageConvertError::UnmappableCharacter);
}

TEST(CodepageConvertTest, EucJpRejectsEmojiWithNoCodepageRepresentation) {
    const auto result = convertFromUtf16(u"\U0001F600", kEucJp);
    ASSERT_TRUE(std::holds_alternative<CodepageConvertError>(result));
    EXPECT_EQ(std::get<CodepageConvertError>(result), CodepageConvertError::UnmappableCharacter);
}

// --- ISO-2022-JP (Phase 6b2, lenient mode only - dwFlags=0 is the only ----
// flag combination CP50220 accepts, verified empirically). These tests
// document convertToUtf16Lenient()/convertFromUtf16Lenient()'s actual raw
// behavior, including its lack of strict validation - the rejection logic
// lives one layer up, in neomifes::encoding::decodeIso2022JpBody()/
// encodeIso2022JpBody() (Phase 6b2 plan), not in this thin Win32 wrapper.

TEST(CodepageConvertTest, Iso2022JpDecodesKnownHiragana) {
    // ESC $ B (invoke JIS X 0208-1983), ku-ten 04-02 as 0x24 0x22, ESC ( B
    // (return to ASCII).
    const auto bytes  = bytesOf({0x1B, 0x24, 0x42, 0x24, 0x22, 0x1B, 0x28, 0x42});
    const auto result = convertToUtf16Lenient(bytes, kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"あ");
}

TEST(CodepageConvertTest, Iso2022JpDecodesKnownKanji) {
    // ku-ten 16-01 as 0x30 0x21.
    const auto bytes  = bytesOf({0x1B, 0x24, 0x42, 0x30, 0x21, 0x1B, 0x28, 0x42});
    const auto result = convertToUtf16Lenient(bytes, kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"亜");
}

TEST(CodepageConvertTest, Iso2022JpEncodesKnownHiragana) {
    const auto result = convertFromUtf16Lenient(u"あ", kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    EXPECT_EQ(std::get<std::vector<std::byte>>(result),
              bytesOf({0x1B, 0x24, 0x42, 0x24, 0x22, 0x1B, 0x28, 0x42}));
}

TEST(CodepageConvertTest, Iso2022JpEncodesKnownKanji) {
    const auto result = convertFromUtf16Lenient(u"亜", kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    EXPECT_EQ(std::get<std::vector<std::byte>>(result),
              bytesOf({0x1B, 0x24, 0x42, 0x30, 0x21, 0x1B, 0x28, 0x42}));
}

TEST(CodepageConvertTest, Iso2022JpAsciiOnlyRoundTripsWithNoEscapeSequences) {
    const std::u16string_view text    = u"Hello, World!";
    const auto                 encoded = convertFromUtf16Lenient(text, kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
    const auto& bytes = std::get<std::vector<std::byte>>(encoded);
    // Plain ASCII needs no ESC $ B / ESC ( B mode switch at all.
    EXPECT_EQ(bytes.size(), text.size());
    const auto decoded = convertToUtf16Lenient(bytes, kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(decoded));
    EXPECT_EQ(std::get<std::u16string>(decoded), text);
}

TEST(CodepageConvertTest, Iso2022JpMixedAsciiAndJapaneseRoundTrips) {
    const std::u16string_view text    = u"Hello, こんにちは世界!";
    const auto                 encoded = convertFromUtf16Lenient(text, kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(encoded));
    const auto& bytes   = std::get<std::vector<std::byte>>(encoded);
    const auto  decoded = convertToUtf16Lenient(bytes, kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(decoded));
    EXPECT_EQ(std::get<std::u16string>(decoded), text);
}

TEST(CodepageConvertTest, Iso2022JpLenientlyDecodesMalformedKuTenPairToPua) {
    // ESC $ B followed by 0x20 0x20 (below the valid 0x21-0x7E ku-ten
    // range) then ESC ( B. dwFlags=0 does NOT reject this - it silently
    // maps it into the Private Use Area instead (documented behavior of
    // this raw layer; neomifes::encoding rejects it one layer up).
    const auto bytes  = bytesOf({0x1B, 0x24, 0x42, 0x20, 0x20, 0x1B, 0x28, 0x42});
    const auto result = convertToUtf16Lenient(bytes, kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    const auto& text = std::get<std::u16string>(result);
    ASSERT_FALSE(text.empty());
    constexpr char16_t kPuaStart = 0xE000;
    constexpr char16_t kPuaEnd   = 0xF8FF;
    EXPECT_GE(text.front(), kPuaStart);
    EXPECT_LE(text.front(), kPuaEnd);
}

TEST(CodepageConvertTest, Iso2022JpLenientlyEncodesUnmappableCharacterAsQuestionMark) {
    // dwFlags=0 does NOT reject an unmappable character (emoji) - it
    // silently substitutes '?' (0x3F) with no detectable signal from this
    // layer (lpDefaultChar/lpUsedDefaultChar both fail with
    // ERROR_INVALID_PARAMETER for this code page, verified empirically).
    const auto result = convertFromUtf16Lenient(u"\U0001F600", kIso2022Jp);
    ASSERT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    const auto& bytes = std::get<std::vector<std::byte>>(result);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes.front(), std::byte{0x3F});
}

}  // namespace
