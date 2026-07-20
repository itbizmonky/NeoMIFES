#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

#include "neomifes/encoding/encoding.h"

namespace {

using neomifes::encoding::BomDetection;
using neomifes::encoding::decode;
using neomifes::encoding::DecodeError;
using neomifes::encoding::detectBom;
using neomifes::encoding::encode;
using neomifes::encoding::Encoding;

std::vector<std::byte> bytesOf(std::initializer_list<unsigned char> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const unsigned char v : values) {
        result.push_back(static_cast<std::byte>(v));
    }
    return result;
}

constexpr std::array<Encoding, 10> kAllEncodings{
    Encoding::Utf8,    Encoding::Utf8Bom,    Encoding::Utf16Le, Encoding::Utf16LeBom,
    Encoding::Utf16Be, Encoding::Utf16BeBom, Encoding::Utf32Le, Encoding::Utf32LeBom,
    Encoding::Utf32Be, Encoding::Utf32BeBom,
};

// --- Round-trip (encode then decode reproduces the original text) ---------

class EncodingRoundTripTest : public ::testing::TestWithParam<Encoding> {};

TEST_P(EncodingRoundTripTest, EmptyString) {
    const Encoding encoding = GetParam();
    const auto     bytes    = encode(u"", encoding);
    const auto     result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"");
}

TEST_P(EncodingRoundTripTest, AsciiText) {
    const Encoding       encoding = GetParam();
    const std::u16string text     = u"Hello, World!";
    const auto            bytes    = encode(text, encoding);
    const auto            result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), text);
}

TEST_P(EncodingRoundTripTest, JapaneseText) {
    const Encoding       encoding = GetParam();
    const std::u16string text     = u"こんにちは世界";
    const auto            bytes    = encode(text, encoding);
    const auto            result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), text);
}

TEST_P(EncodingRoundTripTest, NonBmpCharacterRequiringSurrogatePair) {
    const Encoding       encoding = GetParam();
    const std::u16string text     = u"\U0001F600";  // matches an emoji code point
    const auto            bytes    = encode(text, encoding);
    const auto            result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), text);
}

// Named so a round-trip failure's test output identifies which Encoding
// broke (gtest's default parameter naming otherwise dumps the enum's raw
// bytes, e.g. "4-byte object <03-00 00-00>").
std::string nameEncoding(const ::testing::TestParamInfo<Encoding>& info) {
    switch (info.param) {
        case Encoding::Utf8:
            return "Utf8";
        case Encoding::Utf8Bom:
            return "Utf8Bom";
        case Encoding::Utf16Le:
            return "Utf16Le";
        case Encoding::Utf16LeBom:
            return "Utf16LeBom";
        case Encoding::Utf16Be:
            return "Utf16Be";
        case Encoding::Utf16BeBom:
            return "Utf16BeBom";
        case Encoding::Utf32Le:
            return "Utf32Le";
        case Encoding::Utf32LeBom:
            return "Utf32LeBom";
        case Encoding::Utf32Be:
            return "Utf32Be";
        case Encoding::Utf32BeBom:
            return "Utf32BeBom";
    }
    return "Unknown";  // unreachable, every Encoding enumerator is handled above
}

INSTANTIATE_TEST_SUITE_P(AllEncodings, EncodingRoundTripTest, ::testing::ValuesIn(kAllEncodings),
                         nameEncoding);

// --- detectBom() ------------------------------------------------------------

TEST(DetectBomTest, DetectsUtf8Bom) {
    const auto bytes  = bytesOf({0xEF, 0xBB, 0xBF, 'h', 'i'});
    const auto result = detectBom(bytes);
    ASSERT_TRUE(result.has_value());
    const BomDetection& detection = *result;
    EXPECT_EQ(detection.encoding, Encoding::Utf8Bom);
    EXPECT_EQ(detection.bomLength, 3U);
}

TEST(DetectBomTest, DetectsUtf16LeBom) {
    const auto bytes  = bytesOf({0xFF, 0xFE, 'h', 0x00});
    const auto result = detectBom(bytes);
    ASSERT_TRUE(result.has_value());
    const BomDetection& detection = *result;
    EXPECT_EQ(detection.encoding, Encoding::Utf16LeBom);
    EXPECT_EQ(detection.bomLength, 2U);
}

TEST(DetectBomTest, DetectsUtf16BeBom) {
    const auto bytes  = bytesOf({0xFE, 0xFF, 0x00, 'h'});
    const auto result = detectBom(bytes);
    ASSERT_TRUE(result.has_value());
    const BomDetection& detection = *result;
    EXPECT_EQ(detection.encoding, Encoding::Utf16BeBom);
    EXPECT_EQ(detection.bomLength, 2U);
}

