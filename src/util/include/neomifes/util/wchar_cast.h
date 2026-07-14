#pragma once

// char16_t <-> wchar_t bit-equivalent casts.
// Internally the project uses std::u16string (UTF-16, char16_t). The Win32 API
// requires LPCWSTR/LPWSTR (wchar_t). On Windows both are 16-bit UTF-16 code units,
// so the cast is a no-op at runtime and safe under the strict-aliasing rules for
// character types.
//
// Rule enforced by review: outside this header, `reinterpret_cast<wchar_t*>`
// on a char16_t pointer (or vice versa) must not appear.

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace neomifes::util {

static_assert(sizeof(wchar_t) == sizeof(char16_t),
              "wchar_cast helpers assume 16-bit wchar_t (Windows).");
static_assert(alignof(wchar_t) == alignof(char16_t),
              "wchar_cast helpers assume matching wchar_t / char16_t alignment.");

[[nodiscard]] inline const wchar_t* toWchar(const char16_t* s) noexcept {
    return reinterpret_cast<const wchar_t*>(s);
}

[[nodiscard]] inline wchar_t* toWchar(char16_t* s) noexcept {
    return reinterpret_cast<wchar_t*>(s);
}

[[nodiscard]] inline const char16_t* fromWchar(const wchar_t* s) noexcept {
    return reinterpret_cast<const char16_t*>(s);
}

[[nodiscard]] inline char16_t* fromWchar(wchar_t* s) noexcept {
    return reinterpret_cast<char16_t*>(s);
}

[[nodiscard]] inline std::wstring_view toWstringView(std::u16string_view v) noexcept {
    return {toWchar(v.data()), v.size()};
}

[[nodiscard]] inline std::u16string_view fromWstringView(std::wstring_view v) noexcept {
    return {fromWchar(v.data()), v.size()};
}

}  // namespace neomifes::util
