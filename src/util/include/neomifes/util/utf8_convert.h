#pragma once

// utf8_convert - UTF-16 -> UTF-8 conversion for feeding UTF-8-only APIs
// (RE2, Phase 5a Search Engine) with text that otherwise stays UTF-16
// (char16_t) as this project's internal standard (CLAUDE.md sec.4).
//
// Converts a whole u16string_view into a UTF-8 std::string plus a
// byte-offset -> UTF-16-code-unit-offset lookup table, so callers can map
// match positions found in the UTF-8 bytes back to this project's TextPos
// (UTF-16 code units). Intended for short, per-line inputs - not a bulk
// whole-document converter (a search over a large document calls this once
// per line, mirroring the existing per-line extract() pattern in
// selection_model.cpp).
//
// This performs its own UTF-16/UTF-8 encoding rather than calling
// WideCharToMultiByte (used elsewhere for log-only diagnostic strings, see
// render_error.cpp) because building the offset table requires processing
// one codepoint at a time regardless, at which point a manual encoder avoids
// the overhead of one Win32 API call per character.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neomifes::util {

struct Utf8Conversion {
    std::string utf8;

    // byteToUtf16[i] is the UTF-16 code-unit offset (into the original
    // u16string_view) of the character whose UTF-8 encoding contains byte
    // i. Sized utf8.size() + 1: the trailing entry holds the UTF-16 length
    // of the source text, so a one-past-the-end byte offset (e.g. a match
    // ending at the end of the line) maps correctly without a separate
    // bounds check.
    std::vector<std::uint32_t> byteToUtf16;
};

// Converts `text` to UTF-8. A lone/unpaired UTF-16 surrogate is replaced
// with U+FFFD (the Unicode-recommended replacement character) rather than
// propagated - this can only happen for hand-constructed content (e.g.
// tests), since text loaded through this project's own file-loading path
// is validated UTF-8 decoded via the standard Win32 API before it ever
// becomes a u16string.
[[nodiscard]] Utf8Conversion toUtf8WithOffsets(std::u16string_view text);

}  // namespace neomifes::util