TEST(DetectBomTest, DetectsUtf32LeBomNotMisidentifiedAsUtf16Le) {
    const auto bytes  = bytesOf({0xFF, 0xFE, 0x00, 0x00, 'h', 0x00, 0x00, 0x00});
    const auto result = detectBom(bytes);
    ASSERT_TRUE(result.has_value());
    const BomDetection& detection = *result;
    EXPECT_EQ(detection.encoding, Encoding::Utf32LeBom);
    EXPECT_EQ(detection.bomLength, 4U);
}

TEST(DetectBomTest, DetectsUtf32BeBom) {
    const auto bytes  = bytesOf({0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x00, 'h'});
    const auto result = detectBom(bytes);
    ASSERT_TRUE(result.has_value());
    const BomDetection& detection = *result;
    EXPECT_EQ(detection.encoding, Encoding::Utf32BeBom);
    EXPECT_EQ(detection.bomLength, 4U);
}

TEST(DetectBomTest, NoBomReturnsNullopt) {
    const auto bytes = bytesOf({'h', 'e', 'l', 'l', 'o'});
    EXPECT_FALSE(detectBom(bytes).has_value());
}

TEST(DetectBomTest, EmptyInputReturnsNullopt) {
    EXPECT_FALSE(detectBom({}).has_value());
}

TEST(DetectBomTest, ShortInputShorterThanAnyBomReturnsNullopt) {
    const auto bytes = bytesOf({0xFF});
    EXPECT_FALSE(detectBom(bytes).has_value());
}

TEST(DetectBomTest, DetectionResultCanBePassedDirectlyToDecode) {
    const auto bytes           = bytesOf({0xEF, 0xBB, 0xBF, 'h', 'i'});
    const auto detectionResult = detectBom(bytes);
    ASSERT_TRUE(detectionResult.has_value());
    const BomDetection& detection = *detectionResult;
    const auto            result   = decode(bytes, detection.encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"hi");
}

// --- decode() error handling -------------------------------------------------

TEST(DecodeErrorTest, Utf8TruncatedMultibyteSequence) {
    const auto bytes  = bytesOf({0xE3, 0x81});  // 3-byte lead, missing the 2nd continuation byte
    const auto result = decode(bytes, Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::TruncatedSequence);
}

TEST(DecodeErrorTest, Utf8OverlongEncoding) {
    const auto bytes  = bytesOf({0xC0, 0x80});  // overlong encoding of U+0000
    const auto result = decode(bytes, Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, Utf8EncodedSurrogateIsRejected) {
    const auto bytes  = bytesOf({0xED, 0xA0, 0x80});  // would decode to U+D800, RFC 3629 forbids this
    const auto result = decode(bytes, Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, Utf8OutOfRangeCodepoint) {
    const auto bytes  = bytesOf({0xF4, 0x90, 0x80, 0x80});  // decodes to U+110000, past U+10FFFF
    const auto result = decode(bytes, Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, Utf16OddByteCountIsTruncated) {
    const auto bytes  = bytesOf({0x41, 0x00, 0x42});
    const auto result = decode(bytes, Encoding::Utf16Le);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::TruncatedSequence);
}

TEST(DecodeErrorTest, Utf16UnpairedHighSurrogate) {
    const auto bytes  = bytesOf({0x00, 0xD8, 0x41, 0x00});  // D800 then 0041 (not a low surrogate)
    const auto result = decode(bytes, Encoding::Utf16Le);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, Utf16UnpairedLowSurrogate) {
    const auto bytes  = bytesOf({0x00, 0xDC});  // DC00 with no preceding high surrogate
    const auto result = decode(bytes, Encoding::Utf16Le);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, Utf32NonMultipleOfFourIsTruncated) {
    const auto bytes  = bytesOf({0x41, 0x00, 0x00});
    const auto result = decode(bytes, Encoding::Utf32Le);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::TruncatedSequence);
}

TEST(DecodeErrorTest, Utf32OutOfRangeCodepoint) {
    const auto bytes  = bytesOf({0x00, 0x00, 0x11, 0x00});  // U+00110000 (LE), past U+10FFFF
    const auto result = decode(bytes, Encoding::Utf32Le);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, MismatchedBomHintTooShortIsRejected) {
    const auto bytes  = bytesOf({'h', 'i'});
    const auto result = decode(bytes, Encoding::Utf8Bom);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, MismatchedBomHintWithSufficientLengthIsRejected) {
    const auto bytes  = bytesOf({'h', 'i', '!'});  // 3 bytes, but not EF BB BF
    const auto result = decode(bytes, Encoding::Utf8Bom);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

}  // namespace
