#pragma once

// RenderPipeline - the facade MainWindow/app code actually talks to.
// Owns at most one RenderDevice; on device loss, drops and recreates it
// wholesale (per Direct3D/DXGI guidance - a lost device invalidates the
// entire object graph, not just the swap chain).
//
// Phase 3b: attach/resize/render draw the visible lines of an attached
// Document with a single fixed-pitch IDWriteTextFormat, no word wrap,
// topLine always 0 by default (no interactive scrolling yet - Editor Core /
// Viewport is Phase 4).
//
// Phase 3c (ADR-011): drawVisibleLines() reuses cached IDWriteTextLayout
// objects (TextLayoutCache) instead of laying out every visible line fresh
// every frame, and render() skips the entire beginFrame/Clear/draw/endFrame
// sequence when nothing has changed since the last successful frame (a
// coarse, frame-level "damage" check - see FrameState below). A custom
// glyph-atlas cache and fine-grained dirty-rect tracking are deliberately
// deferred (ADR-011 records why and the re-evaluation triggers).

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/document/text_pos.h"
// WI-14c: m_logLineLevels below needs logmode::LogLevel's complete type
// (a std::vector member, not a pointer). neomifes::logmode depends only on
// neomifes::document (a self-contained leaf module, same as neomifes::
// syntax above) so RenderPipeline uses logmode::LogLevel directly rather
// than mirroring it into a render::-only type - the same precedent
// syntax::Token/syntax::Language already established (see this class's
// setLanguage()).
#include "neomifes/logmode/log_pattern.h"
#include "neomifes/render/render_device.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/syntax_worker.h"
#include "neomifes/render/text_layout_cache.h"
#include "neomifes/render/theme.h"
// Phase 7b: m_tokens below needs syntax::Token's complete type (it's a
// std::vector member, not a pointer), even though no syntax:: type appears
// in this class's public method signatures. See src/render/CMakeLists.txt's
// comment on why neomifes::syntax is linked PUBLIC despite that.
#include "neomifes/syntax/syntax.h"
// Phase 7h: m_cachedOutline below needs syntax::OutlineNode's complete type,
// same reasoning as syntax.h above.
#include "neomifes/syntax/outline.h"

namespace neomifes::document {
class Document;
class BufferSnapshot;
}  // namespace neomifes::document

namespace neomifes::render {

// One cursor's visual state: where its caret sits, and what (if anything) it
// has selected. Deliberately document::-typed only (not core::Cursor) - same
// "independent, concurrently runnable engines" reasoning as the rest of this
// class's public surface (Phase 4b7a, generalizing the single caret/
// selection fields Phase 4b1/4b2 introduced).
struct CursorVisual {
    document::TextPos   position       = 0;
    document::TextRange selectionRange{};  // start==end: no selection for this cursor
    // Phase 4b8e (フリーカーソル簡略版): number of virtual columns past the
    // real end of `position`'s line the caret should be drawn at - 0 for
    // every ordinary cursor. main.cpp is the only writer (it tracks the
    // count as session-lifetime UI state, not a document position - see
    // main.cpp's freeCursorVirtualColumns); `position` itself always stays a
    // real, in-document offset.
    std::uint32_t virtualColumnOffset = 0;
    // Phase 7h (Breadcrumb): mirrors core::Cursor::isPrimary - drawBreadcrumb()
    // needs to know which single cursor's position to show a symbol path for
    // when multiple cursors exist. Defaulted to false (not left unset) so
    // every existing partial designated-initializer construction site (this
    // codebase's established clang-cl "missing designated field initializer"
    // avoidance, same treatment as virtualColumnOffset above) keeps compiling
    // unchanged. main.cpp's syncRenderStateAndInvalidate() is the sole writer,
    // same ownership contract as every other CursorVisual field.
    bool isPrimary = false;

    friend constexpr bool operator==(const CursorVisual&, const CursorVisual&) = default;
};

// One search-match highlight (Phase 5b3a, Find bar UI). Deliberately
// document::-typed only, same "independent, concurrently runnable engines"
// reasoning as CursorVisual above - RenderPipeline does not know about
// search::Match; the app layer (main.cpp) builds these from
// search::SearchService::findAll()'s results.
struct MatchVisual {
    document::TextRange range;
    bool                isCurrent = false;  // true: the "active" match (F3-navigated-to), drawn in a distinct color

    friend constexpr bool operator==(const MatchVisual&, const MatchVisual&) = default;
};

// One foldable symbol region (Phase 7i) - a render::-only mirror of
// core::FoldRegion, same "independent, concurrently runnable engines"
// reasoning as CursorVisual/MatchVisual above (RenderPipeline does not know
// about core::FoldingModel; the app layer converts and pushes this in
// whenever FoldingModel's state changes). headerLine stays visible whether
// folded or not; (headerLine, endLineInclusive] is hidden while folded.
struct FoldVisual {
    document::LineNumber headerLine;
    document::LineNumber endLineInclusive;
    bool                  folded = false;

    friend constexpr bool operator==(const FoldVisual&, const FoldVisual&) = default;
};

// One Git diff hunk marker (WI-17c) - a render::-only mirror of
// git::LineDiffRegion/git::LineDiffKind, same "independent, concurrently
// runnable engines" reasoning as FoldVisual above (RenderPipeline does not
// depend on neomifes::git; the app layer converts via app::
// buildGitDiffMarkers() and pushes this in whenever EditorSession::gitDiff()
// changes). Added/Modified: [startLine, startLine + lineCount) are the
// CURRENT document's own lines this region covers. Deleted: lineCount is
// always 0 (a point marker - HEAD had lines here the current document no
// longer has at all, so startLine is the current-document line immediately
// AFTER where the deleted content used to be, matching git::LineDiffRegion's
// own documented convention exactly).
enum class GitDiffKind : std::uint8_t { Added, Modified, Deleted };

struct GitDiffMarker {
    document::LineNumber startLine = 0;
    document::LineNumber lineCount = 0;
    GitDiffKind            kind      = GitDiffKind::Added;

    friend constexpr bool operator==(const GitDiffMarker&, const GitDiffMarker&) = default;
};

// WI-17f: one contiguous run of Added or Removed lines in the Diff view's
// own SYNTHESIZED document (see setDiffViewLineRegions()'s own comment for
// why this is a deliberately SEPARATE type from GitDiffMarker above, not a
// reinterpretation of it). Reuses GitDiffKind's Added/Deleted values only
// (Modified has no per-line meaning in a unified diff - a "modified" line
// is just a Removed line immediately followed by an Added line, the same
// decomposition git::UnifiedDiffLine/unifiedDiffAgainstHead() already
// produce). UNLIKE GitDiffMarker's Deleted point-marker convention,
// [startLine, startLine + lineCount) here is always a REAL range - the
// synthesized diff document actually contains removed lines as real text,
// something the live document never does.
struct DiffViewLineMarker {
    document::LineNumber startLine = 0;
    document::LineNumber lineCount = 0;
    GitDiffKind            kind      = GitDiffKind::Added;

    friend constexpr bool operator==(const DiffViewLineMarker&, const DiffViewLineMarker&) = default;
};

// An in-progress IME composition (WI-06) - the unconfirmed text a Japanese/
// CJK IME is still converting, never written to Document (only the eventual
// committed string is - see main_window.h's onImeResult). Drawn as an
// overlay on top of the real line at `anchorRange`, not spliced into it (see
// drawImeCompositionOnLine()'s header comment for why true reflow was
// rejected). Deliberately document::-typed only, same "independent,
// concurrently runnable engines" reasoning as CursorVisual/MatchVisual above
// - RenderPipeline does not know about Win32 IME types (HIMC etc.), the app
// layer decodes those via MainWindow's onImeComposition hook and builds this.
struct ImeComposition {
    // Where in the DOCUMENT this visually inserts - captured once at
    // WM_IME_STARTCOMPOSITION time (the primary cursor's selection, if any,
    // so composing replaces it the same way ordinary typeover would). Stays
    // fixed for the whole composition session; `text` below is what actually
    // changes as the user keeps typing.
    document::TextRange anchorRange;
    // GCS_COMPSTR - the current unconfirmed string. Never written to
    // Document.
    std::u16string text;
    // [start, end) code-unit range WITHIN `text` (not within the document)
    // identifying the clause currently being edited (GCS_COMPATTR's
    // ATTR_TARGET_CONVERTED/ATTR_TARGET_NOTCONVERTED run) - highlighted with
    // a distinct background. nullopt if the IME hasn't picked a conversion
    // target yet (e.g. right after the first keystroke).
    std::optional<std::pair<std::uint32_t, std::uint32_t>> targetClauseRange;

    friend bool operator==(const ImeComposition&, const ImeComposition&) = default;
};

class RenderPipeline {
public:
    // Queries the current client-area size and DPI itself (GetClientRect /
    // GetDpiForWindow) rather than trusting a prior WM_SIZE/WM_DPICHANGED -
    // Windows doesn't reliably fire either for the initial CreateWindowExW
    // state, so this is the only correct source of truth for the first
    // frame's dimensions and scale.
    [[nodiscard]] RenderExpected<void> attach(HWND hwnd) noexcept;

    [[nodiscard]] RenderExpected<void> resize(std::uint32_t width, std::uint32_t height,
                                              float dpiScale) noexcept;

    // Clears the background and draws the visible lines of the attached
    // Document (if any). Retries once after a device-lost recreation; a
    // second failure propagates to the caller and that frame is simply
    // skipped (the next WM_PAINT retries).
    [[nodiscard]] RenderExpected<void> render() noexcept;

    [[nodiscard]] bool isAttached() const noexcept { return m_device.has_value(); }

    // Non-owning: caller (main.cpp) must keep `doc` alive for as long as it
    // stays set here, and must call setDocument(nullptr) (or destroy this
    // RenderPipeline) before destroying the Document. nullptr detaches -
    // render() then just clears the background, matching Phase 3a's visual.
    // Safe to call before or after attach(). Non-const pointer (Phase 7l,
    // was `const document::Document*` through Phase 7k) - still purely
    // non-owning/observational in spirit, but refreshDocumentCacheIfStale()
    // now needs to call the non-const Document::takePendingEdits() to drain
    // accumulated edits for the syntax worker.
    void setDocument(document::Document* doc) noexcept { m_document = doc; }

    // Driven every frame by main.cpp's syncRenderStateAndInvalidate()
    // (renderPipeline.setTopLine(viewport.topLine())) once core::Viewport
    // exists (Phase 4b1+) - mouse-wheel/keyboard scrolling moves this. (Older
    // comment here claimed "nothing calls this besides tests" - stale since
    // Phase 4b1; corrected Phase 7o while wiring Sticky scroll, which reads
    // this value every frame via reservedTopHeightDips()/
    // stickyScrollRegionAt().) Clamped against the document's line count at
    // render() time, not here, since the document can mutate between calls.
    void setTopLine(document::LineNumber line) noexcept { m_topLine = line; }
    [[nodiscard]] document::LineNumber topLine() const noexcept { return m_topLine; }

    // WI-03: horizontal counterpart to setTopLine() - driven every frame by
    // main.cpp's syncRenderStateAndInvalidate() the same way, from
    // core::Viewport::leftColumn(). Column is a UTF-16 code-unit offset from
    // each line's start (monospace-column approximation, same convention
    // CaretDraw::column/drawIndentGuidesOnLine() already use).
    void setLeftColumn(std::uint32_t column) noexcept { m_leftColumn = column; }
    [[nodiscard]] std::uint32_t leftColumn() const noexcept { return m_leftColumn; }

