#include "neomifes/encoding/encoding.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "neomifes/platform/codepage_convert.h"

namespace neomifes::encoding {

namespace {

constexpr std::array<std::byte, 3> kUtf8Bom{std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}};
constexpr std::array<std::byte, 2> kUtf16LeBom{std::byte{0xFF}, std::byte{0xFE}};
constexpr std::array<std::byte, 2> kUtf16BeBom{std::byte{0xFE}, std::byte{0xFF}};
constexpr std::array<std::byte, 4> kUtf32LeBom{std::byte{0xFF}, std::byte{0xFE}, std::byte{0x00},
                                               std::byte{0x00}};
constexpr std::array<std::byte, 4> kUtf32BeBom{std::byte{0x00}, std::byte{0x00}, std::byte{0xFE},
                                               std::byte{0xFF}};

enum class Family : std::uint8_t { Utf8, Utf16, Utf32, LegacyCodepage, Iso2022Jp };

struct EncodingInfo {
    Family                      family    = Family::Utf8;
    bool                        bigEndian = false;  // ignored for Utf8/LegacyCodepage/Iso2022Jp
    bool                        hasBom    = false;  // always false for LegacyCodepage/Iso2022Jp
    std::span<const std::byte> bomBytes;             // empty when !hasBom
    unsigned                    codepage  = 0;       // set for LegacyCodepage (e.g. 932) and Iso2022Jp (50220)
};

// Maps every Encoding value to its transformation-format family, byte
// order, and (if applicable) BOM byte sequence - the single source of truth
// decode()/encode()/detectBom() all dispatch through, so the 10-way switch
// exists exactly once.
[[nodiscard]] EncodingInfo describe(Encoding encoding) noexcept {
    switch (encoding) {
        case Encoding::Utf8:
            return {.family = Family::Utf8, .bigEndian = false, .hasBom = false, .bomBytes = {}};
        case Encoding::Utf8Bom:
            return {.family = Family::Utf8, .bigEndian = false, .hasBom = true, .bomBytes = kUtf8Bom};
        case Encoding::Utf16Le:
            return {.family = Family::Utf16, .bigEndian = false, .hasBom = false, .bomBytes = {}};
        case Encoding::Utf16LeBom:
            return {
                .family = Family::Utf16, .bigEndian = false, .hasBom = true, .bomBytes = kUtf16LeBom};
        case Encoding::Utf16Be:
            return {.family = Family::Utf16, .bigEndian = true, .hasBom = false, .bomBytes = {}};
        case Encoding::Utf16BeBom:
            return {
                .family = Family::Utf16, .bigEndian = true, .hasBom = true, .bomBytes = kUtf16BeBom};
        case Encoding::Utf32Le:
            return {.family = Family::Utf32, .bigEndian = false, .hasBom = false, .bomBytes = {}};
        case Encoding::Utf32LeBom:
            return {
                .family = Family::Utf32, .bigEndian = false, .hasBom = true, .bomBytes = kUtf32LeBom};
        case Encoding::Utf32Be:
            return {.family = Family::Utf32, .bigEndian = true, .hasBom = false, .bomBytes = {}};
        case Encoding::Utf32BeBom:
            return {
                .family = Family::Utf32, .bigEndian = true, .hasBom = true, .bomBytes = kUtf32BeBom};
        case Encoding::ShiftJis:
            return {.family = Family::LegacyCodepage, .bigEndian = false, .hasBom = false, .bomBytes = {},
                     .codepage = 932};
        case Encoding::EucJp:
            return {.family = Family::LegacyCodepage, .bigEndian = false, .hasBom = false, .bomBytes = {},
                     .codepage = 20932};
        case Encoding::Iso2022Jp:
            return {.family = Family::Iso2022Jp, .bigEndian = false, .hasBom = false, .bomBytes = {},
                     .codepage = 50220};
    }
    // Unreachable: every Encoding enumerator is handled above. Present so
    // the compiler doesn't warn about a control path with no return (a
    // switch without `default` isn't statically known to be exhaustive).
    return {.family = Family::Utf8, .bigEndian = false, .hasBom = false, .bomBytes = {}};
}

// Combines a UTF-16 surrogate pair at text[i]/text[i+1] into one code point
// if present, advancing `i` past the low surrogate. An unpaired surrogate
// is returned verbatim (see encoding.h's header comment on why encode()
// doesn't guard against this - it cannot arise from this project's own
// Documents).
[[nodiscard]] std::uint32_t nextCodepoint(std::u16string_view text, std::size_t& i) noexcept {
    const char16_t c = text[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size()) {
        const char16_t c2 = text[i + 1];
        if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
            ++i;
            return 0x10000 + ((static_cast<std::uint32_t>(c) - 0xD800) << 10) +
                   (static_cast<std::uint32_t>(c2) - 0xDC00);
        }
    }
    return c;
}

