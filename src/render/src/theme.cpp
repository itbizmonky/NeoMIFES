#include "neomifes/render/theme.h"

namespace neomifes::render {

namespace {

// Phase 7b/7e/7h/7v/WI-06 (pre-WI-09): VSCode Dark+-inspired palette. Every
// literal below is copied verbatim from the ensureXxxBrush() methods it used
// to live inside (render_pipeline.cpp) - byte-for-byte identical so existing
// users see zero visual change from WI-09's refactor.
constexpr Theme kDarkTheme = {
    // Matches the previous GDI placeholder fill (RGB 30,30,30) so the
    // GDI->D2D handoff (ADR-009) stays visually seamless as a background.
    .background = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F, 1.0F},
    .text = {220.0F / 255.0F, 220.0F / 255.0F, 220.0F / 255.0F, 1.0F},
    // Windows' conventional selection blue (RGB 0,120,215), translucent so
    // glyphs drawn on top (drawSelectionOnLine() runs BEFORE
    // DrawTextLayout()) stay legible.
    .selection = {0.0F / 255.0F, 120.0F / 255.0F, 215.0F / 255.0F, 0.4F},
    // Translucent yellow (RGB 255,220,0) - the conventional "found text"
    // highlight color (Notepad++/VSCode Find).
    .match = {1.0F, 220.0F / 255.0F, 0.0F / 255.0F, 0.35F},
    // More saturated orange (RGB 255,140,0) for the "active" (F3-navigated-
    // to) match.
    .currentMatch = {1.0F, 140.0F / 255.0F, 0.0F / 255.0F, 0.55F},
    // Solid red (RGB 220,20,20) - the conventional bookmark/marker dot
    // color.
    .bookmark = {220.0F / 255.0F, 20.0F / 255.0F, 20.0F / 255.0F, 1.0F},
    // Neutral gray (RGB 150,150,150).
    .foldMarker = {150.0F / 255.0F, 150.0F / 255.0F, 150.0F / 255.0F, 1.0F},
    // Dimmer than text (muted gray, RGB 120,120,120) so digits read as
    // gutter chrome rather than document content.
    .lineNumber = {120.0F / 255.0F, 120.0F / 255.0F, 120.0F / 255.0F, 1.0F},
    // VSCode Dark+-inspired token palette.
    .keyword = {86.0F / 255.0F, 156.0F / 255.0F, 214.0F / 255.0F, 1.0F},
    .type = {78.0F / 255.0F, 201.0F / 255.0F, 176.0F / 255.0F, 1.0F},
    .string = {206.0F / 255.0F, 145.0F / 255.0F, 120.0F / 255.0F, 1.0F},
    .number = {181.0F / 255.0F, 206.0F / 255.0F, 168.0F / 255.0F, 1.0F},
    .comment = {106.0F / 255.0F, 153.0F / 255.0F, 85.0F / 255.0F, 1.0F},
    .preprocessor = {197.0F / 255.0F, 134.0F / 255.0F, 192.0F / 255.0F, 1.0F},
    // VSCode Dark+-inspired editorIndentGuide.background/activeBackground.
    .indentGuide = {62.0F / 255.0F, 62.0F / 255.0F, 62.0F / 255.0F, 1.0F},
    .activeIndentGuide = {110.0F / 255.0F, 110.0F / 255.0F, 110.0F / 255.0F, 1.0F},
    // Slightly lighter than the editor background so the strip reads as
    // distinct chrome.
    .breadcrumbBackground = {37.0F / 255.0F, 37.0F / 255.0F, 38.0F / 255.0F, 1.0F},
    // Same chrome color as breadcrumbBackground - a distinct strip color
    // reused rather than a second hardcoded constant.
    .minimapBackground = {37.0F / 255.0F, 37.0F / 255.0F, 38.0F / 255.0F, 1.0F},
    // Translucent white - readable against both the dark background and any
    // token color bar underneath.
    .minimapViewport = {1.0F, 1.0F, 1.0F, 0.15F},
    // Neutral mid-gray fallback for lines with content but no colored
    // token.
    .minimapText = {110.0F / 255.0F, 110.0F / 255.0F, 110.0F / 255.0F, 1.0F},
    // Dimmer than minimapText - close to the editor background so an
    // unpopulated line reads as "faint placeholder", not "confirmed plain
    // text".
    .minimapUnpopulated = {55.0F / 255.0F, 55.0F / 255.0F, 55.0F / 255.0F, 1.0F},
    // Opaque, distinguishable from both the editor background and the
    // breadcrumb/minimap chrome color - a touch lighter still, closer to
    // VSCode's own IME composition indicator.
    .imeCompositionBackground = {45.0F / 255.0F, 45.0F / 255.0F, 48.0F / 255.0F, 1.0F},
    // Translucent blue, distinct from both the selection blue and match
    // yellow above so all three remain visually distinguishable if they
    // ever coincide.
    .imeTargetClause = {100.0F / 255.0F, 150.0F / 255.0F, 220.0F / 255.0F, 0.45F},
};

// VSCode Light+-inspired palette (new for WI-09). Overlay colors
// (selection/match/currentMatch/imeTargetClause) are deliberately pastel/
// translucent so `text`'s near-black glyphs stay legible drawn on top of
// them - drawSelectionOnLine() et al. draw these BEFORE DrawTextLayout()
// (render_pipeline.cpp), the highlight is always a backdrop, never a
// separate text-color override, so every highlight below is tuned for
// "dark text on top", the mirror image of Dark's own "light text on top"
// tuning.
constexpr Theme kLightTheme = {
    // R/G/B written as 1.0F directly (not 255.0F/255.0F) since that
    // self-division trips clang-tidy's misc-redundant-expression - same
    // convention render_pipeline.cpp's ensureMatchBrushes() established
    // pre-WI-09 for its own full-intensity channels.
    .background = {1.0F, 1.0F, 1.0F, 1.0F},  // #FFFFFF
    .text = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F, 1.0F},           // #1E1E1E
    // Same Windows selection blue hue as Dark, lower alpha - Dark's 0.4
    // would look muddy blended against a white background.
    .selection = {0.0F / 255.0F, 120.0F / 255.0F, 215.0F / 255.0F, 0.25F},
    // Deeper gold than Dark's match yellow - stays visible blended against
    // white instead of washing out.
    .match = {1.0F, 205.0F / 255.0F, 0.0F / 255.0F, 0.45F},
    .currentMatch = {1.0F, 140.0F / 255.0F, 0.0F / 255.0F, 0.55F},
    // Slightly deeper red than Dark's for legibility on white.
    .bookmark = {196.0F / 255.0F, 26.0F / 255.0F, 22.0F / 255.0F, 1.0F},
    .foldMarker = {110.0F / 255.0F, 110.0F / 255.0F, 110.0F / 255.0F, 1.0F},
    .lineNumber = {150.0F / 255.0F, 150.0F / 255.0F, 150.0F / 255.0F, 1.0F},
    // VSCode Light+ token palette.
    .keyword = {0.0F / 255.0F, 0.0F / 255.0F, 1.0F, 1.0F},         // #0000FF
    .type = {38.0F / 255.0F, 127.0F / 255.0F, 153.0F / 255.0F, 1.0F},         // #267F99
    .string = {163.0F / 255.0F, 21.0F / 255.0F, 21.0F / 255.0F, 1.0F},        // #A31515
    .number = {9.0F / 255.0F, 134.0F / 255.0F, 88.0F / 255.0F, 1.0F},         // #098658
    .comment = {0.0F / 255.0F, 128.0F / 255.0F, 0.0F / 255.0F, 1.0F},         // #008000
    .preprocessor = {175.0F / 255.0F, 0.0F / 255.0F, 219.0F / 255.0F, 1.0F},  // #AF00DB
    .indentGuide = {220.0F / 255.0F, 220.0F / 255.0F, 220.0F / 255.0F, 1.0F},
    .activeIndentGuide = {160.0F / 255.0F, 160.0F / 255.0F, 160.0F / 255.0F, 1.0F},
    .breadcrumbBackground = {245.0F / 255.0F, 245.0F / 255.0F, 245.0F / 255.0F, 1.0F},
    .minimapBackground = {245.0F / 255.0F, 245.0F / 255.0F, 245.0F / 255.0F, 1.0F},
    // Translucent black - readable against both the white background and
    // any token color bar underneath (Dark's minimapViewport mirrors this
    // with translucent white for the opposite reason).
    .minimapViewport = {0.0F, 0.0F, 0.0F, 0.12F},
    .minimapText = {150.0F / 255.0F, 150.0F / 255.0F, 150.0F / 255.0F, 1.0F},
    .minimapUnpopulated = {225.0F / 255.0F, 225.0F / 255.0F, 225.0F / 255.0F, 1.0F},
    .imeCompositionBackground = {230.0F / 255.0F, 230.0F / 255.0F, 235.0F / 255.0F, 1.0F},
    .imeTargetClause = {80.0F / 255.0F, 130.0F / 255.0F, 220.0F / 255.0F, 0.45F},
};

