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
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/document/text_pos.h"
#include "neomifes/render/render_device.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/syntax_worker.h"
#include "neomifes/render/text_layout_cache.h"
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

    // No interactive scroll input exists yet (Editor Core/Viewport is Phase
    // 4) - nothing in this codebase calls this besides tests today. Exists
    // only as the documented Phase 4 hook (detailed_design.md sec.4.4 pt.3).
    // Clamped against the document's line count at render() time, not here,
    // since the document can mutate between calls.
    void setTopLine(document::LineNumber line) noexcept { m_topLine = line; }
    [[nodiscard]] document::LineNumber topLine() const noexcept { return m_topLine; }

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
    }

    // Called once per completed background parse (Phase 7c) - main.cpp's
    // MainWindowConfig::onAppMessage hook reconstructs `tokens` from the
    // kMsgSyntaxTokensReady payload and passes it here. Resets
    // m_lastRenderedFrameState (not m_hasCachedSnapshot - this must NOT
    // trigger another re-parse) so the next render() isn't coarse-frame-
    // skipped (ADR-011): m_tokens isn't part of FrameState's comparison, so
    // without this, a token-only change could otherwise go undrawn until
    // some unrelated state also changes.
    void applyAsyncSyntaxTokens(std::vector<syntax::Token> tokens) noexcept {
        m_tokens = std::move(tokens);
        m_lastRenderedFrameState.reset();
    }

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
    // ([0, kGutterWidthDips)) and `yPx` resolves (via the same visible-line
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

        friend bool operator==(const FrameState&, const FrameState&) = default;
    };
    [[nodiscard]] FrameState captureFrameState() const noexcept;

    [[nodiscard]] RenderExpected<void> recreateDevice() noexcept;
    [[nodiscard]] RenderExpected<void> refreshDocumentCacheIfStale() noexcept;
    [[nodiscard]] RenderExpected<void> ensureTextFormat() noexcept;
    [[nodiscard]] RenderExpected<void> ensureTextBrush(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> ensureSelectionBrush(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> ensureMatchBrushes(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> ensureBookmarkBrush(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7i: the fold-marker triangle's brush (drawGutterOnLine()).
    [[nodiscard]] RenderExpected<void> ensureFoldMarkerBrush(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7b: one solid brush per colored TokenKind (Text/Variable/
    // Punctuation deliberately excluded - see tokenBrush()'s comment).
    [[nodiscard]] RenderExpected<void> ensureTokenBrushes(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7e: two brushes (regular / active) for indent guide lines.
    [[nodiscard]] RenderExpected<void> ensureIndentGuideBrushes(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7h: background brush for the Breadcrumb strip.
    [[nodiscard]] RenderExpected<void> ensureBreadcrumbBrush(ID2D1DeviceContext6& dc) noexcept;
    // Phase 7i: true if `line` sits strictly inside a currently-folded
    // m_foldRegions entry (never true for a region's own headerLine).
    // Shared by drawVisibleLines()'s line walk and hitTest()'s yDip->line
    // conversion so both agree on which lines are actually drawn/clickable.
    [[nodiscard]] bool isLineHidden(document::LineNumber line) const noexcept;
    // Walks forward from `startLine`, skipping folded-hidden lines, until
    // the `visibleRowOffset`-th VISIBLE line is reached (or the document
    // ends). Extracted from hitTest() (Phase 7j) so hitTestFoldMarker() can
    // resolve a screen row to the same logical line drawVisibleLines()
    // actually drew there, without duplicating the walk a third time.
    [[nodiscard]] document::LineNumber visibleLineAtRow(
        document::LineNumber startLine, document::LineNumber visibleRowOffset) const noexcept;
    [[nodiscard]] RenderExpected<void> renderOnce() noexcept;
    void drawVisibleLines(ID2D1DeviceContext6& dc) noexcept;
    // Draws the top-of-editor Breadcrumb strip: a background band
    // (kBreadcrumbHeightDips tall) plus the symbol path (outermost to
    // innermost, joined with " > ") containing the primary cursor's position,
    // looked up via syntax::findBreadcrumbPath() against m_cachedOutline. A
    // no-op (background only) if no cursor is primary or the path is empty
    // (Phase 7h). Called from renderOnce() after drawVisibleLines().
    void drawBreadcrumb(ID2D1DeviceContext6& dc) noexcept;

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
    // Draws a translucent highlight rectangle for every m_matchVisuals entry
    // that overlaps [lineStart, lineEnd), at vertical offset `y` within
    // `layout`. Called from drawVisibleLines() BEFORE drawSelectionsOnLine()
    // (Phase 5b3a) - matches sit visually behind an active text selection,
    // which itself sits behind the glyphs.
    void drawMatchesOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                           document::TextPos lineStart, document::TextPos lineEnd) noexcept;
    // Draws a thin solid caret bar at `column` (UTF-16 code units into the
    // line) within `layout`, at vertical offset `y`, shifted right by
    // `virtualColumnOffset` * m_charWidthDips if nonzero (Phase 4b8e - an
    // approximation that assumes the fixed-pitch font this pipeline already
    // requires, see ensureTextFormat()'s Consolas comment; not correct for a
    // proportional font). Called from drawCaretsOnLine() for whichever
    // visible line a caret is on, reusing that line's already-fetched layout
    // and m_textBrush (Phase 4b1).
    void drawCaretOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                         std::uint32_t column, std::uint32_t virtualColumnOffset) noexcept;
    // Draws a translucent highlight rectangle spanning [startColumn,
    // endColumn) of `layout`, at vertical offset `y`. Called from
    // drawSelectionsOnLine() once per overlapping selection range.
    void drawSelectionOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                             std::uint32_t startColumn, std::uint32_t endColumn) noexcept;
    // Draws a translucent highlight rectangle spanning [startColumn,
    // endColumn) of `layout`, at vertical offset `y`, using m_matchBrush or
    // m_currentMatchBrush depending on `isCurrent`. Called from
    // drawMatchesOnLine() once per overlapping match range (Phase 5b3a).
    void drawMatchOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                         std::uint32_t startColumn, std::uint32_t endColumn, bool isCurrent) noexcept;
    // Fills a small bookmark dot in the gutter strip ([0, kGutterWidthDips))
    // at vertical offset `y` if `line` is bookmarked (Phase 4b8c). Called
    // from drawVisibleLines() once per visible line - deliberately minimal
    // (no line numbers, no folding arrows - see bookmark_manager.h's file
    // header for why the full "Line Gutter" feature stays a separate,
    // already-deferred future phase).
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
    std::shared_ptr<const document::BufferSnapshot>   m_cachedSnapshot;
    document::LineNumber                              m_topLine               = 0;
    std::vector<CursorVisual>                         m_cursorVisuals;  // empty: no cursors to draw
    std::vector<MatchVisual>                          m_matchVisuals;   // empty: no match highlights (Phase 5b3a)
    std::vector<document::LineNumber>                 m_bookmarkedLines;  // empty: no bookmarks (Phase 4b8c)
    std::vector<FoldVisual>                           m_foldRegions;      // empty: folding disabled (Phase 7i)
    // Phase 7b/7c/7d: gate + cache for syntax-token coloring.
    // refreshDocumentCacheIfStale() clears m_tokens and fires an async
    // SyntaxWorker::requestParse() when this has a value and the document
    // version moved; applyAsyncSyntaxTokens() repopulates m_tokens once
    // that request completes (see both functions' comments).
    std::optional<syntax::Language>                    m_language;
    std::vector<syntax::Token>                         m_tokens;
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
    // Phase 7i: fold-marker triangle brush, same device-bound reset
    // lifecycle as the brushes above. See ensureFoldMarkerBrush()/drawGutterOnLine().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_foldMarkerBrush;
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
    float                                          m_lineHeightDips = 0.0F;  // 0 == not yet measured
    // Phase 4b8e: one fixed-pitch character's advance width, probed once
    // alongside m_lineHeightDips (see ensureTextFormat()) - drawCaretOnLine()
    // uses it to approximate free-cursor virtual-column positions.
    float                                          m_charWidthDips  = 0.0F;  // 0 == not yet measured

    // Line-keyed IDWriteTextLayout cache (Phase 3c, ADR-011). Also not
    // device-bound (unlike m_textBrush) - NOT cleared in recreateDevice().
    // Cleared wholesale only when refreshDocumentCacheIfStale() detects a
    // Document::version() change.
    TextLayoutCache m_layoutCache;

    // nullopt means "no successful frame yet, or the device was just
    // (re)created" - either way the next render() must draw unconditionally
    // rather than risk skipping into an uninitialized/stale back buffer.
    std::optional<FrameState> m_lastRenderedFrameState;
};

}  // namespace neomifes::render
