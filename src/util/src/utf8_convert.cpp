#include "neomifes/util/utf8_convert.h"

namespace neomifes::util {

namespace {

constexpr char16_t kHighSurrogateStart = 0xD800;
constexpr char16_t kHighSurrogateEnd   = 0xDBFF;
constexpr char16_t kLowSurrogateStart  = 0xDC00;
constexpr char16_t kLowSurrogateEnd    = 0xDFFF;
constexpr char32_t kReplacementChar    = 0xFFFD;

// Decodes the codepoint starting at text[i], returning it along with the
// number of UTF-16 code units it consumed (1, or 2 for a valid surrogate
// pair). A lone/unpaired surrogate decodes to U+FFFD and consumes 1 unit.
[[nodiscard]] std::pair<char32_t, std::size_t> decodeOne(std::u16string_view text, std::size_t i) {
    const char16_t unit = text[i];
    if (unit >= kHighSurrogateStart && unit <= kHighSurrogateEnd) {
        if (i + 1 < text.size()) {
            const char16_t low = text[i + 1];
            if (low >= kLowSurrogateStart && low <= kLowSurrogateEnd) {
                const char32_t codepoint = 0x10000 + ((static_cast<char32_t>(unit) - kHighSurrogateStart) << 10) +
                                            (static_cast<char32_t>(low) - kLowSurrogateStart);
                return {codepoint, 2};
            }
        }
        return {kReplacementChar, 1};
    }
    if (unit >= kLowSurrogateStart && unit <= kLowSurrogateEnd) {
        return {kReplacementChar, 1};
    }
    return {static_cast<char32_t>(unit), 1};
}

void appendUtf8(std::string& out, char32_t codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

}  // namespace

Utf8Conversion toUtf8WithOffsets(std::u16string_view text) {
    Utf8Conversion result;
    result.utf8.reserve(text.size() * 3);
    result.byteToUtf16.reserve((text.size() * 3) + 1);

    std::size_t i = 0;
    while (i < text.size()) {
        const auto [codepoint, unitsConsumed] = decodeOne(text, i);

        const std::size_t byteStart = result.utf8.size();
        appendUtf8(result.utf8, codepoint);
        const std::size_t byteCount = result.utf8.size() - byteStart;
        for (std::size_t b = 0; b < byteCount; ++b) {
            result.byteToUtf16.push_back(static_cast<std::uint32_t>(i));
        }

        i += unitsConsumed;
    }
    result.byteToUtf16.push_back(static_cast<std::uint32_t>(text.size()));

    return result;
}

}  // namespace neomifes::util
