#pragma once

// Theme - the small set of colors RenderPipeline paints itself with (WI-09,
// build_plan.md §5). Pure data + a pure kind->Theme lookup function, no
// dependency on core::Settings or anything else above render:: in the
// layering (CLAUDE.md §3) - the app layer (theme_settings.h) is the only
// place a persisted settings string ever gets turned into a ThemeKind, the
// same split syntax_language.h's detectLanguage() already established for
// syntax::Language.
//
// High-contrast OS auto-detection (SPI_GETHIGHCONTRAST) is explicitly out of
// scope here (build_plan.md marks it optional; the requirements doc doesn't
// mention it at all, §14) - HighContrast below is only ever reached by
// explicit user choice (command palette), never inferred from the OS.

#include <d2d1.h>

namespace neomifes::render {

enum class ThemeKind { Dark, Light, HighContrast };

// One color per role RenderPipeline's ensureXxxBrush()/renderOnce() draw
// with. Field order mirrors the order the 11 ensureXxxBrush() methods
// appear in render_pipeline.cpp (background first, matching renderOnce()'s
// own dc->Clear() call) - keeps this struct mechanically diffable against
// that file. Note: no separate caret color exists - drawCaretOnLine() draws
// the caret with the same brush as body text (m_textBrush), so `text` below
// already covers it.
struct Theme {
    D2D1_COLOR_F background;
    D2D1_COLOR_F text;
    D2D1_COLOR_F selection;
    D2D1_COLOR_F match;
    D2D1_COLOR_F currentMatch;
    D2D1_COLOR_F bookmark;
    D2D1_COLOR_F foldMarker;
    D2D1_COLOR_F lineNumber;
    D2D1_COLOR_F keyword;
    D2D1_COLOR_F type;
    D2D1_COLOR_F string;
    D2D1_COLOR_F number;
    D2D1_COLOR_F comment;
    D2D1_COLOR_F preprocessor;
    D2D1_COLOR_F indentGuide;
    D2D1_COLOR_F activeIndentGuide;
    D2D1_COLOR_F breadcrumbBackground;
    D2D1_COLOR_F minimapBackground;
    D2D1_COLOR_F minimapViewport;
    D2D1_COLOR_F minimapText;
    D2D1_COLOR_F minimapUnpopulated;
    D2D1_COLOR_F imeCompositionBackground;
    D2D1_COLOR_F imeTargetClause;
    // WI-14c: log-mode line color-coding (RenderPipeline::
    // drawLogLevelOnLine()). Only 2 severities get a dedicated color -
    // Error/Fatal share logError, Warning uses logWarning; Trace/Debug/
    // Info/Unknown fall back to `text` unmodified (see logLevelBrush()'s
    // own comment for why the other 5 LogLevel values aren't distinguished
    // visually in this WI's MVP scope).
    D2D1_COLOR_F logError;
    D2D1_COLOR_F logWarning;
};

// Dark/Light/HighContrast -> the Theme RenderPipeline should paint with.
// Dark is the pre-WI-09 hardcoded palette verbatim (every ensureXxxBrush()
// literal this replaces) - existing users see zero visual change. Never
// fails: an "impossible" ThemeKind value can't reach this function at all
// (parseThemeKind() at the app layer is the only place a raw string ever
// becomes a ThemeKind, and it already falls back to Dark for anything it
// doesn't recognize - see theme_settings.h).
[[nodiscard]] const Theme& themeForKind(ThemeKind kind) noexcept;

}  // namespace neomifes::render
