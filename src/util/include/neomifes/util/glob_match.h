#pragma once

// globMatch - simple filename-mask matching (Phase 5c1, GrepService's
// include/exclude filters). Header-only declaration + a small .cpp,
// mirroring fuzzy_matcher.h's split.
//
// Deliberately minimal compared to a full glob/gitignore language: `*`
// (any run of characters, including none) and `?` (exactly one character)
// are the only wildcards, matched against a single filename component (the
// caller is expected to pass e.g. "foo.cpp", not "src/foo.cpp" - no `/`
// segment handling, no `**`). Nothing in this codebase needs directory-
// scoped patterns yet; add them if a future caller does.

#include <string_view>

namespace neomifes::util {

// Anchored whole-string match: `pattern` must account for every character
// of `text`, not just a substring of it (globMatch(u"foo", u"foobar") is
// false). Case-insensitive over the ASCII range only, same simplification
// as fuzzyMatchScore() (fuzzy_matcher.h) - NTFS filenames are effectively
// always ASCII in practice, and this project is Windows-only, where path
// lookups are themselves case-insensitive.
[[nodiscard]] bool globMatch(std::u16string_view pattern, std::u16string_view text) noexcept;

}  // namespace neomifes::util