// Windows-standard black/white/yellow high-contrast palette (new for
// WI-09). Deliberately reduced to a handful of fully-saturated hues (no
// muted grays for anything text- or signal-bearing) - real accessibility-
// focused high-contrast schemes (Windows' own "High Contrast Black") work
// the same way, since a muted color may not read as distinct at all for the
// low-vision users this theme exists for. `text` stays pure white on every
// highlight background below (selection/match/currentMatch/imeTargetClause)
// - each is dark/saturated enough, even after alpha-blending with the
// pure-black background, that white glyphs drawn on top remain legible
// (same "highlight is a backdrop, never a text-color override" constraint
// as Light's comment above).
constexpr Theme kHighContrastTheme = {
    .background = {0.0F, 0.0F, 0.0F, 1.0F},
    .text = {1.0F, 1.0F, 1.0F, 1.0F},
    // HC-standard "Highlight" accent - strong enough that white text stays
    // legible on top.
    .selection = {0.0F / 255.0F, 90.0F / 255.0F, 1.0F, 0.85F},
    // Bright yellow wash - blended over black it stays dark enough for
    // white text to remain legible on top, distinct from selection's blue.
    .match = {1.0F, 1.0F, 0.0F / 255.0F, 0.6F},
    .currentMatch = {1.0F, 140.0F / 255.0F, 0.0F / 255.0F, 0.8F},
    // Pure saturated red - HC-standard marker/error color.
    .bookmark = {1.0F, 0.0F, 0.0F, 1.0F},
    // Full white, same as `text` - never occupies the same glyph cell as
    // body text (drawn in the gutter, not the text area), and HC mode
    // avoids muted grays that may be invisible to low-vision users.
    .foldMarker = {1.0F, 1.0F, 1.0F, 1.0F},
    // Pure yellow - HC-standard "secondary but important" gutter accent,
    // distinguishable from both text and foldMarker sharing the gutter
    // strip.
    .lineNumber = {1.0F, 1.0F, 0.0F, 1.0F},
    // Reduced, fully-saturated token palette - 6 distinct hues.
    .keyword = {0.0F, 1.0F, 1.0F, 1.0F},                            // cyan
    .type = {0.0F, 1.0F, 0.0F, 1.0F},                                // green
    .string = {1.0F, 0.0F, 1.0F, 1.0F},                              // magenta
    .number = {1.0F, 140.0F / 255.0F, 0.0F / 255.0F, 1.0F},          // orange
    // Light gray - still >12:1 contrast against pure black, so "comments
    // read dimmer than code" survives without resorting to an unsafe muted
    // tone.
    .comment = {192.0F / 255.0F, 192.0F / 255.0F, 192.0F / 255.0F, 1.0F},
    .preprocessor = {1.0F, 105.0F / 255.0F, 180.0F / 255.0F, 1.0F},  // pink
    .indentGuide = {96.0F / 255.0F, 96.0F / 255.0F, 96.0F / 255.0F, 1.0F},
    .activeIndentGuide = {1.0F, 1.0F, 1.0F, 1.0F},
    // One of the only non-fully-saturated colors here, same rationale as
    // Dark/Light's own breadcrumbBackground: a background fill (not
    // foreground content) just needs to read as distinct from pure black,
    // not maximally saturated.
    .breadcrumbBackground = {40.0F / 255.0F, 40.0F / 255.0F, 40.0F / 255.0F, 1.0F},
    .minimapBackground = {40.0F / 255.0F, 40.0F / 255.0F, 40.0F / 255.0F, 1.0F},
    .minimapViewport = {1.0F, 1.0F, 1.0F, 0.3F},
    .minimapText = {1.0F, 1.0F, 0.0F, 1.0F},
    .minimapUnpopulated = {96.0F / 255.0F, 96.0F / 255.0F, 96.0F / 255.0F, 1.0F},
    .imeCompositionBackground = {0.0F / 255.0F, 0.0F / 255.0F, 128.0F / 255.0F, 1.0F},  // navy
    .imeTargetClause = {0.0F / 255.0F, 200.0F / 255.0F, 1.0F, 0.5F},
};

}  // namespace

const Theme& themeForKind(ThemeKind kind) noexcept {
    switch (kind) {
        case ThemeKind::Dark:
            return kDarkTheme;
        case ThemeKind::Light:
            return kLightTheme;
        case ThemeKind::HighContrast:
            return kHighContrastTheme;
    }
    return kDarkTheme;  // unreachable (all enumerators handled above)
}

}  // namespace neomifes::render
