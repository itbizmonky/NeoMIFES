#pragma once

// detectLanguage - extension-based "which syntax::Language (if any) does this
// file belong to" check (Phase 7b, generalized from the original
// isCppSourceFile() bool in Phase 7d once a 2nd language - Python - existed
// to show what the right abstraction looks like, CLAUDE.md rule 3). Header-
// only pure function, mirrors tag_jump.h's shape so it stays headlessly
// unit-testable.
//
// ASCII-only casefold - matches util::globMatch()/util::fuzzyMatchScore()'s
// established "ASCII range only for now, revisit if Unicode extensions ever
// matter" convention; Windows file extensions are practically always ASCII.

#include <cwctype>
#include <filesystem>
#include <optional>

#include "neomifes/syntax/syntax.h"

namespace neomifes::app {

// Recognizes .cpp/.cc/.cxx/.h/.hpp/.hxx/.hh as C++ and .py/.pyw/.pyi as
// Python, case-insensitively; nullopt for anything else. This is the single
// gate neomifes::render::RenderPipeline::setLanguage()'s argument is built
// from. Shebang-line detection (a Python script with no .py extension) is
// deliberately not implemented - C++ detection is extension-only too, and
// symmetry is preferred until shebang detection is actually needed (Phase
// 7d plan's スコープ外).
[[nodiscard]] inline std::optional<syntax::Language> detectLanguage(
    const std::filesystem::path& path) noexcept {
    std::wstring ext = path.extension().wstring();
    for (wchar_t& ch : ext) {
        ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    }
    if (ext == L".cpp" || ext == L".cc" || ext == L".cxx" || ext == L".h" || ext == L".hpp" ||
        ext == L".hxx" || ext == L".hh") {
        return syntax::Language::Cpp;
    }
    if (ext == L".py" || ext == L".pyw" || ext == L".pyi") {
        return syntax::Language::Python;
    }
    return std::nullopt;
}

}  // namespace neomifes::app
