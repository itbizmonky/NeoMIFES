#pragma once

// parseTagJumpReference - finds an MSVC compiler-diagnostic location
// reference ("path(line)" / "path(line,column)") embedded anywhere within a
// larger line of text (Phase 5c4, F12). Header-only declaration + a small
// .cpp, mirroring glob_match.h/fuzzy_matcher.h's split - this is the same
// "scan an arbitrary string for an embedded pattern" shape those two solve,
// unlike ui::goto_line_parser.h's parseGotoLineInput(), which validates its
// ENTIRE input as one dedicated widget's value.
//
// Deliberately supports only the MSVC/parenthesis convention, not GCC/Clang's
// "path:line:column" - a Windows absolute path (`C:\...`) already uses `:`
// right after the drive letter, and correctly disambiguating that from a
// colon-style field separator adds real parsing complexity for a benefit
// (cross-compiler support) this Windows/MSVC-first project doesn't need yet.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace neomifes::util {

struct TagJumpReference {
    std::u16string                path;    // raw path text as found, unresolved
    std::uint64_t                 line = 0;  // 1-based, mirrors ui::GotoTarget's convention
    std::optional<std::uint64_t>  column;    // 1-based; nullopt means line-start only

    friend constexpr bool operator==(const TagJumpReference&, const TagJumpReference&) = default;
};

// Scans `lineText` left to right for the first substring matching
// "path(line)" or "path(line,column)". Returns nullopt if none is found.
// `path` must look like it has a file extension (a heuristic, not a
// whitelist - see tag_jump_parser.cpp) to avoid matching unrelated
// parenthesized expressions like `if (x)` or `Foo(bar)`; a false positive
// here is harmless, since the caller's subsequent file-open attempt simply
// fails silently on a nonexistent path.
[[nodiscard]] std::optional<TagJumpReference> parseTagJumpReference(
    std::u16string_view lineText) noexcept;

}  // namespace neomifes::util