[[nodiscard]] std::variant<std::u16string, DecodeError> decodeUtf8Body(
    std::span<const std::byte> bytes) {
    std::u16string result;
    result.reserve(bytes.size());
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto            b0 = static_cast<unsigned char>(bytes[i]);
        std::uint32_t          codepoint    = 0;
        std::size_t            seqLen       = 0;
        std::uint32_t          minCodepoint = 0;  // smallest value not "overlong" for seqLen

        if (b0 < 0x80) {
            codepoint = b0;
            seqLen    = 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            codepoint    = b0 & 0x1F;
            seqLen       = 2;
            minCodepoint = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            codepoint    = b0 & 0x0F;
            seqLen       = 3;
            minCodepoint = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            codepoint    = b0 & 0x07;
            seqLen       = 4;
            minCodepoint = 0x10000;
        } else {
            return DecodeError::InvalidSequence;  // stray continuation or invalid lead byte
        }

        if (i + seqLen > bytes.size()) {
            return DecodeError::TruncatedSequence;
        }
        for (std::size_t k = 1; k < seqLen; ++k) {
            const auto bk = static_cast<unsigned char>(bytes[i + k]);
            if ((bk & 0xC0) != 0x80) {
                return DecodeError::InvalidSequence;
            }
            codepoint = (codepoint << 6) | (bk & 0x3F);
        }
        if (codepoint < minCodepoint || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return DecodeError::InvalidSequence;  // overlong, out-of-range, or a surrogate
        }

        if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<char16_t>(codepoint));
        } else {
            const std::uint32_t v = codepoint - 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
        }
        i += seqLen;
    }
    return result;
}

[[nodiscard]] char16_t readUnit(std::span<const std::byte> bytes, std::size_t i,
                                bool bigEndian) noexcept {
    const auto b0 = static_cast<unsigned char>(bytes[i]);
    const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
    return bigEndian ? static_cast<char16_t>((b0 << 8) | b1) : static_cast<char16_t>((b1 << 8) | b0);
}

[[nodiscard]] std::variant<std::u16string, DecodeError> decodeUtf16Body(
    std::span<const std::byte> bytes, bool bigEndian) {
    if (bytes.size() % 2 != 0) {
        return DecodeError::TruncatedSequence;
    }
    std::u16string result;
    result.reserve(bytes.size() / 2);
    std::size_t i = 0;
    while (i < bytes.size()) {
        const char16_t unit = readUnit(bytes, i, bigEndian);
        i += 2;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 2 > bytes.size()) {
                return DecodeError::TruncatedSequence;
            }
            const char16_t unit2 = readUnit(bytes, i, bigEndian);
            if (unit2 < 0xDC00 || unit2 > 0xDFFF) {
                return DecodeError::InvalidSequence;  // unpaired high surrogate
            }
            result.push_back(unit);
            result.push_back(unit2);
            i += 2;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            return DecodeError::InvalidSequence;  // unpaired low surrogate
        } else {
            result.push_back(unit);
        }
    }
    return result;
}

