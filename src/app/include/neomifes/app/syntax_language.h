#pragma once

// isCppSourceFile - extension-based "is this a C++ source/header file" check
// (Phase 7b). Header-only pure function, mirrors tag_jump.h's shape so it
// stays headlessly unit-testable.
//
// ASCII-only casefold - matches util::globMatch()/util::fuzzyMatchScore()'s
// established "ASCII range only for now, revisit if Unicode extensions ever
// matter" convention; Windows file extensions are practically always ASCII.

#include <cwctype>
#include <filesystem>

namespace neomifes::app {

// Recognizes .cpp/.cc/.cxx/.h/.hpp/.hxx/.hh, case-insensitively. This is the
// single gate neomifes::render::RenderPipeline::setSyntaxHighlightingEnabled()'s
// argument is built from - no general per-language dispatch exists yet
// (Phase 7 supports only C++ so far; see master_roadmap.md sec.7.2 for the
// eventual full language list, deliberately not generalized until a second
// language shows what the right abstraction looks like - CLAUDE.md rule 3).
[[nodiscard]] inline bool isCppSourceFile(const std::filesystem::path& path) noexcept {
    std::wstring ext = path.extension().wstring();
    for (wchar_t& ch : ext) {
        ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    }
    return ext == L".cpp" || ext == L".cc" || ext == L".cxx" || ext == L".h" || ext == L".hpp" ||
           ext == L".hxx" || ext == L".hh";
}

}  // namespace neomifes::app
