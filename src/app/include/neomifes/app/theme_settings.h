#pragma once

// parseThemeKind/themeKindToSettingsString - the app-layer bridge between
// core::Settings::themeName (a free-form persisted std::u16string, WI-08)
// and render::ThemeKind (WI-09). Header-only pure functions, mirroring
// tab_index_math.h's "small shared helper, dependency-free, unit-testable
// without a live Workspace/RenderPipeline" shape - no new CMake target
// needed, same reasoning tab_index_math.h's own header comment gives.
//
// This split exists so render::ThemeKind/render::Theme never depend on
// core::Settings (CLAUDE.md §3's layering rule: render:: (L4) may not
// depend on core:: (L5)) - exactly the role syntax_language.h's
// detectLanguage() already plays for syntax::Language vs.
// std::filesystem::path.
//
// core::Settings::loadFrom() does NOT validate themeName (unlike tabWidth/
// fontSizeDips, which ARE clamped there - see settings.cpp) - themeName
// stays a free-form persisted string, same as fontFamily. parseThemeKind()
// below is the actual safety net, applied at the point of consumption
// rather than at the point of load (CLAUDE.md's "validate at boundaries"
// principle) - core::Settings itself is not touched for this WI.

#include <string_view>

#include "neomifes/render/theme.h"

namespace neomifes::app {

// u"light" -> Light, u"high-contrast" -> HighContrast, anything else
// (u"dark", empty, or an unrecognized/typo'd value from a hand-edited or
// stale settings.json) -> Dark. Never fails - same "never fail to start,
// always fall back to a safe default" contract core::Settings::loadFrom()
// itself already applies to every other field.
[[nodiscard]] inline render::ThemeKind parseThemeKind(std::u16string_view name) noexcept {
    if (name == u"light") {
        return render::ThemeKind::Light;
    }
    if (name == u"high-contrast") {
        return render::ThemeKind::HighContrast;
    }
    return render::ThemeKind::Dark;
}

// The exact inverse of parseThemeKind() above - whichever code applies a
// ThemeKind can also write the matching string back into
// settings.themeName, keeping the persisted string and the actually-applied
// theme from ever drifting apart (single source of truth - see the
// view.theme.* command palette entries in normal_mode_wiring.cpp).
[[nodiscard]] inline std::u16string_view themeKindToSettingsString(render::ThemeKind kind) noexcept {
    switch (kind) {
        case render::ThemeKind::Dark:
            return u"dark";
        case render::ThemeKind::Light:
            return u"light";
        case render::ThemeKind::HighContrast:
            return u"high-contrast";
    }
    return u"dark";  // unreachable (all enumerators handled above)
}

// WI-21e: Dark -> Light -> HighContrast -> Dark, the fixed cycle order
// CommandId::ThemeCycle steps through (see that enumerator's own
// declaration comment for why a cycle command exists alongside the 3
// existing direct-selection view.theme.* palette entries). A switch, not a
// chained ternary - the equivalent nested ?: form trips clang-tidy's
// readability-avoid-nested-conditional-operator (treated as an error under
// this project's -WX).
[[nodiscard]] inline render::ThemeKind nextThemeKind(render::ThemeKind current) noexcept {
    switch (current) {
        case render::ThemeKind::Dark:
            return render::ThemeKind::Light;
        case render::ThemeKind::Light:
            return render::ThemeKind::HighContrast;
        case render::ThemeKind::HighContrast:
            return render::ThemeKind::Dark;
    }
    return render::ThemeKind::Dark;  // unreachable (all enumerators handled above)
}

}  // namespace neomifes::app