[[nodiscard]] std::variant<std::u16string, DecodeError> decodeUtf32Body(
    std::span<const std::byte> bytes, bool bigEndian) {
    if (bytes.size() % 4 != 0) {
        return DecodeError::TruncatedSequence;
    }
    std::u16string result;
    result.reserve(bytes.size() / 4);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
        const auto b2 = static_cast<unsigned char>(bytes[i + 2]);
        const auto b3 = static_cast<unsigned char>(bytes[i + 3]);
        const std::uint32_t codepoint =
            bigEndian ? (static_cast<std::uint32_t>(b0) << 24) | (static_cast<std::uint32_t>(b1) << 16) |
                            (static_cast<std::uint32_t>(b2) << 8) | b3
                     : (static_cast<std::uint32_t>(b3) << 24) | (static_cast<std::uint32_t>(b2) << 16) |
                            (static_cast<std::uint32_t>(b1) << 8) | b0;

        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return DecodeError::InvalidSequence;
        }
        if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<char16_t>(codepoint));
        } else {
            const std::uint32_t v = codepoint - 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> encodeUtf8Body(std::u16string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t codepoint = nextCodepoint(text, i);
        if (codepoint < 0x80) {
            bytes.push_back(static_cast<std::byte>(codepoint));
        } else if (codepoint < 0x800) {
            bytes.push_back(static_cast<std::byte>(0xC0 | (codepoint >> 6)));
            bytes.push_back(static_cast<std::byte>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint < 0x10000) {
            bytes.push_back(static_cast<std::byte>(0xE0 | (codepoint >> 12)));
            bytes.push_back(static_cast<std::byte>(0x80 | ((codepoint >> 6) & 0x3F)));
            bytes.push_back(static_cast<std::byte>(0x80 | (codepoint & 0x3F)));
        } else {
            bytes.push_back(static_cast<std::byte>(0xF0 | (codepoint >> 18)));
            bytes.push_back(static_cast<std::byte>(0x80 | ((codepoint >> 12) & 0x3F)));
            bytes.push_back(static_cast<std::byte>(0x80 | ((codepoint >> 6) & 0x3F)));
            bytes.push_back(static_cast<std::byte>(0x80 | (codepoint & 0x3F)));
        }
        ++i;
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> encodeUtf16Body(std::u16string_view text, bool bigEndian) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() * 2);
    for (const char16_t c : text) {
        const auto hi = static_cast<std::byte>((c >> 8) & 0xFF);
        const auto lo = static_cast<std::byte>(c & 0xFF);
        if (bigEndian) {
            bytes.push_back(hi);
            bytes.push_back(lo);
        } else {
            bytes.push_back(lo);
            bytes.push_back(hi);
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> encodeUtf32Body(std::u16string_view text, bool bigEndian) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() * 4);
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t codepoint = nextCodepoint(text, i);
        const auto           b0        = static_cast<std::byte>(codepoint & 0xFF);
        const auto           b1        = static_cast<std::byte>((codepoint >> 8) & 0xFF);
        const auto           b2        = static_cast<std::byte>((codepoint >> 16) & 0xFF);
        const auto           b3        = static_cast<std::byte>((codepoint >> 24) & 0xFF);
        if (bigEndian) {
            bytes.push_back(b3);
            bytes.push_back(b2);
            bytes.push_back(b1);
            bytes.push_back(b0);
        } else {
            bytes.push_back(b0);
            bytes.push_back(b1);
            bytes.push_back(b2);
            bytes.push_back(b3);
        }
        ++i;
    }
    return bytes;
}

[[nodiscard]] std::variant<std::u16string, DecodeError> decodeLegacyCodepageBody(
    std::span<const std::byte> bytes, unsigned codepage) {
    const auto  result = platform::convertToUtf16(bytes, codepage);
    const auto* text    = std::get_if<std::u16string>(&result);
    if (text == nullptr) {
        return DecodeError::InvalidSequence;
    }
    // Windows' CP932/CP20932 map a handful of otherwise-unassigned bytes
    // (a lone 0x80 in Shift-JIS; most of 0x80-0x9F in EUC-JP) as an
    // identity passthrough to the C1 control block (U+0080-U+009F) instead
    // of rejecting them - even with MB_ERR_INVALID_CHARS set. This is
    // undocumented, verified empirically while building this function (see
    // the Phase 6c1 plan). No legitimate Shift-JIS/EUC-JP text encodes a
    // real C1 control character this way, so any such code point in the
    // output means the input byte wasn't actually valid JIS text - reject
    // it, closing this passthrough gap rather than silently accepting it.
    if (std::ranges::any_of(*text, [](char16_t c) { return c >= 0x0080 && c <= 0x009F; })) {
        return DecodeError::InvalidSequence;
    }
    return *text;
}

[[nodiscard]] std::variant<std::vector<std::byte>, EncodeError> encodeLegacyCodepageBody(
    std::u16string_view text, unsigned codepage) {
    const auto result = platform::convertFromUtf16(text, codepage);
    if (const auto* bytes = std::get_if<std::vector<std::byte>>(&result)) {
        return *bytes;
    }
    return EncodeError::UnmappableCharacter;
}

[[nodiscard]] std::variant<std::u16string, DecodeError> decodeIso2022JpBody(
    std::span<const std::byte> bytes, unsigned codepage) {
    const auto  result = platform::convertToUtf16Lenient(bytes, codepage);
    const auto* text    = std::get_if<std::u16string>(&result);
    if (text == nullptr) {
        return DecodeError::InvalidSequence;
    }
    // CP50220 accepts no strict-validation flag at all (dwFlags=0 is the
    // only mode it doesn't reject with ERROR_INVALID_FLAGS - verified
    // empirically, Phase 6b2 plan), so it silently maps malformed escape
    // sequences/invalid ku-ten pairs into the Private Use Area instead of
    // erroring. No legitimate ISO-2022-JP content (ASCII/JIS-Roman/JIS
    // X 0208) decodes to the PUA, so treat any such code point as evidence
    // the input wasn't actually valid ISO-2022-JP.
    if (std::ranges::any_of(*text, [](char16_t c) { return c >= 0xE000 && c <= 0xF8FF; })) {
        return DecodeError::InvalidSequence;
    }
    return *text;
}

[[nodiscard]] std::variant<std::vector<std::byte>, EncodeError> encodeIso2022JpBody(
    std::u16string_view text, unsigned codepage) {
    // CP50220 provides no way to detect "unmappable character silently
    // replaced with '?'" - lpDefaultChar/lpUsedDefaultChar both fail with
    // ERROR_INVALID_PARAMETER for this code page even supplied individually
    // (verified empirically, Phase 6b2 plan). Windows documents CP50220 and
    // CP20932 (EUC-JP) as covering the identical character repertoire
    // ("Japanese, JIS X 0208-1990 & 0212-1990"), and CP20932 already
    // supports strict validation (Phase 6b1) - so it's used here purely as
    // an availability oracle: if `text` can't be strictly encoded as
    // EUC-JP, it's rejected before ever reaching the lenient ISO-2022-JP
    // call, rather than silently returning a '?'-substituted result.
    if (!std::holds_alternative<std::vector<std::byte>>(platform::convertFromUtf16(text, 20932))) {
        return EncodeError::UnmappableCharacter;
    }
    const auto  result = platform::convertFromUtf16Lenient(text, codepage);
    const auto* bytes   = std::get_if<std::vector<std::byte>>(&result);
    if (bytes == nullptr) {
        return EncodeError::UnmappableCharacter;
    }
    return *bytes;
}

}  // namespace

