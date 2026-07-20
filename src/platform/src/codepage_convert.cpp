#include "neomifes/platform/codepage_convert.h"

#include "neomifes/util/wchar_cast.h"

namespace neomifes::platform {

std::variant<std::u16string, CodepageConvertError> convertToUtf16(std::span<const std::byte> bytes,
                                                                    unsigned                    codepage) {
    if (bytes.empty()) {
        return std::u16string{};
    }
    // MultiByteToWideChar's LPCCH parameter is `const char*` - std::byte and
    // char are both required to have the same size/alignment as unsigned
    // char, and the standard's object-representation aliasing exemption
    // permits viewing arbitrary byte data through either type.
    const auto* src    = reinterpret_cast<const char*>(bytes.data());
    const auto  srcLen = static_cast<int>(bytes.size());

    const int required = ::MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
    if (required <= 0) {
        return CodepageConvertError::InvalidSequence;
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    const int    written =
        ::MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, src, srcLen, wide.data(), required);
    if (written <= 0) {
        return CodepageConvertError::InvalidSequence;
    }
    return std::u16string(util::fromWstringView(wide));
}

std::variant<std::vector<std::byte>, CodepageConvertError> convertFromUtf16(std::u16string_view text,
                                                                             unsigned codepage) {
    if (text.empty()) {
        return std::vector<std::byte>{};
    }
    const std::wstring_view wide    = util::toWstringView(text);
    const auto               wideLen = static_cast<int>(wide.size());

    // WC_ERR_INVALID_CHARS (the encode-side mirror of MB_ERR_INVALID_CHARS
    // above) was tried first but fails with ERROR_INVALID_FLAGS for DBCS
    // code pages such as 932/20932 - verified empirically against this
    // codebase's actual Windows toolchain rather than assumed, per the
    // Phase 6b1 plan's "confirm before building on top of" step. Windows
    // only guarantees that flag for CP_UTF8/CP_UTF7 and equivalent code
    // pages. WC_NO_BEST_FIT_CHARS + lpUsedDefaultChar is the DBCS-compatible
    // substitute: it disables silent "best fit" substitution (so a
    // near-miss Unicode character never turns into a different, only
    // visually-similar JIS character) and reports via lpUsedDefaultChar
    // whether any character had to fall back to the default char - which we
    // treat as failure rather than accepting the substituted byte, matching
    // the "reject ambiguous input" convention decode() above already
    // establishes.
    // wideLen (== wide.size()) is passed as the explicit length argument
    // right alongside data(), so WideCharToMultiByte never reads past
    // `wide`'s bounds regardless of null-termination - the pattern this
    // check exists to prevent.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    const int required = ::WideCharToMultiByte(codepage, WC_NO_BEST_FIT_CHARS, wide.data(), wideLen,
                                                nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return CodepageConvertError::UnmappableCharacter;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(required));
    // Same object-representation aliasing exemption as above, in reverse.
    auto* dest            = reinterpret_cast<char*>(bytes.data());
    BOOL  usedDefaultChar = FALSE;
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) - see above.
    const int written = ::WideCharToMultiByte(codepage, WC_NO_BEST_FIT_CHARS, wide.data(), wideLen, dest, required,
                                               nullptr, &usedDefaultChar);
    if (written <= 0 || usedDefaultChar) {
        return CodepageConvertError::UnmappableCharacter;
    }
    return bytes;
}

}  // namespace neomifes::platform
