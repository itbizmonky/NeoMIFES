#pragma once

// Thin wrapper around Win32's native codepage conversion
// (MultiByteToWideChar/WideCharToMultiByte), used by neomifes::encoding for
// the legacy Japanese encodings (Shift-JIS/EUC-JP, Phase 6b1). A hand-rolled
// JIS X 0208 mapping table (thousands of entries) would risk silent
// transcription errors that only surface on real-world files (CLAUDE.md rule
// 3 - no speculative implementation); Windows' own NLS codepage tables are
// the authoritative, Microsoft-maintained source instead. Kept in
// src/platform/ alongside clipboard.h/file_mapping.h - the other thin
// RAII/Win32-facade headers - so neomifes::encoding itself stays free of
// <windows.h> and keeps its "headless, Win32-independent codec" design
// intent (see encoding.h's file header).

#include <windows.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neomifes::platform {

enum class CodepageConvertError {
    InvalidSequence,      // bytes are not valid for `codepage`
    UnmappableCharacter,  // a UTF-16 code point has no representation in `codepage`
};

// Decodes `bytes` (encoded as `codepage`, e.g. 932 for Shift-JIS) into UTF-16.
// Uses MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, ...) - malformed
// byte sequences are rejected rather than lossily replaced, matching
// neomifes::encoding's established "reject ambiguous input" convention
// (encoding.h's decode() header comment).
[[nodiscard]] std::variant<std::u16string, CodepageConvertError> convertToUtf16(
    std::span<const std::byte> bytes, unsigned codepage);

// Encodes `text` as `codepage`. Uses WideCharToMultiByte(codepage,
// WC_NO_BEST_FIT_CHARS, ...) and rejects the result if lpUsedDefaultChar
// comes back true - WC_ERR_INVALID_CHARS (the natural mirror of
// MB_ERR_INVALID_CHARS above) fails with ERROR_INVALID_FLAGS for DBCS code
// pages such as 932/20932 (verified empirically, not merely assumed - see
// convertFromUtf16()'s implementation comment), so this is the DBCS-safe
// substitute. A code point with no representation in `codepage` (e.g. an
// emoji under Shift-JIS), or one that would only be reachable via a lossy
// "best fit" approximation, fails rather than silently returning a
// different/default character.
[[nodiscard]] std::variant<std::vector<std::byte>, CodepageConvertError> convertFromUtf16(
    std::u16string_view text, unsigned codepage);

}  // namespace neomifes::platform
