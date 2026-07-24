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
#include <utility>
#include <vector>

#include "neomifes/document/text_pos.h"
#include "neomifes/render/render_device.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/text_layout_cache.h"
// Phase 7b: m_tokens below needs syntax::Token's complete type (it's a
// std::vector member, not a pointer), even though no syntax:: type appears
// in this class's public method signatures. See src/render/CMakeLists.txt's
// comment on why neomifes::syntax is linked PUBLIC despite that.
#include "neomifes/syntax/syntax.h"

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
    // Safe to call before or after attach().
    void setDocument(const document::Document* doc) noexcept { m_document = doc; }

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

    // Enables/disables C++ syntax-token coloring (Phase 7b). The app layer
    // decides this from the open file's extension (neomifes::app::isCppSourceFile())
    // - no general per-language dispatch exists yet, see syntax.h's TokenKind
    // comment. Forces m_hasCachedSnapshot false so the very next render()
    // unconditionally re-enters refreshDocumentCacheIfStale()'s refresh path
    // and (re)computes m_tokens, rather than relying on Document::version()
    // having moved - a freshly-loaded Document (e.g. after openDocumentAt())
    // starts its own independent version counter, so trusting version() alone
    // here risks a same-value coincidence across two different documents.
    void setSyntaxHighlightingEnabled(bool enabled) noexcept {
        m_syntaxHighlightingEnabled = enabled;
        m_hasCachedSnapshot         = false;
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
    // Phase 7b: one solid brush per colored TokenKind (Text/Variable/
    // Punctuation deliberately excluded - see tokenBrush()'s comment).
    [[nodiscard]] RenderExpected<void> ensureTokenBrushes(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> renderOnce() noexcept;
    void drawVisibleLines(ID2D1DeviceContext6& dc) noexcept;

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

    HWND                         m_hwnd     = nullptr;
    std::uint32_t                m_width    = 0;
    std::uint32_t                m_height   = 0;
    float                        m_dpiScale = 1.0F;
    std::optional<RenderDevice>  m_device;

    // Document -> Render change notification (ADR-010, detailed_design.md
    // sec.4.3/4.4): snapshot() is only called from refreshDocumentCacheIfStale()
    // when m_document->version() has moved past m_cachedDocumentVersion, never
    // once per frame unconditionally.
    const document::Document*                        m_document              = nullptr;
    bool                                              m_hasCachedSnapshot     = false;
    std::uint64_t                                     m_cachedDocumentVersion = 0;
    std::shared_ptr<const document::BufferSnapshot>   m_cachedSnapshot;
    document::LineNumber                              m_topLine               = 0;
    std::vector<CursorVisual>                         m_cursorVisuals;  // empty: no cursors to draw
    std::vector<MatchVisual>                          m_matchVisuals;   // empty: no match highlights (Phase 5b3a)
    std::vector<document::LineNumber>                 m_bookmarkedLines;  // empty: no bookmarks (Phase 4b8c)
    // Phase 7b: gate + cache for C++ syntax-token coloring. m_tokens is
    // recomputed (synchronously, via syntax::parseCpp()) inside
    // refreshDocumentCacheIfStale() alongside m_cachedSnapshot - see that
    // function and setSyntaxHighlightingEnabled() above.
    bool                                               m_syntaxHighlightingEnabled = false;
    std::vector<syntax::Token>                         m_tokens;

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
    // Phase 7b: one brush per colored TokenKind, same device-bound reset
    // lifecycle as the brushes above. See ensureTokenBrushes()/tokenBrush().
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_keywordBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_typeBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_stringBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_numberBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_commentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_preprocessorBrush;
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