    // WI-05: the tab bar's fixed DIP height (ui::TabBar::heightDips()) - a
    // native sibling HWND drawn ABOVE this class's own D2D surface (see
    // outline_pane.h-style widgets vs. this class's swap chain), so this
    // class must draw NOTHING in [0, m_tabBarHeightDips) or its own
    // breadcrumb/sticky-scroll strips would be invisibly painted underneath
    // the opaque tab strip. Folded into reservedTopHeightDips()'s return
    // value and drawBreadcrumb()/drawStickyScroll()'s own Y origins (both of
    // which hardcode absolute Y offsets rather than going through
    // reservedTopHeightDips() - see those functions' bodies). Defaults to
    // 0.0F so every code path that never calls this (measurement launch
    // modes, existing tests that construct a RenderPipeline without a
    // TabBar) keeps its pre-WI-05 layout exactly. Deliberately NOT part of
    // FrameState's coarse-frame-skip comparison (unlike m_leftColumn/
    // m_documentGeneration): set exactly once before the window is created
    // and never again for the process's lifetime, unlike those two members
    // which genuinely vary frame-to-frame - there is no "changed but not
    // detected" risk here to guard against.
    void setTabBarHeightDips(float heightDips) noexcept { m_tabBarHeightDips = heightDips; }

    // WI-07 step4: the status bar's fixed DIP height (ui::StatusBar::
    // heightDips()) - a native sibling HWND drawn BELOW this class's own D2D
    // surface, the bottom-edge counterpart to m_tabBarHeightDips above (same
    // "set exactly once before the window is created" lifecycle, same
    // reasoning for staying out of FrameState's comparison). Subtracted from
    // the available height in visibleLineRange()/widenedVisibleLineRange()
    // via reservedBottomHeightDips() below - unlike m_tabBarHeightDips this
    // is not folded into reservedTopHeightDips() (that name specifically
    // means "space reserved at the TOP"), so a second bottom-side helper
    // exists instead of overloading the same one for both edges.
    void setStatusBarHeightDips(float heightDips) noexcept { m_statusBarHeightDips = heightDips; }

    // WI-15i: DIP width reserved at the RIGHT edge for whichever right-
    // docked native sibling pane (ui::OutlinePane/ui::JsonTreePane) is
    // currently visible - the gutter's exact mirror image on the opposite
    // edge (see gutterWidthDips()). Subtracted from drawVisibleLines()'s own
    // clip (so glyphs stop being drawn into a region the pane's own opaque
    // HWND already covers - a wasted-work optimization, not a visual-bleed
    // fix; a native child HWND always paints over this class's D2D surface
    // regardless) and from visibleColumnCount() (the actual functional fix -
    // without this, the horizontal scrollbar/line-wrap math believes more
    // columns are visible than truly are, since it never learns the pane
    // exists). UNLIKE m_tabBarHeightDips/m_statusBarHeightDips above (each
    // set exactly once before the window is created and never again), this
    // value changes every time a pane is toggled open/closed - the caller
    // (normal_mode_wiring.cpp's syncRightPaneWidthDips()) must re-call this
    // at every toggle transition, not just from the WM_SIZE resize path.
    // Deliberately included in FrameState's coarse-frame-skip comparison for
    // exactly that reason - see FrameState::rightPaneWidthDips's own
    // comment.
    // WI-21b: no-op guard + wrap-conditional layout-cache invalidation added
    // (previously an unconditional one-liner) - a pane toggling open/closed
    // changes wrapWidthDips() (gutterWidthDips()/minimapWidthDips()/
    // m_rightPaneWidthDips are all wrapWidthDips() inputs), and
    // TextLayoutCache::getOrCreate() does not re-validate a cache hit
    // against the maxWidthDips it was given (text_layout_cache.h's own
    // contract) - stale wrapped layouts built at the old width would keep
    // being returned otherwise. No-op when word wrap is off (wrapWidthDips()
    // is never consulted there - see the getOrCreate() call sites in
    // drawTextLine()/hitTest()), matching every wrap-off code path staying
    // byte-identical to its pre-WI-21 behavior.
    void setRightPaneWidthDips(float widthDips) noexcept {
        if (widthDips == m_rightPaneWidthDips) {
            return;
        }
        m_rightPaneWidthDips = widthDips;
        if (m_wordWrapEnabled) {
            m_layoutCache.clear();
        }
    }

    // WI-08: changes the font family/size used by ensureTextFormat(). No-op
    // if both are already the current values (avoids needless invalidation,
    // e.g. an app-startup call that happens to match the built-in default).
    // Otherwise resets every piece of state ensureTextFormat() lazily
    // computes from the OLD font (m_textFormat itself, plus
    // m_charWidthDips/m_lineHeightDips - both measured alongside it and
    // otherwise cached forever by ensureTextFormat()'s own early-return
    // guard) so the next render() re-measures from the new font. Also clears
    // m_layoutCache: TextLayoutCache::getOrCreate() keys ONLY by line
    // number and does not re-validate a cache hit against the textFormat
    // parameter it's given (see text_layout_cache.h's own contract comment)
    // - without this clear(), stale IDWriteTextLayout objects built from the
    // old font would keep being returned. Same "store + force downstream
    // invalidation" shape as setLanguage() above.
    void setFontSettings(std::u16string fontFamily, float fontSizeDips) noexcept {
        if (fontFamily == m_fontFamily && fontSizeDips == m_fontSizeDips) {
            return;
        }
        m_fontFamily     = std::move(fontFamily);
        m_fontSizeDips   = fontSizeDips;
        m_textFormat.Reset();
        m_charWidthDips  = 0.0F;
        m_lineHeightDips = 0.0F;
        m_layoutCache.clear();
    }

    // WI-08: changes the tab width used for both indent-guide column math
    // (drawIndentGuidesOnLine()) and literal '\t' glyph rendering
    // (IDWriteTextFormat::SetIncrementalTabStop(), set inside
    // ensureTextFormat() - see that method's own comment for why this call
    // didn't exist anywhere in this codebase before WI-08). `0` is rejected
    // (SetIncrementalTabStop() requires a positive value; indent_guide_math.h
    // already treats a 0 tab width as degenerate) - same no-op guard shape
    // as setFontSettings() above. If a text format already exists, the new
    // tab stop is applied immediately (SetIncrementalTabStop() is a mutator
    // on the live IDWriteTextFormat, no recreation needed) rather than
    // waiting for some future setFontSettings()-triggered rebuild; the
    // layout cache is still cleared since cached layouts may contain '\t'
    // glyphs laid out at the old stop.
    void setTabWidth(std::uint32_t tabWidth) noexcept {
        if (tabWidth == 0 || tabWidth == m_tabWidth) {
            return;
        }
        m_tabWidth = tabWidth;
        if (m_textFormat && m_charWidthDips > 0.0F) {
            m_textFormat->SetIncrementalTabStop(static_cast<float>(m_tabWidth) * m_charWidthDips);
        }
        m_layoutCache.clear();
    }

    // WI-08: show/hide the line-number labels drawGutterOnLine() draws.
    // gutterWidthDips() below also consults this - hiding the numbers
    // shrinks the gutter back to its pre-WI-07 bookmark/fold-marker-only
    // width rather than leaving the now-empty digit-count space reserved.
    // WI-21b: no-op guard + wrap-conditional cache clear added - see
    // setRightPaneWidthDips()'s own comment for why (gutterWidthDips()
    // changing is a wrapWidthDips() input the same way).
    void setLineNumbersVisible(bool visible) noexcept {
        if (visible == m_showLineNumbers) {
            return;
        }
        m_showLineNumbers = visible;
        if (m_wordWrapEnabled) {
            m_layoutCache.clear();
        }
    }

    // WI-08: show/hide the minimap strip (drawMinimap()). minimapWidthDips()
    // below also consults this so visibleColumnCount()/minimapLeftDips()
    // reclaim the strip's width when hidden instead of leaving it reserved
    // and blank. WI-21b: no-op guard + wrap-conditional cache clear added -
    // see setRightPaneWidthDips()'s own comment for why.
    void setMinimapVisible(bool visible) noexcept {
        if (visible == m_showMinimap) {
            return;
        }
        m_showMinimap = visible;
        if (m_wordWrapEnabled) {
            m_layoutCache.clear();
        }
    }

    // WI-09: switches the color palette every ensureXxxBrush() creates from
    // (theme.h's themeForKind()). No-op if `kind` already matches - same
    // guard shape as setFontSettings()/setTabWidth() above. Otherwise resets
    // every brush via resetThemeBrushes() (shared with recreateDevice()'s
    // device-loss path below) so the next render() recreates them from the
    // new theme's colors - and because m_themeKind is part of FrameState
    // (captureFrameState() below), that next render() is guaranteed to
    // actually run rather than being coarse-frame-skipped (Phase 3c/
    // ADR-011) even if nothing else changed. Deliberately does NOT call
    // recreateDevice() - no device loss occurred, so tearing down/rebuilding
    // the whole D3D11/D2D device graph would be needless work.
    void setTheme(ThemeKind kind) noexcept {
        if (kind == m_themeKind) {
            return;
        }
        m_themeKind = kind;
        resetThemeBrushes();
    }