std::variant<std::u16string, DecodeError> decode(std::span<const std::byte> bytes,
                                                  Encoding                    encoding) {
    const auto                  info = describe(encoding);
    std::span<const std::byte> body = bytes;
    if (info.hasBom) {
        if (bytes.size() < info.bomBytes.size() ||
            !std::ranges::equal(info.bomBytes, bytes.first(info.bomBytes.size()))) {
            return DecodeError::InvalidSequence;
        }
        body = bytes.subspan(info.bomBytes.size());
    }
    switch (info.family) {
        case Family::Utf8:
            return decodeUtf8Body(body);
        case Family::Utf16:
            return decodeUtf16Body(body, info.bigEndian);
        case Family::Utf32:
            return decodeUtf32Body(body, info.bigEndian);
        case Family::LegacyCodepage:
            return decodeLegacyCodepageBody(body, info.codepage);
        case Family::Iso2022Jp:
            return decodeIso2022JpBody(body, info.codepage);
    }
    return DecodeError::InvalidSequence;  // unreachable, see describe()'s comment
}

std::variant<std::vector<std::byte>, EncodeError> encode(std::u16string_view text, Encoding encoding) {
    const auto info = describe(encoding);
    if (info.family == Family::LegacyCodepage) {
        return encodeLegacyCodepageBody(text, info.codepage);  // no BOM for legacy code pages
    }
    if (info.family == Family::Iso2022Jp) {
        return encodeIso2022JpBody(text, info.codepage);  // no BOM for ISO-2022-JP either
    }
    std::vector<std::byte> bytes(info.bomBytes.begin(), info.bomBytes.end());
    std::vector<std::byte> body;
    switch (info.family) {
        case Family::Utf8:
            body = encodeUtf8Body(text);
            break;
        case Family::Utf16:
            body = encodeUtf16Body(text, info.bigEndian);
            break;
        case Family::Utf32:
            body = encodeUtf32Body(text, info.bigEndian);
            break;
        case Family::LegacyCodepage:
        case Family::Iso2022Jp:
            break;  // handled above
    }
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

std::optional<BomDetection> detectBom(std::span<const std::byte> bytes) noexcept {
    // Longer (4-byte) BOMs are checked first: UTF-32 LE's BOM (FF FE 00 00)
    // shares its first 2 bytes with UTF-16 LE's BOM (FF FE), so checking
    // UTF-16 first would misdetect a UTF-32 LE file.
    if (bytes.size() >= kUtf32LeBom.size() && std::ranges::equal(kUtf32LeBom, bytes.first(kUtf32LeBom.size()))) {
        return BomDetection{.encoding = Encoding::Utf32LeBom, .bomLength = kUtf32LeBom.size()};
    }
    if (bytes.size() >= kUtf32BeBom.size() && std::ranges::equal(kUtf32BeBom, bytes.first(kUtf32BeBom.size()))) {
        return BomDetection{.encoding = Encoding::Utf32BeBom, .bomLength = kUtf32BeBom.size()};
    }
    if (bytes.size() >= kUtf8Bom.size() && std::ranges::equal(kUtf8Bom, bytes.first(kUtf8Bom.size()))) {
        return BomDetection{.encoding = Encoding::Utf8Bom, .bomLength = kUtf8Bom.size()};
    }
    if (bytes.size() >= kUtf16LeBom.size() &&
        std::ranges::equal(kUtf16LeBom, bytes.first(kUtf16LeBom.size()))) {
        return BomDetection{.encoding = Encoding::Utf16LeBom, .bomLength = kUtf16LeBom.size()};
    }
    if (bytes.size() >= kUtf16BeBom.size() &&
        std::ranges::equal(kUtf16BeBom, bytes.first(kUtf16BeBom.size()))) {
        return BomDetection{.encoding = Encoding::Utf16BeBom, .bomLength = kUtf16BeBom.size()};
    }
    return std::nullopt;
}

std::optional<Encoding> detectEncoding(std::span<const std::byte> head) noexcept {
    if (const auto bom = detectBom(head)) {
        return bom->encoding;
    }
    if (head.empty() || std::holds_alternative<std::u16string>(decode(head, Encoding::Utf8))) {
        return Encoding::Utf8;
    }
    // Shift-JIS's 0x81-0x9F lead-byte range is entirely outside EUC-JP's
    // valid byte range (0xA1-0xFE for both bytes of a pair), so any head
    // containing such a byte necessarily fails EUC-JP decode - the
    // "priority marker" roadmap describes falls out of decode()'s existing
    // strict validation rather than needing a separate byte-range check.
    const bool shiftJisOk = std::holds_alternative<std::u16string>(decode(head, Encoding::ShiftJis));
    const bool eucJpOk    = std::holds_alternative<std::u16string>(decode(head, Encoding::EucJp));
    if (shiftJisOk && !eucJpOk) {
        return Encoding::ShiftJis;
    }
    if (eucJpOk && !shiftJisOk) {
        return Encoding::EucJp;
    }
    // Both succeed (genuinely ambiguous - e.g. a byte pair that is
    // simultaneously two valid Shift-JIS half-width-katakana bytes and one
    // valid EUC-JP double-byte character) or neither succeeds (not
    // recognized). Disambiguating the former without a statistical model
    // (roadmap's N-gram stage, out of scope here) would be a guess.
    return std::nullopt;
}

std::optional<LineEnding> detectLineEnding(std::u16string_view text) noexcept {
    std::size_t crlfCount = 0;
    std::size_t lfCount   = 0;
    std::size_t crCount   = 0;
    std::size_t i         = 0;
    while (i < text.size()) {
        if (text[i] == u'\r') {
            if (i + 1 < text.size() && text[i + 1] == u'\n') {
                ++crlfCount;
                i += 2;
            } else {
                ++crCount;
                ++i;
            }
        } else if (text[i] == u'\n') {
            ++lfCount;
            ++i;
        } else {
            ++i;
        }
    }
    const int conventionsSeen = (crlfCount > 0 ? 1 : 0) + (lfCount > 0 ? 1 : 0) + (crCount > 0 ? 1 : 0);
    if (conventionsSeen == 0) {
        return std::nullopt;
    }
    if (conventionsSeen > 1) {
        return LineEnding::Mixed;
    }
    if (crlfCount > 0) {
        return LineEnding::Crlf;
    }
    return lfCount > 0 ? LineEnding::Lf : LineEnding::Cr;
}

}  // namespace neomifes::encoding
