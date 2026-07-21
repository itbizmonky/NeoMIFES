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
using neomifes::encoding::detectEncoding;
using neomifes::encoding::detectLineEnding;
using neomifes::encoding::encode;
using neomifes::encoding::EncodeError;
using neomifes::encoding::Encoding;
using neomifes::encoding::LineEnding;

std::vector<std::byte> bytesOf(std::initializer_list<unsigned char> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const unsigned char v : values) {
        result.push_back(static_cast<std::byte>(v));
    }
    return result;
}

// Asserts encode() succeeded and returns the produced bytes - used by every
// round-trip test below, all of which exercise inputs known to be
// representable in every Encoding under test (see each fixture's comment).
std::vector<std::byte> encodeOrFail(std::u16string_view text, Encoding encoding) {
    const auto result = encode(text, encoding);
    EXPECT_TRUE(std::holds_alternative<std::vector<std::byte>>(result));
    if (const auto* bytes = std::get_if<std::vector<std::byte>>(&result)) {
        return *bytes;
    }
    return {};
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
        case Encoding::ShiftJis:
            return "ShiftJis";
        case Encoding::EucJp:
            return "EucJp";
        case Encoding::Iso2022Jp:
            return "Iso2022Jp";
    }
    return "Unknown";  // unreachable, every Encoding enumerator is handled above
}

// --- Round-trip: Unicode transformation formats only -----------------------
// (10 values, Phase 6a). NonBmpCharacterRequiringSurrogatePair uses an emoji
// code point, which has no JIS X 0208 representation - this test is
// deliberately not extended to ShiftJis/EucJp (see the legacy-only
// UnmappableCharacter test further down instead).

constexpr std::array<Encoding, 10> kAllEncodings{
    Encoding::Utf8,    Encoding::Utf8Bom,    Encoding::Utf16Le, Encoding::Utf16LeBom,
    Encoding::Utf16Be, Encoding::Utf16BeBom, Encoding::Utf32Le, Encoding::Utf32LeBom,
    Encoding::Utf32Be, Encoding::Utf32BeBom,
};

class EncodingRoundTripTest : public ::testing::TestWithParam<Encoding> {};

TEST_P(EncodingRoundTripTest, NonBmpCharacterRequiringSurrogatePair) {
    const Encoding       encoding = GetParam();
    const std::u16string text     = u"\U0001F600";  // matches an emoji code point
    const auto            bytes    = encodeOrFail(text, encoding);
    const auto            result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), text);
}

INSTANTIATE_TEST_SUITE_P(AllEncodings, EncodingRoundTripTest, ::testing::ValuesIn(kAllEncodings),
                         nameEncoding);

// --- Round-trip: all encodings including Shift-JIS/EUC-JP/ISO-2022-JP ------
// (Phase 6b1/6b2). EmptyString/AsciiText/JapaneseText are all representable
// in every encoding here, including the three legacy JIS X 0208-based ones.

constexpr std::array<Encoding, 13> kAllEncodingsWithLegacy{
    Encoding::Utf8,      Encoding::Utf8Bom,  Encoding::Utf16Le,   Encoding::Utf16LeBom,
    Encoding::Utf16Be,   Encoding::Utf16BeBom, Encoding::Utf32Le, Encoding::Utf32LeBom,
    Encoding::Utf32Be,   Encoding::Utf32BeBom, Encoding::ShiftJis, Encoding::EucJp,
    Encoding::Iso2022Jp,
};

class EncodingRoundTripWithLegacyTest : public ::testing::TestWithParam<Encoding> {};

TEST_P(EncodingRoundTripWithLegacyTest, EmptyString) {
    const Encoding encoding = GetParam();
    const auto     bytes    = encodeOrFail(u"", encoding);
    const auto     result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), u"");
}

TEST_P(EncodingRoundTripWithLegacyTest, AsciiText) {
    const Encoding       encoding = GetParam();
    const std::u16string text     = u"Hello, World!";
    const auto            bytes    = encodeOrFail(text, encoding);
    const auto            result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), text);
}

TEST_P(EncodingRoundTripWithLegacyTest, JapaneseText) {
    const Encoding       encoding = GetParam();
    const std::u16string text     = u"こんにちは世界";
    const auto            bytes    = encodeOrFail(text, encoding);
    const auto            result   = decode(bytes, encoding);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(result));
    EXPECT_EQ(std::get<std::u16string>(result), text);
}

INSTANTIATE_TEST_SUITE_P(AllEncodingsWithLegacy, EncodingRoundTripWithLegacyTest,
                         ::testing::ValuesIn(kAllEncodingsWithLegacy), nameEncoding);

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