    // WI-21b: toggles real DirectWrite word wrapping. No-op guard + apply-
    // immediately-if-a-format-already-exists shape, same as setTabWidth()
    // above (SetWordWrapping() is a mutator on the live IDWriteTextFormat,
    // no recreation needed - unlike setFontSettings(), which must tear down
    // m_textFormat entirely since family/size aren't mutable in place).
    // Unconditionally clears the layout cache (same rationale
    // setFontSettings()/setTabWidth() already have): every cached
    // IDWriteTextLayout was built with the OLD wrapping mode + OLD width
    // (kMaxLayoutWidthDips when off, wrapWidthDips() when on - see the
    // getOrCreate() call sites in drawTextLine()/hitTest()), and
    // TextLayoutCache::getOrCreate() does not re-validate a cache hit
    // against either. Deliberately does NOT reset m_leftColumn - that is
    // core::Viewport's own authoritative state (this class only mirrors it
    // via setLeftColumn(), called fresh every frame), so resetting the
    // mirror here would just be overwritten a frame later; WI-21e's
    // Viewport::setWordWrapEnabled() is where the real reset belongs.
    void setWordWrap(bool enabled) noexcept {
        if (enabled == m_wordWrapEnabled) {
            return;
        }
        m_wordWrapEnabled = enabled;
        if (m_textFormat) {
            m_textFormat->SetWordWrapping(enabled ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        m_layoutCache.clear();
    }

    // WI-03: the length (UTF-16 code units) of the longest line among those
    // ACTUALLY drawn last frame (drawVisibleLines() updates this as a side
    // effect of its existing per-line loop, at no extra cost - lineSpan.size()
    // is already computed there). Deliberately NOT a whole-document maximum -
    // scanning the whole document would violate this codebase's 10GB-file
    // guarantee (CLAUDE.md's core value proposition; no O(document-size) scan
    // is acceptable here, matching every other "operate on the visible window
    // only" choice in this file - minimap bucketing, syntax tokens, folding).
    // main.cpp uses this as the horizontal scrollbar's nMax - an inherently
    // approximate range that updates as the user scrolls into different
    // regions of the document, an accepted editor-UX trade-off.
    [[nodiscard]] std::uint32_t maxVisibleLineLength() const noexcept { return m_maxVisibleLineLength; }

    // WI-03: how many monospace columns actually fit in the text area right
    // now (viewport_math.h::computeVisibleColumnCount(), the horizontal
    // counterpart to layoutCacheStats()-adjacent visibility queries). The
    // main.cpp app layer uses this for two purposes: (a) feeding
    // core::Viewport::setVisibleColumnCount() so ensureVisible()'s
    // right-edge auto-follow (End key, typing past the visible edge) has a
    // window size to clamp against, (b) the horizontal scrollbar's nPage
    // (thumb size / page-scroll step) in SetScrollInfo. Available width is
    // reduced by both the gutter and the minimap (m_width/m_dpiScale -
    // kGutterWidthDips - kMinimapWidthDips) - the minimap always occupies
    // the strip's width regardless of horizontal scroll position (Phase 7v/
    // 7w's "whole document overview", unaffected by leftColumn), so it's as
    // much "reserved, non-text width" as the gutter is. Returns 0 before
    // m_charWidthDips is measured (pre-first-render) or m_dpiScale isn't
    // set yet (pre-first-resize) - same "resolves itself within a frame or
    // two" tolerance computeDesiredTokenRange() already documents.
    [[nodiscard]] std::uint32_t visibleColumnCount() const noexcept;

    // The full set of cursors to draw - one caret + (optionally) one
    // selection highlight each (Phase 4b7a, generalizing Phase 4b1's
    // setCaretPosition()/Phase 4b2's setSelectionRange() from a single
    // primary-cursor value to every cursor SelectionModel holds). Not
    // core::Cursor-typed - RenderPipeline stays independent of
    // neomifes::core (same "independent, concurrently runnable engines"
    // reasoning as Viewport's header comment). The app layer builds this
    // from SelectionModel::cursors() and forwards it here in one call.
    void setCursorVisuals(std::vector<CursorVisual> cursors) noexcept {
        m_cursorVisuals = std::move(cursors);
    }

    // The full set of search-match highlights to draw (Phase 5b3a, Find bar
    // UI). Same non-owning, document::-typed-only shape as setCursorVisuals()
    // above. The app layer rebuilds and passes the whole vector after every
    // search/navigation change; empty clears all highlighting.
    void setMatchVisuals(std::vector<MatchVisual> matches) noexcept {
        m_matchVisuals = std::move(matches);
    }

    // The full set of bookmarked lines to mark in the gutter (Phase 4b8c).
    // Same non-owning shape as setMatchVisuals() above - the app layer
    // rebuilds and passes the whole vector after every toggle. Sorted
    // ascending, same convention as core::BookmarkManager::lines() (this
    // class deliberately does not depend on neomifes::core - it takes a
    // plain LineNumber vector, same "independent, concurrently runnable
    // engines" reasoning as CursorVisual/MatchVisual above).
    void setBookmarkedLines(std::vector<document::LineNumber> lines) noexcept {
        m_bookmarkedLines = std::move(lines);
    }

    // The full set of foldable regions and their current folded state
    // (Phase 7i). Same non-owning, render::-typed-only shape as
    // setBookmarkedLines() above - the app layer rebuilds and pushes the
    // whole vector after every core::FoldingModel change (toggle, or a
    // fresh region list from re-parsing the outline). Empty disables
    // folding entirely (drawVisibleLines()/hitTest() then behave exactly as
    // before Phase 7i).
    void setFoldRegions(std::vector<FoldVisual> regions) noexcept {
        m_foldRegions = std::move(regions);
    }

    // The full set of Git diff hunk markers to draw in the gutter (WI-17c).
    // Same non-owning, render::-typed-only shape as setFoldRegions() above -
    // the app layer rebuilds and pushes the whole vector after every
    // EditorSession::gitDiff() change (a manual "Git: Refresh Diff Markers"
    // command in this WI's scope, an automatic trigger in a later one). Empty
    // clears all markers (untitled buffer, not yet diffed, or outside any
    // Git repository - drawGutterOnLine() cannot and does not distinguish
    // those cases, same ambiguity EditorSession::gitDiff() itself already
    // documents).
    void setGitDiffRegions(std::vector<GitDiffMarker> markers) noexcept {
        m_gitDiffMarkers = std::move(markers);
    }

    // WI-17f: the Diff view's own Added/Removed line ranges - painted as a
    // full-line background tint (drawDiffViewLineBackground(), NOT the
    // gutter bar drawGutterOnLine() paints for GitDiffMarker above), since a
    // synthesized diff document has no gutter-marker use for hunk hints -
    // every line IS either changed or context, spelled out directly. Same
    // non-owning, whole-vector-replace shape as setGitDiffRegions() -
    // rebuilt by app::buildDiffViewLineMarkers() every time the Diff view
    // (re)opens.
    void setDiffViewLineRegions(std::vector<DiffViewLineMarker> markers) noexcept {
        m_diffViewLineMarkers = std::move(markers);
    }

    // WI-17f: true while the Diff view is showing a synthesized document
    // instead of the active session's own live one - setDocument() alone
    // can't distinguish the two (both are just `document::Document*`), and
    // several call sites (handleKeyDownEvent()/handleCharEvent()'s input-
    // blocking guard, dispatchCommand()'s auto-close-first guard) need to
    // ask "is this the Diff view right now" without needing to know
    // anything else about it. Setting `active` to false ALSO clears
    // m_diffViewLineMarkers unconditionally - closing the Diff view and
    // forgetting to also clear its markers would otherwise leave stale
    // colored backgrounds painted over whatever real document is shown
    // next, since the two pieces of state would otherwise have to be kept
    // in sync by every caller individually.
    void setDiffViewActive(bool active) noexcept {
        m_diffViewActive = active;
        if (!active) {
            m_diffViewLineMarkers.clear();
        }
    }
    [[nodiscard]] bool isDiffViewActive() const noexcept { return m_diffViewActive; }

    // WI-14c: per-document-line log severity, 1:1 with logmode::LogModel::
    // lines() (empty vector = log mode disabled for the attached document,
    // matching m_tokens/m_bookmarkedLines/m_foldRegions' own "empty means
    // off" convention). UNLIKE those smaller vectors, this one can be
    // O(document line count) - potentially millions of entries - so it is
    // deliberately NOT part of FrameState's per-frame equality comparison
    // (captureFrameState() would have to copy the whole vector every frame
    // just to compare it, a real cost against the 10GB/60fps target). Same
    // "force exactly one redraw on arrival" technique
    // applyAsyncSyntaxTokens() already uses for m_tokens (also populated
    // asynchronously, also excluded from FrameState for a related but
    // distinct reason - see that method's own comment).
    void setLogLineLevels(std::vector<logmode::LogLevel> levels) noexcept {
        m_logLineLevels = std::move(levels);
        m_lastRenderedFrameState.reset();
    }

    // WI-14c: bitmask of logmode::LogLevel values currently shown
    // (logmode::logLevelFilterBit()) - consulted by isLineHidden() together
    // with m_logLineLevels above. Unlike setLogLineLevels(), this IS part
    // of FrameState (a single byte, cheap to compare every frame) so a
    // filter-only command (no document/topLine change) still forces a
    // redraw - same "leftColumn/themeKind" treatment FrameState's own
    // comment documents for similarly small, frequently-toggled state.
    void setLogLevelFilter(std::uint8_t mask) noexcept { m_logLevelFilterMask = mask; }

    // The current IME composition to overlay-draw, or nullopt while nothing
    // is being composed (WI-06). Same non-owning, document::-typed-only
    // shape as setMatchVisuals()/setFoldRegions() above - the app layer
    // rebuilds this from MainWindow's onImeStartComposition/onImeComposition/
    // onImeEndComposition hooks and pushes the whole value each time.
    void setImeComposition(std::optional<ImeComposition> composition) noexcept {
        m_imeComposition = std::move(composition);
    }
    [[nodiscard]] const std::optional<ImeComposition>& imeComposition() const noexcept {
        return m_imeComposition;
    }

    // Client-area DEVICE PIXEL position (see MainWindow::setImeCandidatePosition()'s
    // contract - not DIPs) the app layer should anchor the IME candidate
    // window at, computed as a side effect of the last drawImeCompositionOnLine()
    // call (Phase 7w's m_maxVisibleLineLength precedent: "computed during
    // the draw walk, exposed via getter" rather than a separate query path
    // that would need to redo the same layout work). nullopt whenever there
    // is no active composition, OR the composition's anchor line has
    // scrolled off-screen this frame (drawVisibleLines() resets this at the
    // top of every call, before the draw walk - see its body) - callers
    // must not reposition the candidate window using a stale value from a
    // previous frame.
    [[nodiscard]] std::optional<POINT> imeCandidateAnchorPx() const noexcept {
        return m_imeCandidateAnchorPx;
    }

    // Enables/disables syntax-token coloring and selects which grammar to
    // parse with (Phase 7b, generalized to a language parameter in Phase 7d).
    // The app layer decides this from the open file's extension
    // (neomifes::app::detectLanguage()). nullopt disables coloring entirely
    // (m_tokens stays cleared, drawTokensOnLine() becomes a no-op) - same
    // meaning Phase 7b's `enabled=false` had. Forces m_hasCachedSnapshot
    // false so the very next render() unconditionally re-enters
    // refreshDocumentCacheIfStale()'s refresh path and (re)requests a
    // re-parse, rather than relying on Document::version() having moved - a
    // freshly-loaded Document (e.g. after openDocumentAt()) starts its own
    // independent version counter, so trusting version() alone here risks a
    // same-value coincidence across two different documents. This is also
    // (Phase 7l) how the syntax worker learns to discard whatever
    // incremental-parse tree it retained for whichever document it saw
    // before - see refreshDocumentCacheIfStale()'s forceFullReparse.
    void setLanguage(std::optional<syntax::Language> language) noexcept {
        m_language          = language;
        m_hasCachedSnapshot = false;
        // Phase 7t: a requested-range value from the PREVIOUS document is
        // meaningless once the document identity changes - forceFullReparse
        // (derived from m_hasCachedSnapshot above) would cause a re-request
        // regardless, but resetting this explicitly avoids relying on that
        // indirection for correctness.
        m_hasRequestedTokenRange = false;
        // WI-02: see m_documentGeneration's own declaration comment for why
        // FrameState::documentVersion alone cannot reliably detect a
        // document swap, and why every document-swap caller already calls
        // setLanguage() unconditionally (making this the single correct
        // place to bump it, the same reasoning that made it the right place
        // for the m_hasCachedSnapshot reset above).
        ++m_documentGeneration;
    }

    // Called once per completed background parse (Phase 7c) - main.cpp's
    // MainWindowConfig::onAppMessage hook reconstructs `tokens` from the
    // kMsgSyntaxTokensReady payload and passes it here. Resets
    // m_lastRenderedFrameState (not m_hasCachedSnapshot - this must NOT
    // trigger another re-parse) so the next render() isn't coarse-frame-
    // skipped (ADR-011): m_tokens isn't part of FrameState's comparison, so
    // without this, a token-only change could otherwise go undrawn until
    // some unrelated state also changes. Phase 7w: also populates
    // m_minimapLineColors for whatever line range m_requestedTokenRange
    // covers (populateMinimapColorsForRequestedRange()) - see that method's
    // comment for the "responses always reflect the most recently STARTED
    // request" assumption this relies on, and its known limitation under
    // rapid successive scrolling.
    void applyAsyncSyntaxTokens(std::vector<syntax::Token> tokens) noexcept;

    // Converts a client-area point (device pixels, e.g. from
    // WM_LBUTTONDOWN's lParam) to the nearest document::TextPos, using the
    // same TextLayoutCache/DPI/line-height state drawVisibleLines() already
    // maintains (Phase 4b2). Not const: a cache-miss line populates
    // m_layoutCache, same as drawVisibleLines(). nullopt if no document is
    // attached or nothing has been rendered yet (no cached snapshot to
    // hit-test against).
    [[nodiscard]] std::optional<document::TextPos> hitTest(std::int32_t xPx,
                                                            std::int32_t yPx) noexcept;

    // Hit-tests a client-area point against the gutter's fold-marker column
    // (Phase 7j). Returns the header line of a currently-drawn foldable
    // region if `xPx` falls anywhere within the gutter strip
    // ([0, gutterWidthDips())) and `yPx` resolves (via the same visible-line
    // walk hitTest() uses) to a line that is a fold header - nullopt
    // otherwise (click outside the gutter, or on a gutter row that isn't
    // foldable). Deliberately hit-tests the WHOLE gutter width for a
    // foldable row, not just the drawn chevron's ~7dip span (see
    // drawGutterOnLine()'s marker geometry for what's actually drawn) - a
    // more forgiving click target is intentional. Callers (main.cpp) check
    // this BEFORE calling hitTest(), so a fold-header gutter click never
    // also falls through to ordinary cursor placement.
    [[nodiscard]] std::optional<document::LineNumber> hitTestFoldMarker(std::int32_t xPx,
                                                                        std::int32_t yPx) noexcept;

    // Hit-tests a client-area point against the minimap strip (Phase 7v).
    // Returns the logical line under `yPx` if `xPx` falls inside
    // [minimapLeftDips(), right edge) - nullopt otherwise (click lands in
    // the text area, or the strip is too narrow to draw at all). The entry
    // point for a fresh WM_LBUTTONDOWN; drag continuation should call
    // minimapLineAtY() instead (see that method's comment on why).
    // const/noexcept, unlike hitTest()/hitTestFoldMarker() - this never
    // touches m_layoutCache.
    [[nodiscard]] std::optional<document::LineNumber> hitTestMinimap(std::int32_t xPx,
                                                                      std::int32_t yPx) const noexcept;
    // Y-only core of hitTestMinimap() - resolves `yPx` to a logical line via
    // direct proportion against the WHOLE document (line / totalLines ==
    // yDip / heightDips, Phase 7w's "whole document overview" model), not
    // any windowed/margined range. Once a minimap drag has started
    // (main.cpp's isDraggingMinimap), every subsequent WM_MOUSEMOVE should
    // keep tracking even if the cursor drifts outside the strip's X range -
    // the same convention an ordinary Win32 scrollbar thumb drag follows.
    // hitTestMinimap() itself calls this after its own X check passes.
    [[nodiscard]] std::optional<document::LineNumber> minimapLineAtY(std::int32_t yPx) const noexcept;

    // WI-07 step7: dynamic-width line-number gutter (computeGutterWidthDips(),
    // gutter_math.h) - replaces the old fixed kGutterWidthDips constant
    // (still present as this function's own minWidthDips floor) as the
    // single source of truth every x-coordinate consumer in this file must
    // agree on (same "every consumer must agree" contract kGutterWidthDips
    // itself used to document - see this constant's own header comment).
    // Computed fresh from m_document->lineCount() (O(1), a maintained
    // counter - see document.h) every call rather than cached: this class
    // has no per-frame-invalidated cache for it, and the underlying counter
    // read is cheap enough not to need one (CLAUDE.md rule 10 - no
    // benchmark motivates adding a cache here). Falls back to
    // kGutterWidthDips outright when m_document is null or m_charWidthDips
    // hasn't been measured yet (pre-first-resize) - preserves every existing
    // test's/measurement-mode's coordinate system exactly until real layout
    // info exists.
    //
    // WI-18a: moved from private to public (declaration only - the .cpp
    // definition is unchanged) so app-layer WM_CONTEXTMENU handling can
    // exclude the gutter from "is this point over real text content",
    // reusing this as the single existing source of truth for that boundary
    // rather than duplicating its computeGutterWidthDips() logic.
    [[nodiscard]] float gutterWidthDips() const noexcept;

    // Exposed for the --measure-frame harness and integration tests to
    // observe caching behavior (Phase 3c, ADR-011) - not merely test-only,
    // the frame harness reports these numbers in its JSON output.
    [[nodiscard]] TextLayoutCacheStats layoutCacheStats() const noexcept {
        return m_layoutCache.stats();
    }

private:
    // Coarse, frame-level "did anything change" snapshot (Phase 3c's
    // DamageTracker equivalent, ADR-011). No per-region information - just
    // enough to decide whether to skip the frame entirely. See render()'s
    // use of this and the ADR for the flip-model/DWM-composition safety
    // argument for why skipping a WM_PAINT-driven redraw is sound here.
    struct FrameState {
        bool                  hasDocument     = false;
        std::uint64_t         documentVersion = 0;
        // WI-02: see m_documentGeneration's declaration comment - catches a
        // document SWAP (Ctrl+O/Ctrl+N/D&D/F12/Grep-click) whose new
        // Document's version() coincidentally matches the previous cached
        // value (most commonly right after startup, both at version 0),
        // which documentVersion alone cannot distinguish from "nothing
        // changed".
        std::uint64_t         documentGeneration = 0;
        document::LineNumber  topLine         = 0;
        std::uint32_t         width = 0, height = 0;
        float                 dpiScale = 0.0F;
        // Included so caret-only movement, selection-only changes, or a
        // change in how many cursors exist (document/topLine/size
        // unchanged) still force a redraw instead of being coarse-frame-
        // skipped (Phase 4b1/4b2, generalized to N cursors in Phase 4b7a).
        std::vector<CursorVisual> cursorVisuals;
        // Same rationale as cursorVisuals above, Phase 5b3a: a match-
        // highlight-only change (new search, F3 navigation) must not be
        // coarse-frame-skipped either.
        std::vector<MatchVisual> matchVisuals;
        // Same rationale, Phase 4b8c: a bookmark toggle alone (document/
        // topLine/size unchanged) must not be coarse-frame-skipped either.
        std::vector<document::LineNumber> bookmarkedLines;
        // Same rationale, Phase 7i: a fold/unfold toggle alone (document/
        // topLine/size unchanged) must not be coarse-frame-skipped either -
        // it changes which lines are drawn.
        std::vector<FoldVisual> foldRegions;
        // WI-17c: same rationale, a Git diff refresh alone (document/topLine/
        // size unchanged) must not be coarse-frame-skipped either. Hunk-
        // granularity like bookmarkedLines/foldRegions above (bounded, not
        // one-per-line) - unlike logLevelFilterMask's own comment below about
        // m_logLineLevels, this is small enough to include directly rather
        // than force a redraw via reset() on arrival.
        std::vector<GitDiffMarker> gitDiffMarkers;
        // WI-17f: same rationale as gitDiffMarkers above - a Diff view
        // marker change alone must not be coarse-frame-skipped either.
        // Bounded (one entry per contiguous Added/Removed run), same
        // "small enough to include directly" reasoning. Does not ALSO need
        // a separate m_diffViewActive field here - every actual toggle
        // transition (open/close) also swaps m_document to a different
        // Document instance, which already forces documentGeneration to
        // differ and a redraw to happen regardless.
        std::vector<DiffViewLineMarker> diffViewLineMarkers;
        // WI-03: same rationale as topLine above - a horizontal-only scroll
        // (e.g. dragging the new horizontal scrollbar with topLine/cursor/
        // selection/etc all unchanged) must not be coarse-frame-skipped
        // either. This is the exact bug class m_documentGeneration was added
        // to fix earlier in this project's history (a mutated field NOT
        // included here silently disables redraw whenever nothing else in
        // FrameState happens to change) - leftColumn is added here the
        // moment m_leftColumn is introduced, not after the fact.
        std::uint32_t leftColumn = 0;
        // WI-15i: same rationale as leftColumn above - a right-pane-width-
        // only change (OutlinePane/JsonTreePane toggled open/closed, with
        // document/topLine/etc all otherwise unchanged) must not be coarse-
        // frame-skipped either, or the document view would keep rendering at
        // its OLD (wider or narrower) clip width until some unrelated state
        // change happened to force a real repaint. Unlike m_tabBarHeightDips/
        // m_statusBarHeightDips (deliberately excluded below, see
        // setTabBarHeightDips()'s own comment), this value is NOT set once at
        // startup and left alone - it changes every time a right-docked pane
        // is toggled, exactly the "mutated field not in FrameState silently
        // disables redraw" hazard leftColumn's own comment warns about.
        float rightPaneWidthDips = 0.0F;
        // WI-21b: same rationale as rightPaneWidthDips above, discovered
        // while writing that WI's own layoutCacheStats() invalidation tests
        // - setMinimapVisible()/setLineNumbersVisible() toggling with
        // nothing else changed was silently coarse-frame-skipped (these two
        // fields were never in FrameState), so the TextLayoutCache::clear()
        // those setters trigger while word wrap is on had no visible effect
        // until some unrelated state change forced a real redraw. Pre-WI-21b
        // this was latent (toggling minimap/gutter visibility with the rest
        // of FrameState unchanged still skipped a real redraw, silently
        // leaving the strip/gutter on screen) - it just had no test catching
        // it because nothing downstream depended on the redraw actually
        // happening promptly.
        bool showMinimap     = true;
        bool showLineNumbers = true;
        // WI-06: same rationale as leftColumn above - a composition-only
        // change (the user keeps typing into an active IME session, with
        // topLine/cursor/document all otherwise unchanged) must not be
        // coarse-frame-skipped either. See leftColumn's own comment for why
        // this bug class (a mutated field NOT included here silently
        // disabling redraw) is added proactively rather than discovered
        // later.
        std::optional<ImeComposition> imeComposition;
        // WI-09: same rationale as imeComposition above - a theme-only
        // change (setTheme() with Document/topLine/cursor/etc all
        // unchanged) must not be coarse-frame-skipped either. Without this,
        // resetThemeBrushes() would null every brush but the actual
        // rebuild-with-new-colors redraw would be skipped, leaving stale
        // pixels on screen until some unrelated state change eventually
        // forces a real repaint.
        ThemeKind themeKind = ThemeKind::Dark;
        // WI-14c: same rationale as leftColumn/themeKind above - a filter-
        // only change (no document/topLine/etc change) must not be coarse-
        // frame-skipped either. m_logLineLevels itself (the per-line
        // severity data) is deliberately NOT here - see setLogLineLevels()'s
        // own comment for why (O(document size), forces a redraw via
        // m_lastRenderedFrameState.reset() on arrival instead).
        std::uint8_t logLevelFilterMask = logmode::kAllLogLevelsVisible;

        friend bool operator==(const FrameState&, const FrameState&) = default;
    };
    [[nodiscard]] FrameState captureFrameState() const noexcept;

    // WI-09: resets the 21 device-bound brush ComPtrs, shared by
    // recreateDevice() (device context is gone) and setTheme() (brushes
    // were built from the old theme's colors) - see resetThemeBrushes()'s
    // own .cpp comment.
    void resetThemeBrushes() noexcept;

    [[nodiscard]] RenderExpected<void> recreateDevice() noexcept;
    [[nodiscard]] RenderExpected<void> refreshDocumentCacheIfStale() noexcept;
    [[nodiscard]] RenderExpected<void> ensureTextFormat() noexcept;
    [[nodiscard]] RenderExpected<void> ensureTextBrush(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> ensureSelectionBrush(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> ensureMatchBrushes(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> ensureBookmarkBrush(ID2D1DeviceContext6& dc) noexcept;
    // WI-17c: 3 solid brushes (Added/Modified/Deleted) for the Git diff
    // gutter marker (drawGutterOnLine()), same one-method-for-a-related-set
    // shape as ensureTokenBrushes()/ensureIndentGuideBrushes() below rather
    // than 3 separate ensureXxxBrush() methods.
    [[nodiscard]] RenderExpected<void> ensureGitDiffBrushes(ID2D1DeviceContext6& dc) noexcept;
    // WI-17f: 2 solid brushes (Added/Removed), built at reduced opacity so
    // text drawn on top of a full-line background fill stays readable -
    // deliberately NOT a reuse of ensureGitDiffBrushes()'s own fully-opaque
    // brushes above (those are sized for a thin 3dip gutter bar, not a
    // whole-line fill - see setDiffViewLineRegions()'s own comment for why
    // this is a wholly separate rendering path).
    [[nodiscard]] RenderExpected<void> ensureDiffViewBrushes(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7i: the fold-marker triangle's brush (drawGutterOnLine()).
    [[nodiscard]] RenderExpected<void> ensureFoldMarkerBrush(ID2D1DeviceContext6& dc) noexcept;
    // WI-07 step7: the line-number digits' brush (drawGutterOnLine()).
    [[nodiscard]] RenderExpected<void> ensureLineNumberBrush(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7b: one solid brush per colored TokenKind (Text/Variable/
    // Punctuation deliberately excluded - see tokenBrush()'s comment).
    [[nodiscard]] RenderExpected<void> ensureTokenBrushes(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7e: two brushes (regular / active) for indent guide lines.
    [[nodiscard]] RenderExpected<void> ensureIndentGuideBrushes(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7h: background brush for the Breadcrumb strip.
    [[nodiscard]] RenderExpected<void> ensureBreadcrumbBrush(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7v: background/viewport-highlight/fallback-text brushes for the
    // minimap strip (drawMinimap()). Token colors themselves reuse the
    // existing tokenBrush() palette - no new per-TokenKind brushes.
    [[nodiscard]] RenderExpected<void> ensureMinimapBrushes(ID2D1DeviceContext6& dc) noexcept;
    // WI-06: background (behind the whole composition string, occluding
    // whatever real trailing glyphs it overlaps) + target-clause-highlight
    // brushes for drawImeCompositionOnLine(). Composition TEXT itself reuses
    // m_textBrush (see drawFoldedHeaderMarker()'s identical choice for its
    // own synthesized marker text) - no separate brush needed for that part.
    [[nodiscard]] RenderExpected<void> ensureImeCompositionBrushes(ID2D1DeviceContext6& dc) noexcept;
    // WI-14c: 2 brushes (Error/Fatal share one, Warning gets the other) for
    // drawLogLevelOnLine() - see logLevelBrush()'s own comment for why only
    // 2 of LogLevel's 7 values get a dedicated color.
    [[nodiscard]] RenderExpected<void> ensureLogLevelBrushes(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7i: true if `line` sits strictly inside a currently-folded
    // m_foldRegions entry (never true for a region's own headerLine), OR
    // (WI-14c) `line` has a log severity excluded by m_logLevelFilterMask.
    // Shared by drawVisibleLines()'s line walk and hitTest()'s yDip->line
    // conversion so both agree on which lines are actually drawn/clickable -
    // folding into this single existing predicate (rather than adding a
    // second, parallel "is this line filtered" check at each of those call
    // sites) means the log-level filter automatically gets correct
    // scrolling/hit-testing behavior for free.
    [[nodiscard]] bool isLineHidden(document::LineNumber line) const noexcept;
    // WI-21c: the single source of truth for "how many on-screen rows does
    // this logical line occupy", replacing the "1 logical line = 1 drawn
    // row" assumption visibleLineRange()/drawVisibleLines() used to bake in
    // directly. 0 for a folded-hidden line (isLineHidden() above - keeps
    // that single existing predicate as the one place folding visibility is
    // decided, per its own declaration comment), 1 when word wrap is off
    // (the pre-WI-21 behavior, unconditionally - no layout is even built),
    // otherwise the actual visual_row_layout.h::computeVisualRows() count
    // for that line's real text and the current wrapWidthDips(). Builds (or
    // reuses, via TextLayoutCache) the same cached layout drawTextLine()
    // will draw a moment later, so calling this ahead of drawing is not
    // wasted work - it warms the cache entry drawTextLine() then hits.
    // Falls back to 1 if the document/DirectWrite state needed to build a
    // layout isn't ready yet (mirrors every other "not attached/measured
    // yet" degrade-to-something-safe path in this class - see
    // computeDesiredTokenRange()'s own whole-document fallback for the same
    // spirit) rather than 0, since a real (if not-yet-measured) line must
    // never be treated as occupying zero rows.
    [[nodiscard]] std::uint32_t visualRowCountForLine(document::LineNumber line) const noexcept;
    // Walks forward from `startLine`, skipping folded-hidden lines and
    // accounting for wrapped lines' real row count (visualRowCountForLine(),
    // WI-21c), until the `visibleRowOffset`-th VISUAL row on screen is
    // reached (or the document ends). Extracted from hitTest() (Phase 7j) so
    // hitTestFoldMarker() can resolve a screen row to the same logical line
    // drawVisibleLines() actually drew there, without duplicating the walk a
    // third time. WI-21d: returns {line, rowWithinLine} instead of just
    // `line` - rowWithinLine (0-based) is which of that line's own visual
    // rows visibleRowOffset landed on, needed by hitTest() to hit-test the
    // correct row of a wrapped layout instead of always row 0
    // (hitTestFoldMarker() ignores it - a fold marker is drawn once per
    // logical line regardless of how many rows it wraps into, see
    // drawGutterOnLine()'s call site in drawTextLine()). Clamps to the last
    // row of the last line if `visibleRowOffset` runs past the end of the
    // document, same clamping spirit the pre-WI-21d version had for lines.
    [[nodiscard]] std::pair<document::LineNumber, document::LineNumber> visibleLineAtRow(
        document::LineNumber startLine, document::LineNumber visibleRowOffset) const noexcept;
    // Logical line span [startLine, endLineExclusive) currently visible,
    // given m_topLine/m_height/m_lineHeightDips/m_dpiScale - the same walk
    // drawVisibleLines() has always done inline (skipping folded-hidden
    // lines, Phase 7i), extracted (Phase 7t) so computeDesiredTokenRange()
    // can share it instead of duplicating a third copy (same "extract once
    // 2 call sites exist" rule as visibleLineAtRow()'s Phase 7j precedent).
    // Returns {startLine, startLine} (empty span) if the window can't show
    // even one line yet (m_lineHeightDips not measured, m_height == 0,
    // etc.) - callers check for that themselves, same as drawVisibleLines()
    // already did via its own visibleCount==0 early return.
    [[nodiscard]] std::pair<document::LineNumber, document::LineNumber> visibleLineRange() const noexcept;
    // visibleLineRange() widened by one screenful of margin on each side
    // (viewport_math.h::widenLineRangeWithMargin(), Phase 7t - untuned
    // starting point). Extracted (Phase 7v) from computeDesiredTokenRange().
    // Phase 7w: the minimap no longer shares this - "whole document
    // overview" mode reads m_document->lineCount() directly (its window has
    // no margin/clamping concept at all, unlike the syntax-token prefetch
    // window this still serves). Sole caller is now computeDesiredTokenRange().
    [[nodiscard]] std::pair<document::LineNumber, document::LineNumber> widenedVisibleLineRange() const noexcept;
    // The document::TextRange (code-unit space) syntax tokens should cover
    // right now - widenedVisibleLineRange(), converted to offsets via
    // m_document->lineToOffset(). Falls back to the whole document if
    // layout info isn't measured yet (m_lineHeightDips <= 0 etc.) - a safe,
    // simple default for a state that resolves itself within a frame or two
    // once the window is sized.
    [[nodiscard]] document::TextRange computeDesiredTokenRange() const noexcept;
    // Phase 7t: the single place that decides whether to fire a new
    // SyntaxWorker::requestParse() - unifies two independent triggers that
    // refreshDocumentCacheIfStale() alone can't cover: (a) the document
    // changed (drained EditDeltas staged in m_pendingSyntaxEdits/
    // m_forceFullReparseNextRequest by refreshDocumentCacheIfStale(), since
    // that function returns early on a pure scroll and never reaches its
    // own body then) and (b) the visible range moved outside
    // m_requestedTokenRange even with NO document change (pure scrolling).
    // Called unconditionally from renderOnce() every frame, right after
    // refreshDocumentCacheIfStale().
    void ensureSyntaxTokensCoverVisibleRange() noexcept;
    [[nodiscard]] RenderExpected<void> renderOnce() noexcept;
    void drawVisibleLines(ID2D1DeviceContext6& dc) noexcept;
    // Draws the top-of-editor Breadcrumb strip: a background band
    // (kBreadcrumbHeightDips tall) plus the symbol path (outermost to
    // innermost, joined with " > ") containing the primary cursor's position,
    // looked up via syntax::findBreadcrumbPath() against m_cachedOutline. A
    // no-op (background only) if no cursor is primary or the path is empty
    // (Phase 7h). Called from renderOnce() after drawVisibleLines().
    void drawBreadcrumb(ID2D1DeviceContext6& dc) noexcept;
    // Draws a one-line "sticky" strip directly below the Breadcrumb band,
    // showing the header line of whichever unfolded fold region currently
    // contains topLine (i.e. we've scrolled past its header but not yet past
    // its end) - roadmap sec.7.6. A true no-op (draws nothing, reserves no
    // height - see reservedTopHeightDips()) if no such region exists, unlike
    // drawBreadcrumb()'s "always draw the background band" choice - a
    // permanently-reserved empty strip would be visual noise for a feature
    // that's only meaningful while inside some scope (Phase 7o plan's design
    // point 4). Plain text (m_textBrush only, no syntax-token coloring) -
    // same simplification drawBreadcrumb() made for its own synthesized path
    // string (Phase 7h precedent); syntax highlighting here is deliberately
    // out of scope (Phase 7o plan's scope-out section). Called from
    // renderOnce() right after drawBreadcrumb() so it visually layers on top
    // of drawVisibleLines()'s scrolled-past text underneath it.
    void drawStickyScroll(ID2D1DeviceContext6& dc) noexcept;
    // Innermost (largest headerLine) UNFOLDED region in m_foldRegions whose
    // (headerLine, endLineInclusive] span contains `topLine` - i.e. we've
    // scrolled past its header line but not yet past its last line. nullopt
    // if no such region (folding disabled, no folds, or topLine is at/above
    // every region's own headerLine). Folded regions are excluded: their
    // body lines are hidden (never drawn by drawVisibleLines()), so there is
    // nothing to have "scrolled into". Shared by drawStickyScroll() (what to
    // draw) and reservedTopHeightDips() (how much vertical space to reserve)
    // so both agree every frame (Phase 7o).
    [[nodiscard]] std::optional<FoldVisual> stickyScrollRegionAt(
        document::LineNumber topLine) const noexcept;
    // Total DIPs reserved at the top of the client area before the
    // scrollable text area starts this frame: kBreadcrumbHeightDips, plus
    // kStickyScrollHeightDips IF stickyScrollRegionAt() (at the current,
    // clamped topLine) has a value. Centralizes what was, before Phase 7o,
    // four separate hardcoded kBreadcrumbHeightDips references
    // (drawVisibleLines()'s y-origin/effective-height calc, hitTest()'s and
    // hitTestFoldMarker()'s yDip offset) - same "small shared helper used by
    // 3+ call sites" pattern as isLineHidden()/visibleLineAtRow() (Phase
    // 7i/7j).
    [[nodiscard]] float reservedTopHeightDips() const noexcept;
    // WI-07 step4: total DIPs reserved at the BOTTOM of the client area -
    // currently just m_statusBarHeightDips (see its own setter comment).
    // Trivial today, but kept as its own named function (rather than
    // reading m_statusBarHeightDips directly at each call site) so a future
    // second bottom-docked strip can extend it in one place, mirroring
    // reservedTopHeightDips()'s own shape.
    [[nodiscard]] float reservedBottomHeightDips() const noexcept { return m_statusBarHeightDips; }
    // WI-03: X-DIP offset every horizontally-scrolled X-coordinate consumer
    // in this file subtracts from its otherwise-fixed kGutterWidthDips-
    // relative position - `m_leftColumn * m_charWidthDips`, the same
    // monospace-column approximation drawIndentGuidesOnLine() already uses
    // (`level*m_tabWidth*m_charWidthDips`, WI-08). Extracted once 3+ call sites
    // needed it (drawCaretOnLine/drawSelectionOnLine/drawMatchOnLine/
    // drawIndentGuidesOnLine/hitTest()/drawTextLine()'s clip+glyph origin/
    // drawFoldedHeaderMarker's call site - 7 in total), same "extract once
    // 3+ call sites exist" precedent reservedTopHeightDips() itself set
    // (Phase 7o).
    [[nodiscard]] float leftColumnOffsetDips() const noexcept;
    // WI-08: kMinimapWidthDips if the minimap is currently visible
    // (m_showMinimap), 0.0F otherwise - so hiding the minimap
    // (setMinimapVisible(false)) reclaims its reserved width instead of
    // leaving a blank strip. Extracted once minimapLeftDips() and
    // visibleColumnCount() both needed the same visibility-gated value, same
    // "2+ call sites" rule as gutterWidthDips()'s own siblings.
    // Defined out-of-line (render_pipeline.cpp, alongside gutterWidthDips())
    // since kMinimapWidthDips is a .cpp-anonymous-namespace constant, not
    // visible to an inline header definition.
    [[nodiscard]] float minimapWidthDips() const noexcept;
    // WI-21b: the layout box width drawTextLine()/hitTest() pass to
    // TextLayoutCache::getOrCreate() when word wrap is on (kMaxLayoutWidthDips
    // is used instead when it's off - see those call sites). Reuses
    // visibleColumnCount() (already computes exactly "how many monospace
    // columns fit after subtracting the gutter/minimap/right-pane widths")
    // rather than re-deriving the same subtraction, so the wrap point stays
    // aligned to the same whole-column mental model the gutter/indent-guide/
    // horizontal-scroll math already uses - a fractional-DIP wrap point
    // would otherwise drift by a hair across resizes as m_width/m_dpiScale
    // change independently. Falls back to kMaxLayoutWidthDips (effectively
    // "don't wrap yet") when no column fits or m_charWidthDips isn't
    // measured yet (pre-first-render/degenerate-window state) - same
    // graceful-degradation shape computeVisibleColumnCount() itself already
    // has for a 0 input, rather than returning a 0-or-negative width that
    // would make CreateTextLayout()/getOrCreate() behave unpredictably.
    [[nodiscard]] float wrapWidthDips() const noexcept;
    // X-DIP offset where the minimap strip begins (kMinimapWidthDips before
    // the client-area's right edge). Extracted (Phase 7v) once drawMinimap()
    // and hitTestMinimap() both needed it - same "2nd call site" rule as
    // visibleLineAtRow()/reservedTopHeightDips(). Unlike kGutterWidthDips
    // (which every x-coordinate consumer in this file must agree on),
    // drawVisibleLines() itself does NOT need to know this offset: it draws
    // into an unbounded-width layout box same as always, and drawMinimap()
    // simply paints an opaque strip over the right edge afterward (called
    // after drawVisibleLines() in renderOnce()) - deliberately looser than
    // the gutter's contract. WI-03 (fact-check, was stale before): this
    // codebase DOES have a horizontal scroll mechanism now (m_leftColumn/
    // WM_HSCROLL), but the minimap is deliberately unaffected by it - Phase
    // 7w's "whole document overview" mode always represents [0, totalLines)
    // regardless of m_leftColumn (see drawMinimap()'s own comment), so text
    // scrolled right never needs to "peek out" from under the minimap the
    // way it needs the gutter clip (drawTextLine()'s PushAxisAlignedClip) to
    // stay fixed instead. Returns 0.0F if m_dpiScale isn't positive yet
    // (pre-first-resize) - callers compare this against kGutterWidthDips to
    // detect "too narrow to draw/hit-test" the same way.
    [[nodiscard]] float minimapLeftDips() const noexcept;
    // Draws the right-edge minimap strip: opaque background, then one
    // FillRectangle per bucket in [0, totalLines) colored by
    // minimapLineBrush() (drawMinimapLines()), then a translucent highlight
    // over the rows visibleLineRange() currently covers
    // (drawMinimapViewportHighlight()) - roadmap sec.7.4. Phase 7w: "whole
    // document overview" - the strip always represents the ENTIRE document,
    // not a window around topLine (see widenedVisibleLineRange()'s updated
    // comment on why that member no longer serves this). Deliberately a
    // direct-primitive strip, not an offscreen bitmap scaled via
    // D2D1_BITMAP_INTERPOLATION_MODE_LINEAR (that sketch's own wording
    // contradicts itself: "draw at 1/8 scale" vs "GPU-scale a full-size
    // render" are two different techniques) - same simplification
    // drawBreadcrumb()/drawStickyScroll() made versus their own roadmap
    // sketches. No-op if the strip would collide with the gutter (see
    // minimapLeftDips()). Called from renderOnce() after drawStickyScroll().
    void drawMinimap(ID2D1DeviceContext6& dc) noexcept;
    void drawMinimapLines(ID2D1DeviceContext6& dc, float left, float heightDips, float charWidthDips,
                          std::uint64_t totalLines) noexcept;
    void drawMinimapViewportHighlight(ID2D1DeviceContext6& dc, float left, float widthDips, float heightDips,
                                      std::uint64_t totalLines) noexcept;
    // Phase 7w: per-line classification for the minimap's lazily-populated
    // color cache (m_minimapLineColors) - a CLASSIFICATION (not a stored
    // brush), so it survives device loss untouched; resolved to an actual
    // brush at draw time via minimapBrushForState(). Mirrors tokenBrush()'s
    // TokenKind grouping exactly (Keyword/Type/String/Number/Comment/
    // Preprocessor get their own state, Text/Variable/Punctuation all
    // collapse to PlainText) - std::uint8_t-based so 1,000,000 lines costs
    // ~1MB (m_tokens itself deliberately stays windowed, Phase 7t/7u, to
    // avoid the 130-200MB a full-document token vector would cost; this
    // per-line summary is the cheap alternative that makes "whole document
    // overview" affordable).
    enum class MinimapLineColorState : std::uint8_t {
        Unpopulated,  // never covered by applyAsyncSyntaxTokens() since the last document-version clear
        PlainText,    // covered, has content, but no colored token overlaps it
        Keyword,
        Type,
        String,
        Number,
        Comment,
        Preprocessor,  // covered, first colored token's kind
    };
    [[nodiscard]] static MinimapLineColorState classifyTokenKindForMinimap(syntax::TokenKind kind) noexcept;
    [[nodiscard]] ID2D1SolidColorBrush* minimapBrushForState(MinimapLineColorState state) noexcept;
    // The [start, end) offset span of `line`'s own content, excluding its
    // trailing '\n' - shared by drawMinimapLines()'s per-bucket lookup and
    // populateMinimapColorsForRequestedRange()'s per-line classification
    // loop (Phase 7w), same convention extractLineText() established
    // (Phase 7o).
    [[nodiscard]] document::TextRange minimapLineSpan(document::LineNumber line,
                                                       std::uint64_t         totalLines) const noexcept;
    // First colored token overlapping [lineStart, lineEnd) among the tokens
    // m_tokens holds RIGHT NOW (valid only while populating -
    // populateMinimapColorsForRequestedRange() calls this immediately after
    // m_tokens has been replaced by applyAsyncSyntaxTokens() with tokens
    // covering exactly the contiguous range being swept over, same
    // monotonic-tokenCursor-sweep contract drawTokensOnLine() uses).
    [[nodiscard]] MinimapLineColorState classifyLineForMinimap(document::TextPos lineStart,
                                                                document::TextPos lineEnd,
                                                                std::size_t&       tokenCursor) noexcept;
    // Fills m_minimapLineColors for the line range m_requestedTokenRange
    // covers, called from applyAsyncSyntaxTokens(). No-op if no request has
    // ever been made, or m_minimapLineColors's size doesn't match the
    // current document's line count (a stale response arriving after a
    // version-triggered clear/resize - see refreshDocumentCacheIfStale()).
    // Known limitation: under rapid successive scrolling, a stale response
    // (started before, delivered after a newer request overwrote
    // m_requestedTokenRange) can transiently populate the wrong line range;
    // this self-heals once the newer request's own response arrives. Fixing
    // this properly needs a generation counter on SyntaxWorker's request/
    // response payload - out of scope here (m_tokens itself has the same
    // unaddressed limitation already).
    void populateMinimapColorsForRequestedRange() noexcept;
    // Color for line `line`'s minimap bar: m_minimapLineColors[line]
    // resolved to a brush (minimapBrushForState()), or nullptr for a truly
    // empty line ([lineStart,lineEnd) empty - draw nothing, same convention
    // Phase 7v established). Unlike Phase 7v's version, this does NOT sweep
    // m_tokens directly - m_tokens only ever covers the visible+prefetch
    // window (Phase 7t), never arbitrary bucket-sampled lines scattered
    // across the whole document, so a per-frame m_tokens sweep is
    // structurally the wrong tool here (Phase 7w).
    [[nodiscard]] ID2D1SolidColorBrush* minimapLineBrush(document::LineNumber line, document::TextPos lineStart,
                                                          document::TextPos lineEnd) noexcept;
    // Raw text of logical line `line` (no trailing '\n'), extracted from
    // m_cachedSnapshot. Caller must have already verified m_document/
    // m_cachedSnapshot are non-null (same precondition convention as this
    // class's other m_cachedSnapshot-reading helpers). Factored out of
    // drawVisibleLines()'s inline per-line splitting loop (Phase 7o) so
    // drawStickyScroll() can fetch a single arbitrary (usually off-screen)
    // line's text without re-deriving that loop.
    [[nodiscard]] std::u16string extractLineText(document::LineNumber line) const noexcept;

    // Precomputed line/column for one cursor's caret (Phase 4b1, N-cursor
    // generalization Phase 4b7a). A line outside the visible range simply
    // never matches inside drawCaretsOnLine()'s per-line loop.
    struct CaretDraw {
        document::LineNumber line;
        std::uint32_t         column;
        std::uint32_t         virtualColumnOffset = 0;  // Phase 4b8e, see CursorVisual
    };
    // One offsetToLine()/lineToOffset() pair per cursor in m_cursorVisuals,
    // done once per frame rather than once per (visible line x cursor) pair
    // - pulled out of drawVisibleLines() to keep its cognitive complexity
    // down (Phase 4b7a; same rationale as main.cpp's dispatchMouseDown()/
    // handleClipboardKey() extractions).
    [[nodiscard]] std::vector<CaretDraw> computeCaretDraws() const noexcept;
    // Fetches (or creates) `line`'s cached layout and draws its full visible
    // content in order: match/selection highlights, indent guides, syntax
    // tokens, glyphs, carets, gutter, and (Phase 7i) a folded-header marker
    // if `line` is a folded region's header. A layout-cache miss for this
    // line is a no-op (matches the pre-Phase-3c tolerance of a single line
    // silently failing to draw). Pulled out of drawVisibleLines() to keep
    // its cognitive complexity down (Phase 7i; same rationale as
    // computeCaretDraws()'s extraction, Phase 4b7a) - called once per
    // visible (non-folded-hidden) line.
    void drawTextLine(ID2D1DeviceContext6& dc, document::LineNumber line, float y,
                      std::u16string_view lineSpan, document::TextPos lineStart, document::TextPos lineEnd,
                      const std::vector<CaretDraw>& caretDraws, std::size_t& tokenCursor) noexcept;
    // Draws whichever of `caretDraws` land on `line`, at vertical offset
    // `y` within `layout`. Called from drawVisibleLines() per visible line,
    // after DrawTextLayout so carets render on top of the glyphs.
    void drawCaretsOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                         document::LineNumber line, const std::vector<CaretDraw>& caretDraws) noexcept;
    // Draws a translucent highlight rectangle for every m_cursorVisuals
    // selection range that overlaps [lineStart, lineEnd), at vertical
    // offset `y` within `layout`. Called from drawVisibleLines() BEFORE
    // DrawTextLayout for the current visible line, so highlights sit
    // behind the glyphs (Phase 4b2, N-cursor generalization Phase 4b7a).
    void drawSelectionsOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                             document::TextPos lineStart, document::TextPos lineEnd) noexcept;
    // WI-17f: fills the FULL LINE WIDTH (not a layout-relative column range
    // like drawMatchesOnLine()/drawSelectionsOnLine() below - m_diffViewLine
    // Markers classify whole lines, not sub-ranges) with a translucent
    // Added/Removed tint if `line` falls inside one of m_diffViewLineMarkers'
    // ranges, at vertical offset `y`. No-op (and no ensureDiffViewBrushes()
    // call) when m_diffViewLineMarkers is empty - the common case, since
    // this state only exists while the Diff view is open. Called from
    // drawTextLine() BEFORE drawMatchesOnLine() below - a whole-line
    // background sits beneath everything else (selection/match highlights,
    // glyphs), the same "background elements paint first" ordering that
    // function's own declaration comment documents for the two calls
    // immediately after it.
    void drawDiffViewLineBackground(ID2D1DeviceContext6& dc, float y, document::LineNumber line,
                                    float widthDips) noexcept;
    // Draws a translucent highlight rectangle for every m_matchVisuals entry
    // that overlaps [lineStart, lineEnd), at vertical offset `y` within
    // `layout`. Called from drawVisibleLines() BEFORE drawSelectionsOnLine()
    // (Phase 5b3a) - matches sit visually behind an active text selection,
    // which itself sits behind the glyphs.
    void drawMatchesOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                           document::TextPos lineStart, document::TextPos lineEnd) noexcept;
    // WI-21d: shared by drawSelectionOnLine()/drawMatchOnLine() below -
    // [startColumn, endColumn) of `layout` can span more than one visual row
    // once word wrap is on, and a single X-to-X rectangle (the pre-WI-21d
    // shape, built from two HitTestTextPosition() calls that silently
    // discarded their own Y output) draws a meaningless rectangle connecting
    // two different rows' X coordinates when that happens. HitTestTextRange()
    // is DirectWrite's purpose-built range-hit-test API: it returns one
    // DWRITE_HIT_TEST_METRICS per contiguous run within a single visual row
    // the range touches, so a range spanning N rows yields N metrics/rects -
    // exactly the correctly-shaped highlight this class was missing. Uses
    // the same 2-call sizing pattern as GetLineMetrics()
    // (visual_row_layout.h::computeVisualRows()) and every other "ask
    // DirectWrite how many, then fetch them" call in this codebase. Returns
    // rects already translated to `y` + the gutter/leftColumn origin
    // (gutterWidthDips() - leftColumnOffsetDips(), same offset
    // drawCaretOnLine() applies) - ready to FillRectangle() directly.
    [[nodiscard]] std::vector<D2D1_RECT_F> rowRectsForColumnRange(
        IDWriteTextLayout& layout, float y, std::uint32_t startColumn,
        std::uint32_t endColumn) const noexcept;
    // Draws a thin solid caret bar at `column` (UTF-16 code units into the
    // line) within `layout`, at vertical offset `y`, shifted right by
    // `virtualColumnOffset` * m_charWidthDips if nonzero (Phase 4b8e - an
    // approximation that assumes the fixed-pitch font this pipeline already
    // requires, see ensureTextFormat()'s Consolas comment; not correct for a
    // proportional font). Called from drawCaretsOnLine() for whichever
    // visible line a caret is on, reusing that line's already-fetched layout
    // and m_textBrush (Phase 4b1). WI-21d: now also honors
    // HitTestTextPosition()'s own Y/height output instead of discarding it -
    // `column` can land on any wrapped row of `layout`, not just row 0, once
    // word wrap is on.
    void drawCaretOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                         std::uint32_t column, std::uint32_t virtualColumnOffset) noexcept;
    // Draws a translucent highlight rectangle spanning [startColumn,
    // endColumn) of `layout`, at vertical offset `y`. Called from
    // drawSelectionsOnLine() once per overlapping selection range. WI-21d:
    // now draws one rectangle per visual row the range spans (via
    // rowRectsForColumnRange() above) instead of a single row-0-shaped
    // rectangle - see that method's own comment.
    void drawSelectionOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                             std::uint32_t startColumn, std::uint32_t endColumn) noexcept;
    // Draws a translucent highlight rectangle spanning [startColumn,
    // endColumn) of `layout`, at vertical offset `y`, using m_matchBrush or
    // m_currentMatchBrush depending on `isCurrent`. Called from
    // drawMatchesOnLine() once per overlapping match range (Phase 5b3a).
    // WI-21d: same rowRectsForColumnRange()-based multi-row fix as
    // drawSelectionOnLine() above.
    void drawMatchOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                         std::uint32_t startColumn, std::uint32_t endColumn, bool isCurrent) noexcept;
    // Draws the gutter strip's per-line contents at vertical offset `y`:
    // the 1-based line number (WI-07 step7, right-aligned, drawn first so
    // the two markers below layer on top of it), a bookmark dot if `line`
    // is bookmarked (Phase 4b8c), and a fold-header chevron if `line` is a
    // fold header (Phase 7i). Called from drawVisibleLines() once per
    // visible line.
    void drawGutterOnLine(ID2D1DeviceContext6& dc, float y, document::LineNumber line) noexcept;
    // Applies a per-token-kind DrawingEffect brush to `layout` for whichever
    // m_tokens overlap [lineStart, lineEnd) (Phase 7b). Called from
    // drawVisibleLines() BEFORE DrawTextLayout() (the effect must be set on
    // the layout before it's drawn) - unlike drawSelectionsOnLine()/
    // drawMatchesOnLine(), this isn't a background rectangle, so ordering
    // relative to those two doesn't matter, only relative to DrawTextLayout.
    // Re-applied every frame regardless of TextLayoutCache hit/miss, using
    // the CURRENT frame's brushes: SetDrawingEffect() is a cheap metadata
    // write (no reshape), and this deliberately avoids baking device-bound
    // ID2D1Brush pointers into a layout that TextLayoutCache keeps alive
    // across device loss (ADR-011 - see this file's top-of-class comment on
    // m_layoutCache not being cleared in recreateDevice()).
    //
    // `tokenCursor` is threaded in/out by the caller across successive calls
    // within one frame's line loop: visible lines are visited in increasing
    // document order and m_tokens is sorted the same way (parseCpp()
    // guarantees this - see syntax_syntax_test.cpp's
    // TokensAreOrderedLeftToRightAndNonOverlapping), so a single forward
    // sweep across the whole visible range costs O(tokens overlapping the
    // viewport) total, not O(visible lines x total document tokens).
    void drawTokensOnLine(IDWriteTextLayout& layout, document::TextPos lineStart,
                          document::TextPos lineEnd, std::size_t& tokenCursor) noexcept;
    // nullptr for TokenKind::Text/Variable/Punctuation (deliberately
    // unstyled - they fall through to DrawTextLayout()'s default brush,
    // m_textBrush, exactly like a run with no DrawingEffect set at all).
    [[nodiscard]] ID2D1SolidColorBrush* tokenBrush(syntax::TokenKind kind) noexcept;
    // WI-14c: colors an entire line's text by log severity (unlike
    // tokenBrush(), which colors sub-ranges within a line). nullptr for
    // Trace/Debug/Info/Unknown - this WI's MVP scope only visually
    // distinguishes Error/Fatal (logError) and Warning (logWarning); the
    // other 4 values fall back to m_textBrush unmodified, same "no
    // DrawingEffect means the default brush" contract tokenBrush() already
    // documents.
    [[nodiscard]] ID2D1SolidColorBrush* logLevelBrush(logmode::LogLevel level) noexcept;
    // Sets `layout`'s entire text range to logLevelBrush(m_logLineLevels
    // [line])'s color, if `line` has an entry and it maps to a non-null
    // brush - a no-op line (log mode disabled, or a severity with no
    // dedicated color) draws exactly as if this were never called. Must run
    // BEFORE DrawTextLayout() draws `layout` (SetDrawingEffect() is a
    // layout-mutation call, same ordering constraint drawTokensOnLine()
    // documents) - called from drawTextLine() right after
    // drawTokensOnLine() so a log-severity color always wins over any
    // (in practice absent, since log files aren't language-syntax-
    // highlighted) overlapping token color.
    void drawLogLevelOnLine(IDWriteTextLayout& layout, document::LineNumber line, document::TextPos lineStart,
                            document::TextPos lineEnd) noexcept;
    // Draws one thin vertical line per indent-guide level computed from
    // lineSpan's leading whitespace (Phase 7e, indent_guide_math.h), at
    // vertical offset `y`. Called from drawVisibleLines() alongside
    // drawMatchesOnLine()/drawSelectionsOnLine() (background element, before
    // DrawTextLayout). `isActiveLine` selects m_activeIndentGuideBrush
    // (brighter) over m_indentGuideBrush - true for whichever line(s)
    // computeCaretDraws() places a cursor on; this is a simplified
    // "highlight this one line's guides" approximation of VSCode's "active
    // indent guide" (the real feature highlights the guide across the whole
    // enclosing scope, which needs block-range detection this codebase does
    // not have yet - see the Phase 7e plan's Context section).
    void drawIndentGuidesOnLine(ID2D1DeviceContext6& dc, float y, std::u16string_view lineSpan,
                                bool isActiveLine) noexcept;
    // Draws a short " {...}" marker at (x, y) standing in for a folded
    // region's hidden body (Phase 7i). One-off IDWriteTextLayout, same
    // disposable-layout pattern drawBreadcrumb() uses for its short,
    // per-frame-synthesized string (TextLayoutCache is keyed by line number
    // for real document lines, not for this kind of synthesized fragment).
    // Called from drawVisibleLines() right after a folded header line's own
    // DrawTextLayout call, `x` positioned past that line's measured width.
    void drawFoldedHeaderMarker(ID2D1DeviceContext6& dc, float x, float y) noexcept;
    // Draws the current m_imeComposition (if any) as an OVERLAY on top of
    // `realLineLayout`'s already-drawn glyphs - not spliced into them (see
    // ImeComposition's own header comment for why true reflow was rejected,
    // WI-06). No-op unless m_imeComposition's anchorRange starts on `line`.
    // Uses `realLineLayout`'s HitTestTextPosition() (same call
    // drawCaretOnLine() makes) to find where the composition text should
    // begin - a fresh, disposable IDWriteTextLayout (same pattern
    // drawFoldedHeaderMarker() uses; this string is transient IME state,
    // never part of TextLayoutCache) is then built for the composition
    // string itself, underlined via SetUnderline(), with an opaque
    // background sized to its own measured width (sized to itself, not to
    // whatever real text it overlaps - an accepted, documented overlay
    // trade-off) and a translucent highlight behind targetClauseRange if
    // present. Also updates m_imeCandidateAnchorPx as a side effect (Phase
    // 7w's m_maxVisibleLineLength precedent: computed once during the draw
    // walk that already has every coordinate needed, rather than a second
    // query path redoing the same layout work). Called from drawTextLine()
    // right after drawCaretsOnLine(), inside the same clip region (this is
    // text-derived content, same as the glyphs it overlays).
    void drawImeCompositionOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& realLineLayout, float y,
                                  document::LineNumber line, document::TextPos lineStart) noexcept;

    HWND                         m_hwnd     = nullptr;
    std::uint32_t                m_width    = 0;
    std::uint32_t                m_height   = 0;
    float                        m_dpiScale = 1.0F;
    std::optional<RenderDevice>  m_device;

    // Document -> Render change notification (ADR-010, detailed_design.md
    // sec.4.3/4.4): snapshot() is only called from refreshDocumentCacheIfStale()
    // when m_document->version() has moved past m_cachedDocumentVersion, never
    // once per frame unconditionally.
    document::Document*                               m_document              = nullptr;
    bool                                              m_hasCachedSnapshot     = false;
    std::uint64_t                                     m_cachedDocumentVersion = 0;
    // Bumped by setLanguage() (see its own comment - every document-swap
    // caller calls setLanguage() unconditionally, WI-02's Ctrl+O/Ctrl+N/D&D
    // included). FrameState::documentGeneration below exists because
    // FrameState::documentVersion alone is NOT enough to detect a document
    // swap: a freshly-loaded Document starts its own independent version
    // counter (Document::version()), so a brand-new document can coincide
    // with the previous document's cached version number - most commonly
    // right after startup, where both the initial empty Document and a
    // just-opened file's Document sit at version 0. When that coincidence
    // lines up with every other FrameState field also being unchanged (same
    // topLine/cursor/selection/etc, plausible right after Ctrl+O on a fresh
    // window), render()'s coarse frame-skip (ADR-011) would otherwise treat
    // the swap as "nothing changed" and never actually draw the new content
    // until some UNRELATED state change (e.g. a resize from moving the
    // window) forced a real render() call - the exact bug reported during
    // WI-02 dogfooding. A monotonically increasing counter can never repeat,
    // so this coincidence is now structurally impossible.
    std::uint64_t                                     m_documentGeneration    = 0;
    std::shared_ptr<const document::BufferSnapshot>   m_cachedSnapshot;
    document::LineNumber                              m_topLine               = 0;
    // WI-03: horizontal scroll state, same lifecycle as m_topLine above -
    // driven by setLeftColumn(), read by every X-coordinate consumer via
    // leftColumnOffsetDips(). m_maxVisibleLineLength is a side-effect
    // output (not scroll state itself) updated by drawVisibleLines()'s
    // existing per-line loop - see maxVisibleLineLength()'s comment for why
    // this stays windowed rather than a whole-document scan.
    std::uint32_t                                     m_leftColumn            = 0;
    std::uint32_t                                     m_maxVisibleLineLength  = 0;
    // WI-05: see setTabBarHeightDips()'s own comment.
    float                                              m_tabBarHeightDips     = 0.0F;
    // WI-07 step4: see setStatusBarHeightDips()'s own comment.
    float                                              m_statusBarHeightDips  = 0.0F;
    // WI-15i: see setRightPaneWidthDips()'s own comment.
    float                                              m_rightPaneWidthDips   = 0.0F;
    std::vector<CursorVisual>                         m_cursorVisuals;  // empty: no cursors to draw
    std::vector<MatchVisual>                          m_matchVisuals;   // empty: no match highlights (Phase 5b3a)
    std::vector<document::LineNumber>                 m_bookmarkedLines;  // empty: no bookmarks (Phase 4b8c)
    std::vector<FoldVisual>                           m_foldRegions;      // empty: folding disabled (Phase 7i)
    std::vector<GitDiffMarker>                        m_gitDiffMarkers;   // empty: no diff data (WI-17c)
    std::vector<DiffViewLineMarker>                   m_diffViewLineMarkers;  // empty: Diff view closed, or open with no changes to highlight (WI-17f)
    bool                                               m_diffViewActive       = false;  // WI-17f: see isDiffViewActive()'s own comment
    // WI-14c: see setLogLineLevels()'s own comment for why this can be
    // O(document size) and is deliberately excluded from FrameState.
    std::vector<logmode::LogLevel>                    m_logLineLevels;    // empty: log mode disabled
    std::uint8_t                                       m_logLevelFilterMask = logmode::kAllLogLevelsVisible;
    // WI-06: nullopt - no active IME composition (the common case). See
    // setImeComposition()/drawImeCompositionOnLine().
    std::optional<ImeComposition>                     m_imeComposition;
    // WI-06: reset to nullopt at the top of every drawVisibleLines() call,
    // set by drawImeCompositionOnLine() as a side effect - see
    // imeCandidateAnchorPx()'s own doc comment for why a stale value must
    // never survive past the frame that produced it.
    std::optional<POINT>                              m_imeCandidateAnchorPx;
    // Phase 7b/7c/7d: gate + cache for syntax-token coloring.
    // refreshDocumentCacheIfStale() clears m_tokens and fires an async
    // SyntaxWorker::requestParse() when this has a value and the document
    // version moved; applyAsyncSyntaxTokens() repopulates m_tokens once
    // that request completes (see both functions' comments).
    std::optional<syntax::Language>                    m_language;
    std::vector<syntax::Token>                         m_tokens;
    // Phase 7w: lazily-populated per-line minimap color cache (whole-
    // document overview mode) - see MinimapLineColorState's comment.
    // Resized/cleared to m_document->lineCount() * Unpopulated whenever
    // refreshDocumentCacheIfStale() detects a document-version change (or a
    // forced full reparse) - the simplest possible edit-tracking strategy
    // (no EditDelta-based shifting, CLAUDE.md rule 10), same "wholesale
    // invalidation" spirit as m_layoutCache.clear() just above it.
    std::vector<MinimapLineColorState>                 m_minimapLineColors;
    // Phase 7h: symbol tree for Breadcrumb, recomputed alongside m_tokens
    // inside refreshDocumentCacheIfStale() - SYNCHRONOUSLY (not via
    // m_syntaxWorker), see that function's comment for why. Cleared whenever
    // m_tokens is (document change or m_language reset to nullopt).
    std::vector<syntax::OutlineNode>                   m_cachedOutline;
    // Phase 7c: lazily constructed inside refreshDocumentCacheIfStale() on
    // the first actual parse request (needs a valid m_hwnd - see that
    // function's comment for why it isn't constructed in setLanguage()
    // instead). Never constructed at all for the --measure-* launch modes,
    // which never enable syntax highlighting.
    std::optional<SyntaxWorker>                        m_syntaxWorker;
    // Phase 7t: staged by refreshDocumentCacheIfStale() (which can return
    // early on a pure scroll and never reach its own body - see
    // setLanguage()'s forceFullReparse), consumed by
    // ensureSyntaxTokensCoverVisibleRange() the same frame or a later one.
    // Accumulates (never overwritten) the same way SyntaxWorker's own
    // m_pendingEdits does, for the same reason (an edit must never be
    // silently dropped).
    std::vector<document::EditDelta> m_pendingSyntaxEdits;
    bool                              m_forceFullReparseNextRequest = false;
    // What was last actually REQUESTED from the worker (not what m_tokens
    // currently holds - SyntaxWorker processes requests strictly in order
    // on a single background thread, so a request-time value alone is
    // sufficient to avoid redundant re-requests; no separate "what m_tokens
    // covers" bookkeeping is needed - see ensureSyntaxTokensCoverVisibleRange()).
    document::TextRange m_requestedTokenRange{};
    bool                 m_hasRequestedTokenRange = false;

    // m_textFormat/m_dwriteFactory are DPI-independent (DIPs) and survive
    // device loss; m_textBrush/m_selectionBrush are bound to the device
    // context and must be reset whenever the device is (re)created
    // (recreateDevice()/attach()).
    Microsoft::WRL::ComPtr<IDWriteFactory7>       m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>     m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_textBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_selectionBrush;
    // Phase 5b3a: separate brushes for ordinary vs. "current" (F3-navigated-
    // to) match highlights, same device-bound reset lifecycle as
    // m_selectionBrush above.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_matchBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_currentMatchBrush;
    // Phase 4b8c: the bookmark gutter dot's brush, same device-bound reset
    // lifecycle as the brushes above.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_bookmarkBrush;
    // WI-17c: the Git diff gutter marker's 3 brushes, same device-bound
    // reset lifecycle as the brushes above.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_diffAddedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_diffModifiedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_diffDeletedBrush;
    // WI-17f: the Diff view's own 2 full-line-background brushes (Added/
    // Removed), same device-bound reset lifecycle as the brushes above -
    // deliberately separate ComPtrs from m_diffAddedBrush/m_diffDeletedBrush
    // above (built at reduced opacity, see ensureDiffViewBrushes()'s own
    // comment for why sharing those would be wrong).
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_diffViewAddedBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_diffViewRemovedBrush;
    // Phase 7i: fold-marker triangle brush, same device-bound reset
    // lifecycle as the brushes above. See ensureFoldMarkerBrush()/drawGutterOnLine().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_foldMarkerBrush;
    // WI-07 step7: line-number digits' brush, same device-bound reset
    // lifecycle as the brushes above. See ensureLineNumberBrush()/drawGutterOnLine().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_lineNumberBrush;
    // Phase 7b: one brush per colored TokenKind, same device-bound reset
    // lifecycle as the brushes above. See ensureTokenBrushes()/tokenBrush().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_keywordBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_typeBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_stringBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_numberBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_commentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_preprocessorBrush;
    // Phase 7e: indent guide line brushes, same device-bound reset lifecycle
    // as the brushes above. See ensureIndentGuideBrushes()/drawIndentGuidesOnLine().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_indentGuideBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_activeIndentGuideBrush;
    // Phase 7h: Breadcrumb strip background, same device-bound reset
    // lifecycle as the brushes above. See ensureBreadcrumbBrush()/drawBreadcrumb().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_breadcrumbBackgroundBrush;
    // Phase 7v: minimap strip brushes, same device-bound reset lifecycle as
    // the brushes above. See ensureMinimapBrushes()/drawMinimap().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_minimapBackgroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_minimapViewportBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_minimapTextBrush;
    // Phase 7w: distinct dimmer gray for a line that has never been covered
    // by an async syntax-token response yet ("not computed" placeholder) -
    // visually distinguishable from m_minimapTextBrush (a line that WAS
    // covered but had no colored token to show).
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_minimapUnpopulatedBrush;
    // WI-06: IME composition overlay brushes, same device-bound reset
    // lifecycle as the brushes above. See ensureImeCompositionBrushes()/
    // drawImeCompositionOnLine().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_imeCompositionBackgroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_imeTargetClauseBrush;
    // WI-14c: same device-bound reset lifecycle as the brushes above. See
    // ensureLogLevelBrushes()/logLevelBrush()/drawLogLevelOnLine().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_logErrorBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_logWarningBrush;
    float                                          m_lineHeightDips = 0.0F;  // 0 == not yet measured
    // Phase 4b8e: one fixed-pitch character's advance width, probed once
    // alongside m_lineHeightDips (see ensureTextFormat()) - drawCaretOnLine()
    // uses it to approximate free-cursor virtual-column positions.
    float                                          m_charWidthDips  = 0.0F;  // 0 == not yet measured

    // WI-08: font family/size ensureTextFormat() builds m_textFormat from.
    // Defaults match the pre-WI-08 hardcoded values, so any code path that
    // never calls setFontSettings() (every existing test, --measure-* launch
    // modes) keeps its exact prior appearance.
    std::u16string m_fontFamily   = u"Consolas";
    float          m_fontSizeDips = 14.0F;
    // WI-08: tab width for both drawIndentGuidesOnLine()'s column math and
    // ensureTextFormat()'s SetIncrementalTabStop() call. Default matches the
    // pre-WI-08 hardcoded kTabWidth constants this replaces.
    std::uint32_t m_tabWidth = 4;
    // WI-08: gutterWidthDips()/drawGutterOnLine() and minimapWidthDips()/
    // drawMinimap() visibility gates. Both default true (identical to every
    // pre-WI-08 frame, which always drew both).
    bool m_showLineNumbers = true;
    bool m_showMinimap     = true;
    // WI-21b:折り返し(word wrap). See setWordWrap()/wrapWidthDips() below -
    // default false matches the pre-WI-21 hardcoded DWRITE_WORD_WRAPPING_NO_WRAP
    // exactly, so any code path that never calls setWordWrap() (every
    // existing test, --measure-* launch modes) keeps its exact prior
    // appearance.
    bool m_wordWrapEnabled = false;

    // WI-09: the currently-applied theme. themeForKind(m_themeKind) is what
    // every ensureXxxBrush() and renderOnce()'s background dc->Clear() now
    // read from. Default matches core::Settings::themeName's own default
    // (u"dark") so any code path that never calls setTheme() (every
    // existing test, --measure-* launch modes) keeps its exact prior
    // appearance.
    ThemeKind m_themeKind = ThemeKind::Dark;

    // Line-keyed IDWriteTextLayout cache (Phase 3c, ADR-011). Also not
    // device-bound (unlike m_textBrush) - NOT cleared in recreateDevice().
    // Cleared wholesale only when refreshDocumentCacheIfStale() detects a
    // Document::version() change. WI-21c: mutable - visualRowCountForLine()
    // (called from the const-qualified visibleLineRange()) populates this
    // cache via getOrCreate() as a pure memoization side effect (same
    // "logically read-only, physically caches" contract every other memoized
    // getter in this codebase uses mutable for) - it never changes what
    // visibleLineRange() reports, only how expensively the answer is
    // computed the next time.
    mutable TextLayoutCache m_layoutCache;

    // nullopt means "no successful frame yet, or the device was just
    // (re)created" - either way the next render() must draw unconditionally
    // rather than risk skipping into an uninitialized/stale back buffer.
    std::optional<FrameState> m_lastRenderedFrameState;
};

}  // namespace neomifes::render