TEST(DecodeErrorTest, ShiftJisRejectsInvalidByteSequence) {
    const auto bytes  = bytesOf({0xFF, 0xFF});  // 0xFF is never a valid Shift-JIS lead byte
    const auto result = decode(bytes, Encoding::ShiftJis);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, EucJpRejectsInvalidByteSequence) {
    const auto bytes  = bytesOf({0xA4});  // valid lead byte with no trailing byte
    const auto result = decode(bytes, Encoding::EucJp);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, ShiftJisRejectsC1ControlPassthroughGap) {
    // Windows' CP932 maps the otherwise-unassigned byte 0x80 to U+0080 (a
    // C1 control code) instead of rejecting it, even with
    // MB_ERR_INVALID_CHARS - undocumented, verified empirically (Phase
    // 6c1). No real Shift-JIS text means this; treat it as invalid.
    const auto bytes  = bytesOf({0x80});
    const auto result = decode(bytes, Encoding::ShiftJis);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, EucJpRejectsC1ControlPassthroughGap) {
    // Same undocumented Windows behavior as the Shift-JIS case above, but
    // CP20932's gap is wider - most of 0x80-0x9F (excluding the SS2 shift
    // byte 0x8E, which legitimately starts a half-width katakana pair).
    const auto bytes  = bytesOf({0x85});
    const auto result = decode(bytes, Encoding::EucJp);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

TEST(DecodeErrorTest, Iso2022JpRejectsMalformedKuTenPair) {
    // ESC $ B, then 0x20 0x20 (below the valid 0x21-0x7E ku-ten range),
    // then ESC ( B. CP50220's only working mode (dwFlags=0) silently maps
    // this into the Private Use Area rather than erroring -
    // decodeIso2022JpBody() rejects it by checking for that range.
    const auto bytes  = bytesOf({0x1B, 0x24, 0x42, 0x20, 0x20, 0x1B, 0x28, 0x42});
    const auto result = decode(bytes, Encoding::Iso2022Jp);
    ASSERT_TRUE(std::holds_alternative<DecodeError>(result));
    EXPECT_EQ(std::get<DecodeError>(result), DecodeError::InvalidSequence);
}

// --- encode() error handling (Shift-JIS/EUC-JP/ISO-2022-JP, Phase --------
// 6b1/6b2). The Unicode transformation formats (Phase 6a) are total
// functions and never produce EncodeError - only JIS X 0208-based
// encodings can fail here.

TEST(EncodeErrorTest, ShiftJisRejectsCharacterWithNoRepresentation) {
    const auto result = encode(u"\U0001F600", Encoding::ShiftJis);  // emoji, not in JIS X 0208
    ASSERT_TRUE(std::holds_alternative<EncodeError>(result));
    EXPECT_EQ(std::get<EncodeError>(result), EncodeError::UnmappableCharacter);
}

TEST(EncodeErrorTest, EucJpRejectsCharacterWithNoRepresentation) {
    const auto result = encode(u"\U0001F600", Encoding::EucJp);
    ASSERT_TRUE(std::holds_alternative<EncodeError>(result));
    EXPECT_EQ(std::get<EncodeError>(result), EncodeError::UnmappableCharacter);
}

TEST(EncodeErrorTest, Iso2022JpRejectsCharacterWithNoRepresentation) {
    // Caught via the EUC-JP availability-oracle pre-check
    // (encodeIso2022JpBody()) before ever reaching CP50220's lenient
    // (undetectable '?'-substituting) encode call.
    const auto result = encode(u"\U0001F600", Encoding::Iso2022Jp);
    ASSERT_TRUE(std::holds_alternative<EncodeError>(result));
    EXPECT_EQ(std::get<EncodeError>(result), EncodeError::UnmappableCharacter);
}

// --- detectEncoding() (Phase 6c1) -------------------------------------------
// Reuses detectBom()/decode() as its only building blocks (see encoding.h's
// header comment) - these tests exercise the composition, not new byte-range
// parsing logic.

TEST(DetectEncodingTest, PrefersBomWhenPresent) {
    const auto bytes = bytesOf({0xEF, 0xBB, 0xBF, 'h', 'i'});
    EXPECT_EQ(detectEncoding(bytes), Encoding::Utf8Bom);
}

TEST(DetectEncodingTest, Utf16LeBomTakesPriorityOverShiftJisLikeBytes) {
    const auto bytes = bytesOf({0xFF, 0xFE, 'h', 0x00});
    EXPECT_EQ(detectEncoding(bytes), Encoding::Utf16LeBom);
}

TEST(DetectEncodingTest, EmptyInputIsUtf8) {
    EXPECT_EQ(detectEncoding({}), Encoding::Utf8);
}

TEST(DetectEncodingTest, PureAsciiWithoutBomIsUtf8) {
    const auto bytes = bytesOf({'h', 'e', 'l', 'l', 'o'});
    EXPECT_EQ(detectEncoding(bytes), Encoding::Utf8);
}

TEST(DetectEncodingTest, ValidMultibyteUtf8WithoutBomIsUtf8) {
    const auto bytes = std::get<std::vector<std::byte>>(encode(u"こんにちは世界", Encoding::Utf8));
    EXPECT_EQ(detectEncoding(bytes), Encoding::Utf8);
}

TEST(DetectEncodingTest, ShiftJisByteInDecisiveRangeIsDetectedAsShiftJis) {
    // 0x88 falls in Shift-JIS's 0x81-0x9F lead-byte range. Without the
    // Phase 6c1 C1-control-passthrough fix (see decodeLegacyCodepageBody()),
    // Windows' CP20932 would accept these same bytes too (mapping them as
    // two unrelated C1 control code points instead of rejecting them) -
    // that fix is what makes this decisive rather than ambiguous.
    const auto bytes = bytesOf({0x88, 0x9F});  // known Shift-JIS bytes for "亜" (Phase 6b1)
    EXPECT_EQ(detectEncoding(bytes), Encoding::ShiftJis);
}

TEST(DetectEncodingTest, EucJpTrailByteOutsideShiftJisRangeIsDetectedAsEucJp) {
    // Trail byte 0xFD/0xFE is valid for EUC-JP (whose range is 0xA1-0xFE for
    // both bytes) but exceeds Shift-JIS's DBCS trailing-byte range
    // (0x40-0x7E / 0x80-0xFC, capped at 0xFC) - decisive for EUC-JP,
    // verified empirically (see the Phase 6c1 plan's probe results).
    const auto bytes = bytesOf({0xA1, 0xFD});
    EXPECT_EQ(detectEncoding(bytes), Encoding::EucJp);
}

TEST(DetectEncodingTest, AmbiguousInputReturnsNullopt) {
    // 0xA4 0xA2 is EUC-JP's encoding of "あ" (Phase 6b1's known bytes), but
    // both bytes also fall in Shift-JIS's 0xA1-0xDF single-byte half-width
    // katakana range, so Shift-JIS decode succeeds too (as two separate
    // half-width katakana characters) - a genuine, reachable ambiguity with
    // no statistical model (roadmap's N-gram stage) to break the tie. Most
    // EUC-JP byte pairs where both bytes stay within 0xA1-0xDF share this
    // ambiguity - it is not a corner case.
    const auto bytes = bytesOf({0xA4, 0xA2});
    EXPECT_EQ(detectEncoding(bytes), std::nullopt);
}

TEST(DetectEncodingTest, ByteInvalidUnderBothCodecsReturnsNullopt) {
    const auto bytes = bytesOf({0xFF, 0xFF});  // never valid Shift-JIS or EUC-JP
    EXPECT_EQ(detectEncoding(bytes), std::nullopt);
}

// --- detectLineEnding() (Phase 6c2) -----------------------------------------

TEST(DetectLineEndingTest, AllCrlfIsDetected) {
    EXPECT_EQ(detectLineEnding(u"line1\r\nline2\r\nline3"), LineEnding::Crlf);
}

TEST(DetectLineEndingTest, AllLfIsDetected) {
    EXPECT_EQ(detectLineEnding(u"line1\nline2\nline3"), LineEnding::Lf);
}

TEST(DetectLineEndingTest, AllCrIsDetected) {
    EXPECT_EQ(detectLineEnding(u"line1\rline2\rline3"), LineEnding::Cr);
}

TEST(DetectLineEndingTest, SingleCrlfWithNoFurtherLinesIsStillCrlf) {
    // Confirms a single terminator (one-line-and-a-bit of content) is
    // sufficient to report a convention - not just multi-line files.
    EXPECT_EQ(detectLineEnding(u"line1\r\nline2"), LineEnding::Crlf);
}

TEST(DetectLineEndingTest, MixedCrlfAndLfIsMixed) {
    EXPECT_EQ(detectLineEnding(u"line1\r\nline2\nline3"), LineEnding::Mixed);
}

TEST(DetectLineEndingTest, SingleMinorityTerminatorIsStillMixed) {
    // A lone \n among many \r\n still reports Mixed rather than being
    // rounded to the majority - matching roadmap's "surface this as a
    // warning" intent (master_roadmap.md §6.3), not silent majority voting.
    EXPECT_EQ(detectLineEnding(u"a\r\nb\r\nc\r\nd\r\ne\nf"), LineEnding::Mixed);
}

TEST(DetectLineEndingTest, CrlfImmediatelyFollowedByLoneCrIsMixed) {
    // "\r\n\r" must count as one CRLF + one lone CR, not two CRLFs or some
    // other double-count - exercises the boundary between the two branches.
    EXPECT_EQ(detectLineEnding(u"a\r\n\rb"), LineEnding::Mixed);
}

TEST(DetectLineEndingTest, NoLineTerminatorReturnsNullopt) {
    EXPECT_EQ(detectLineEnding(u"single line, no newline"), std::nullopt);
}

TEST(DetectLineEndingTest, EmptyTextReturnsNullopt) {
    EXPECT_EQ(detectLineEnding(u""), std::nullopt);
}

}  // namespace
