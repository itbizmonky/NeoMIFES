#include "neomifes/render/render_pipeline.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/render/d2d_factories.h"
#include "neomifes/render/indent_guide_math.h"
#include "neomifes/render/resize_math.h"
#include "neomifes/render/viewport_math.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::render {

namespace {
using document::LineNumber;
using document::TextPos;
using document::TextRange;

// Fixed, window-size-independent bound rather than the live client width:
// keeps layout cache entries valid across resize() (a resize never needs to
// invalidate this cache), same spirit as ensureTextFormat()'s 4096-DIP
// probe-layout box. Shared by drawVisibleLines() and hitTest() (Phase 4b2)
// since both fetch line layouts through the same TextLayoutCache.
constexpr float kMaxLayoutWidthDips  = 65536.0F;
constexpr float kMaxLayoutHeightDips = 65536.0F;

// Phase 4b8c: minimal bookmark-only gutter width. Every x-coordinate
// consumer in this file (DrawTextLayout's origin, hitTest()'s xDip, and the
// three draw*OnLine() rect-builders below) must agree on this offset - see
// drawCaretOnLine()/drawSelectionOnLine()/drawMatchOnLine()'s comments for
// why HitTestTextPosition()'s layout-local coordinates do not automatically
// inherit DrawTextLayout()'s origin shift.
constexpr float kGutterWidthDips  = 24.0F;
constexpr float kBookmarkDotSizeDips = 8.0F;

// Phase 7h: top-of-editor Breadcrumb strip height. Same "every y-coordinate
// consumer in this file must agree on this offset" contract kGutterWidthDips
// documents for the x-axis - see drawVisibleLines()'s `y` origin and
// hitTest()'s `yDip` clamp below.
constexpr float kBreadcrumbHeightDips = 24.0F;
}  // namespace

RenderExpected<void> RenderPipeline::attach(HWND hwnd) noexcept {
    RECT rect{};
    if (::GetClientRect(hwnd, &rect) == 0) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached,
                                           .hr = HRESULT_FROM_WIN32(::GetLastError())});
    }
    m_hwnd     = hwnd;
    m_width    = static_cast<std::uint32_t>(rect.right - rect.left);
    m_height   = static_cast<std::uint32_t>(rect.bottom - rect.top);
    m_dpiScale = dpiToScale(::GetDpiForWindow(hwnd));

    auto device = RenderDevice::create(m_hwnd, m_width, m_height);
    if (!device) {
        return std::unexpected(device.error());
    }
    m_device = std::move(*device);
    m_device->setDpi(m_dpiScale);
    m_textBrush.Reset();       // stale binding to whatever device context existed before
    m_selectionBrush.Reset();
    m_matchBrush.Reset();
    m_currentMatchBrush.Reset();
    return {};
}

RenderExpected<void> RenderPipeline::resize(std::uint32_t width, std::uint32_t height,
                                            float dpiScale) noexcept {
    m_width    = width;
    m_height   = height;
    m_dpiScale = dpiScale;
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }
    auto result = m_device->resize(width, height);
    if (!result && result.error().isDeviceLost()) {
        return recreateDevice();
    }
    if (result) {
        m_device->setDpi(m_dpiScale);
    }
    return result;
}

RenderPipeline::FrameState RenderPipeline::captureFrameState() const noexcept {
    return FrameState{
        .hasDocument     = m_document != nullptr,
        .documentVersion = m_document != nullptr ? m_document->version() : 0,
        .topLine         = m_topLine,
        .width           = m_width,
        .height          = m_height,
        .dpiScale        = m_dpiScale,
        .cursorVisuals   = m_cursorVisuals,
        .matchVisuals    = m_matchVisuals,
        .bookmarkedLines = m_bookmarkedLines,
        .foldRegions     = m_foldRegions,
    };
}

RenderExpected<void> RenderPipeline::render() noexcept {
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }

    // Coarse frame-level skip (Phase 3c, ADR-011): if nothing observable has
    // changed since the last successful frame, skip beginFrame/Clear/
    // drawVisibleLines/endFrame entirely. Safe under the FLIP_DISCARD swap
    // effect + DWM composition (the compositor retains the last presented
    // frame independently of this process; MainWindow::handlePaint() already
    // calls ValidateRect() unconditionally regardless of what the paint
    // handler does, so this cannot cause a WM_PAINT repost loop).
    const FrameState current = captureFrameState();
    if (m_lastRenderedFrameState && *m_lastRenderedFrameState == current) {
        return {};
    }

    auto result = renderOnce();
    if (!result && result.error().isDeviceLost()) {
        auto recreated = recreateDevice();
        if (!recreated) {
            return recreated;
        }
        if (!m_device) {
            // recreateDevice() reported success but left m_device empty -
            // should not happen, but stay honest rather than dereferencing.
            return std::unexpected(
                RenderError{.stage = RenderStage::NotAttached, .hr = E_UNEXPECTED});
        }
        result = renderOnce();
    }
    if (result) {
        m_lastRenderedFrameState = current;
    }
    return result;
}

RenderExpected<void> RenderPipeline::recreateDevice() noexcept {
    m_device.reset();
    m_textBrush.Reset();       // bound to the device context that just went away
    m_selectionBrush.Reset();
    m_matchBrush.Reset();
    m_currentMatchBrush.Reset();
    m_bookmarkBrush.Reset();
    m_keywordBrush.Reset();
    m_typeBrush.Reset();
    m_stringBrush.Reset();
    m_numberBrush.Reset();
    m_commentBrush.Reset();
    m_preprocessorBrush.Reset();
    m_indentGuideBrush.Reset();
    m_activeIndentGuideBrush.Reset();
    m_breadcrumbBackgroundBrush.Reset();
    m_foldMarkerBrush.Reset();
    // A freshly (re)created swap chain's back buffer is uninitialized - the
    // next render() must not treat "nothing logically changed" as license to
    // skip drawing into it.
    m_lastRenderedFrameState.reset();
    auto device = RenderDevice::create(m_hwnd, m_width, m_height);
    if (!device) {
        return std::unexpected(device.error());
    }
    m_device = std::move(*device);
    m_device->setDpi(m_dpiScale);
    return {};
}

RenderExpected<void> RenderPipeline::refreshDocumentCacheIfStale() noexcept {
    if (m_document == nullptr) {
        m_hasCachedSnapshot = false;
        m_cachedSnapshot.reset();
        m_tokens.clear();
        m_cachedOutline.clear();
        return {};
    }
    if (m_hasCachedSnapshot && m_document->version() == m_cachedDocumentVersion) {
        return {};
    }
    // The one and only Document::snapshot() call site in the render layer -
    // gated on version() having moved, per detailed_design.md sec.4.3's
    // "don't call snapshot() every frame" guardrail (ADR-010).
    m_cachedSnapshot        = m_document->snapshot();
    m_cachedDocumentVersion = m_document->version();
    m_hasCachedSnapshot     = true;
    // Wholesale invalidation - the only granularity available without a
    // per-region change source (ADR-011). Every line's layout is stale once
    // the document has mutated at all, since Document::version() carries no
    // range information.
    m_layoutCache.clear();
    // Phase 7c: clear immediately, re-tokenize off the UI thread. m_tokens
    // is cleared (not left showing the previous parse) because this is
    // still a full-document re-parse, not true tree-sitter incremental
    // diffing (see syntax_worker.h) - after ANY edit, every existing
    // token's offset can be wrong, so drawing them would risk coloring the
    // wrong characters. applyAsyncSyntaxTokens() repopulates m_tokens once
    // SyntaxWorker's background parse completes; until then the text falls
    // back to the default (uncolored) brush, a deliberate, documented
    // deviation from roadmap sec.7.9's "keep showing old tokens" sketch
    // (which assumes true incremental parsing, not implemented yet).
    m_tokens.clear();
    m_cachedOutline.clear();
    if (m_language.has_value()) {
        // Lazily started here (not setLanguage()) because that can be called
        // before RenderPipeline::attach() has set m_hwnd (main.cpp calls it
        // right after wireNormalMode(), before window.create() runs) -
        // refreshDocumentCacheIfStale() is only ever reached from render(),
        // which requires a live m_device/m_hwnd already, so m_hwnd is
        // guaranteed valid here. --measure-frame/-startup/-memory never
        // enable syntax highlighting at all, so they never pay for an idle
        // background thread either way.
        if (!m_syntaxWorker.has_value()) {
            m_syntaxWorker.emplace(m_hwnd);
        }
        m_syntaxWorker->requestParse(m_cachedSnapshot, *m_language);
        // Phase 7h (Breadcrumb): SYNCHRONOUS, unlike token coloring above -
        // extractOutline() is already a deliberately independent, separate
        // parse from token coloring (outline.h's header comment), so this
        // does not share SyntaxWorker's background thread. Recomputing it
        // asynchronously as well would need either a second background
        // worker or extending SyntaxWorker to also produce outlines - a
        // materially bigger scope increase with no benchmark yet showing the
        // synchronous cost actually matters (CLAUDE.md rule 10). Same
        // "ship synchronous first" order Phase 7b (sync tokens) took before
        // Phase 7c (async) was justified by a measured stutter. Known,
        // accepted limitation: on a very large file, Breadcrumb can lag one
        // frame behind the edit that invalidated it.
        m_cachedOutline = syntax::extractOutline(
            m_cachedSnapshot->extract(TextRange{.start = 0, .end = m_cachedSnapshot->length()}), *m_language);
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureTextFormat() noexcept {
    if (m_textFormat) {
        return {};
    }
    auto factory = sharedDWriteFactory();
    if (!factory) {
        return std::unexpected(factory.error());
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    constexpr float kFontSizeDips = 14.0F;
    HRESULT hr = (*factory)->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                              kFontSizeDips, L"en-us", format.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }
    // Promoted to a member (Phase 3c) so drawVisibleLines() can hand it to
    // TextLayoutCache::getOrCreate() on a cache miss without re-querying the
    // process-wide singleton every frame.
    m_dwriteFactory = *factory;

    // The default DWRITE_WORD_WRAPPING_WRAP would silently break the fixed
    // topLine*lineHeight row layout drawVisibleLines() relies on (a long
    // line would wrap and push every following row down instead of being
    // clipped at the client edge).
    hr = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }

    // Probe line height via a throwaway layout: representative string, a
    // generously large layout box so nothing clips/wraps during the probe.
    Microsoft::WRL::ComPtr<IDWriteTextLayout> probeLayout;
    hr = (*factory)->CreateTextLayout(L"Ag", 2, format.Get(), 4096.0F, 4096.0F,
                                      probeLayout.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }
    DWRITE_LINE_METRICS metrics{};
    UINT32              actualCount = 0;
    hr = probeLayout->GetLineMetrics(&metrics, 1, &actualCount);
    if (FAILED(hr) || actualCount == 0) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }

    // Reuses the same "Ag" probe layout (Phase 4b8e) - the X coordinate
    // right after the first character equals that character's advance
    // width, since HitTestTextPosition() is layout-local (origin 0,0). Only
    // meaningful because ensureTextFormat() requires Consolas (fixed-pitch);
    // see drawCaretOnLine()'s comment for where this is consumed.
    DWRITE_HIT_TEST_METRICS charMetrics{};
    float                    charX = 0.0F;
    float                    charY = 0.0F;
    hr = probeLayout->HitTestTextPosition(1, FALSE, &charX, &charY, &charMetrics);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = hr});
    }

    m_textFormat     = std::move(format);
    m_lineHeightDips = metrics.height;
    m_charWidthDips  = charX;
    return {};
}

RenderExpected<void> RenderPipeline::ensureTextBrush(ID2D1DeviceContext6& dc) noexcept {
    if (m_textBrush) {
        return {};
    }
    constexpr D2D1_COLOR_F kTextColor = {220.0F / 255.0F, 220.0F / 255.0F, 220.0F / 255.0F, 1.0F};
    const HRESULT hr = dc.CreateSolidColorBrush(kTextColor, m_textBrush.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureSelectionBrush(ID2D1DeviceContext6& dc) noexcept {
    if (m_selectionBrush) {
        return {};
    }
    // Windows' conventional selection blue (RGB 0,120,215), translucent so
    // glyphs drawn on top (drawVisibleLines() draws the highlight before
    // DrawTextLayout) stay legible.
    constexpr D2D1_COLOR_F kSelectionColor = {0.0F / 255.0F, 120.0F / 255.0F, 215.0F / 255.0F, 0.4F};
    const HRESULT hr = dc.CreateSolidColorBrush(kSelectionColor, m_selectionBrush.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureMatchBrushes(ID2D1DeviceContext6& dc) noexcept {
    if (!m_matchBrush) {
        // Translucent yellow (RGB 255,220,0) - the conventional "found text"
        // highlight color (Notepad++/VSCode Find), distinct enough from the
        // selection blue above to layer visibly underneath an active
        // selection. R channel written as 1.0F directly (not 255.0F/255.0F)
        // since that self-division trips clang-tidy's misc-redundant-expression.
        constexpr D2D1_COLOR_F kMatchColor = {1.0F, 220.0F / 255.0F, 0.0F / 255.0F, 0.35F};
        const HRESULT hr = dc.CreateSolidColorBrush(kMatchColor, m_matchBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_currentMatchBrush) {
        // More saturated orange (RGB 255,140,0) for the "active" (F3-
        // navigated-to) match, so it stands out among many highlighted
        // matches. R channel written as 1.0F, see kMatchColor's comment above.
        constexpr D2D1_COLOR_F kCurrentMatchColor = {1.0F, 140.0F / 255.0F, 0.0F / 255.0F, 0.55F};
        const HRESULT hr = dc.CreateSolidColorBrush(kCurrentMatchColor, m_currentMatchBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureBookmarkBrush(ID2D1DeviceContext6& dc) noexcept {
    if (!m_bookmarkBrush) {
        // Solid red (RGB 220,20,20) - the conventional bookmark/marker dot
        // color (VSCode's own bookmark extensions, MIFES's marker column).
        constexpr D2D1_COLOR_F kBookmarkColor = {220.0F / 255.0F, 20.0F / 255.0F, 20.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kBookmarkColor, m_bookmarkBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureFoldMarkerBrush(ID2D1DeviceContext6& dc) noexcept {
    if (!m_foldMarkerBrush) {
        // Neutral gray (RGB 150,150,150) - same "hardcoded, no Theme system
        // yet" rationale as ensureIndentGuideBrushes()/ensureBreadcrumbBrush().
        constexpr D2D1_COLOR_F kFoldMarkerColor = {150.0F / 255.0F, 150.0F / 255.0F, 150.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kFoldMarkerColor, m_foldMarkerBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureTokenBrushes(ID2D1DeviceContext6& dc) noexcept {
    // Phase 7b: VSCode Dark+-inspired palette, chosen for contrast against
    // this pipeline's existing kBackgroundColor (RGB 30,30,30, see
    // renderOnce()) and kTextColor (RGB 220,220,220, see ensureTextBrush()).
    // Hardcoded (no Theme system exists in this codebase yet - see the
    // Phase 7b plan's Context section) - a future user-configurable theme
    // would replace these constants, not this brush-creation shape.
    if (!m_keywordBrush) {
        constexpr D2D1_COLOR_F kKeywordColor = {86.0F / 255.0F, 156.0F / 255.0F, 214.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kKeywordColor, m_keywordBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_typeBrush) {
        constexpr D2D1_COLOR_F kTypeColor = {78.0F / 255.0F, 201.0F / 255.0F, 176.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kTypeColor, m_typeBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_stringBrush) {
        constexpr D2D1_COLOR_F kStringColor = {206.0F / 255.0F, 145.0F / 255.0F, 120.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kStringColor, m_stringBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_numberBrush) {
        constexpr D2D1_COLOR_F kNumberColor = {181.0F / 255.0F, 206.0F / 255.0F, 168.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kNumberColor, m_numberBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_commentBrush) {
        constexpr D2D1_COLOR_F kCommentColor = {106.0F / 255.0F, 153.0F / 255.0F, 85.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kCommentColor, m_commentBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_preprocessorBrush) {
        constexpr D2D1_COLOR_F kPreprocessorColor = {197.0F / 255.0F, 134.0F / 255.0F, 192.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kPreprocessorColor, m_preprocessorBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

ID2D1SolidColorBrush* RenderPipeline::tokenBrush(syntax::TokenKind kind) noexcept {
    switch (kind) {
        case syntax::TokenKind::Keyword:      return m_keywordBrush.Get();
        case syntax::TokenKind::Type:         return m_typeBrush.Get();
        case syntax::TokenKind::String:       return m_stringBrush.Get();
        case syntax::TokenKind::Number:       return m_numberBrush.Get();
        case syntax::TokenKind::Comment:      return m_commentBrush.Get();
        case syntax::TokenKind::Preprocessor: return m_preprocessorBrush.Get();
        // Text/Variable/Punctuation deliberately unstyled - see this
        // function's declaration comment in render_pipeline.h.
        case syntax::TokenKind::Text:
        case syntax::TokenKind::Variable:
        case syntax::TokenKind::Punctuation:
            return nullptr;
    }
    return nullptr;  // unreachable, every TokenKind enumerator handled above
}

RenderExpected<void> RenderPipeline::ensureIndentGuideBrushes(ID2D1DeviceContext6& dc) noexcept {
    // Phase 7e: VSCode Dark+-inspired editorIndentGuide.background/
    // activeBackground approximations, same "hardcoded, no Theme system yet"
    // rationale as ensureTokenBrushes() above.
    if (!m_indentGuideBrush) {
        constexpr D2D1_COLOR_F kIndentGuideColor = {62.0F / 255.0F, 62.0F / 255.0F, 62.0F / 255.0F, 1.0F};
        const HRESULT hr = dc.CreateSolidColorBrush(kIndentGuideColor, m_indentGuideBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    if (!m_activeIndentGuideBrush) {
        constexpr D2D1_COLOR_F kActiveIndentGuideColor = {110.0F / 255.0F, 110.0F / 255.0F, 110.0F / 255.0F, 1.0F};
        const HRESULT hr =
            dc.CreateSolidColorBrush(kActiveIndentGuideColor, m_activeIndentGuideBrush.GetAddressOf());
        if (FAILED(hr)) {
            return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
        }
    }
    return {};
}

RenderExpected<void> RenderPipeline::ensureBreadcrumbBrush(ID2D1DeviceContext6& dc) noexcept {
    // Phase 7h: VSCode Dark+-inspired editor breadcrumb background
    // approximation, same "hardcoded, no Theme system yet" rationale as
    // ensureTokenBrushes()/ensureIndentGuideBrushes() above - slightly
    // lighter than the editor background (RGB 30,30,30) so the strip reads
    // as distinct chrome.
    if (m_breadcrumbBackgroundBrush) {
        return {};
    }
    constexpr D2D1_COLOR_F kBreadcrumbBackgroundColor = {37.0F / 255.0F, 37.0F / 255.0F, 38.0F / 255.0F, 1.0F};
    const HRESULT hr =
        dc.CreateSolidColorBrush(kBreadcrumbBackgroundColor, m_breadcrumbBackgroundBrush.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }
    return {};
}

void RenderPipeline::drawVisibleLines(ID2D1DeviceContext6& dc) noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F ||
        !m_dwriteFactory) {
        return;
    }

    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return;
    }
    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);

    // Phase 7h: the Breadcrumb strip occupies the top kBreadcrumbHeightDips
    // of the client area, so the effective height available to text lines is
    // reduced by that many (DPI-scaled) pixels - mirrors kGutterWidthDips'
    // effect on drawn width, just on the y-axis. computeVisibleLineCount()
    // itself stays a general-purpose pure function unaware of Breadcrumb.
    const auto breadcrumbHeightPx = static_cast<std::uint32_t>(kBreadcrumbHeightDips * m_dpiScale);
    const std::uint32_t effectiveHeightPx = m_height > breadcrumbHeightPx ? m_height - breadcrumbHeightPx : 0;
    const std::uint32_t visibleCount = computeVisibleLineCount(effectiveHeightPx, m_dpiScale, m_lineHeightDips);
    if (visibleCount == 0) {
        return;
    }
    // Phase 7i: walk forward counting only VISIBLE (non-folded-hidden) lines,
    // so a screen holding `visibleCount` rows can span more than
    // `visibleCount` logical lines when folds are active. m_foldRegions
    // empty (folding disabled/no folds yet) makes this identical to the
    // pre-Phase-7i arithmetic (every logical line counts as visible).
    LineNumber    endLineExclusive = startLine;
    std::uint32_t visibleSeen      = 0;
    while (endLineExclusive < totalLines && visibleSeen < visibleCount) {
        if (!isLineHidden(endLineExclusive)) {
            ++visibleSeen;
        }
        ++endLineExclusive;
    }

    const TextPos startOffset = m_document->lineToOffset(startLine);
    const TextPos endOffset   = (endLineExclusive >= totalLines)
                                     ? m_cachedSnapshot->length()
                                     : m_document->lineToOffset(endLineExclusive);
    const std::u16string text =
        m_cachedSnapshot->extract(TextRange{.start = startOffset, .end = endOffset});

    const std::vector<CaretDraw> caretDraws = computeCaretDraws();
    std::size_t tokenCursor = 0;  // Phase 7b: threaded forward across the line loop, see drawTokensOnLine()'s comment

    std::u16string_view remaining(text);
    float                y         = kBreadcrumbHeightDips;  // Phase 7h: reserve the strip above
    TextPos              lineStart = startOffset;
    for (LineNumber line = startLine; line < endLineExclusive; ++line) {
        const auto newlinePos = remaining.find(u'\n');
        const std::u16string_view lineSpan =
            (newlinePos == std::u16string_view::npos) ? remaining : remaining.substr(0, newlinePos);
        const TextPos lineEnd = lineStart + lineSpan.size();

        // Phase 7i: hidden lines (inside a folded region, not its header)
        // are walked (to advance lineStart/remaining correctly) but never
        // drawn, and y does not advance for them - the next visible line
        // simply lands where this one would have.
        if (!isLineHidden(line)) {
            drawTextLine(dc, line, y, lineSpan, lineStart, lineEnd, caretDraws, tokenCursor);
            y += m_lineHeightDips;
        }
        if (newlinePos == std::u16string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;  // +1 for the '\n' this line's span excluded
        remaining = remaining.substr(newlinePos + 1);
    }
}

void RenderPipeline::drawTextLine(ID2D1DeviceContext6& dc, LineNumber line, float y,
                                  std::u16string_view lineSpan, TextPos lineStart, TextPos lineEnd,
                                  const std::vector<CaretDraw>& caretDraws,
                                  std::size_t& tokenCursor) noexcept {
    const auto layoutResult =
        m_layoutCache.getOrCreate(line, lineSpan, *m_dwriteFactory.Get(), *m_textFormat.Get(),
                                  kMaxLayoutWidthDips, kMaxLayoutHeightDips);
    // A layout-creation failure for a single line is no worse than the
    // pre-Phase-3c behavior of DrawText() silently failing per-call - it
    // skips just that line, not the whole frame.
    if (!layoutResult.has_value()) {
        return;
    }
    // Drawn before DrawTextLayout so glyphs render on top of the highlight
    // (Phase 4b2, N-cursor generalization Phase 4b7a). Matches drawn first
    // (Phase 5b3a) so an active text selection layers visibly above match
    // highlighting, both still behind the glyphs. Token colors (Phase 7b)
    // are applied to the layout itself (not a background rect), so they
    // must be set before DrawTextLayout - order relative to the two
    // highlight calls above doesn't matter.
    drawMatchesOnLine(dc, **layoutResult, y, lineStart, lineEnd);
    drawSelectionsOnLine(dc, **layoutResult, y, lineStart, lineEnd);
    // Phase 7e: a background element like the two calls above, so it must
    // run before DrawTextLayout too - see this method's declaration comment
    // for the isActiveLine approximation.
    const bool isActiveLine = std::ranges::any_of(
        caretDraws, [line](const CaretDraw& caret) { return caret.line == line; });
    drawIndentGuidesOnLine(dc, y, lineSpan, isActiveLine);
    drawTokensOnLine(**layoutResult, lineStart, lineEnd, tokenCursor);
    dc.DrawTextLayout(D2D1::Point2F(kGutterWidthDips, y), *layoutResult, m_textBrush.Get());
    drawCaretsOnLine(dc, **layoutResult, y, line, caretDraws);
    drawGutterOnLine(dc, y, line);
    // Phase 7i: a folded header shows its own text (drawn above) plus a
    // short " {...}" marker past it, standing in for the hidden body.
    const auto foldedHeader = std::ranges::find_if(
        m_foldRegions, [line](const FoldVisual& r) { return r.folded && r.headerLine == line; });
    if (foldedHeader != m_foldRegions.end()) {
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED((*layoutResult)->GetMetrics(&metrics))) {
            drawFoldedHeaderMarker(dc, kGutterWidthDips + metrics.width, y);
        }
    }
}

bool RenderPipeline::isLineHidden(document::LineNumber line) const noexcept {
    return std::ranges::any_of(m_foldRegions, [line](const FoldVisual& region) {
        return region.folded && line > region.headerLine && line <= region.endLineInclusive;
    });
}

void RenderPipeline::drawFoldedHeaderMarker(ID2D1DeviceContext6& dc, float x, float y) noexcept {
    if (!m_dwriteFactory || !m_textFormat || !m_textBrush) {
        return;
    }
    // One-off layout, not TextLayoutCache - see drawBreadcrumb()'s identical
    // rationale (this string is synthesized fresh, not keyed by line number).
    constexpr std::u16string_view kMarker = u" {…}";
    const std::wstring_view       wMarker = util::toWstringView(kMarker);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = m_dwriteFactory->CreateTextLayout(
        wMarker.data(), static_cast<UINT32>(wMarker.size()), m_textFormat.Get(), kMaxLayoutWidthDips,
        kMaxLayoutHeightDips, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return;
    }
    dc.DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), m_textBrush.Get());
}

std::vector<RenderPipeline::CaretDraw> RenderPipeline::computeCaretDraws() const noexcept {
    std::vector<CaretDraw> draws;
    draws.reserve(m_cursorVisuals.size());
    for (const CursorVisual& cv : m_cursorVisuals) {
        const LineNumber cursorLine = m_document->offsetToLine(cv.position);
        draws.push_back(CaretDraw{
            .line   = cursorLine,
            .column = static_cast<std::uint32_t>(cv.position - m_document->lineToOffset(cursorLine)),
            .virtualColumnOffset = cv.virtualColumnOffset,
        });
    }
    return draws;
}

void RenderPipeline::drawCaretsOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                      LineNumber line, const std::vector<CaretDraw>& caretDraws) noexcept {
    for (const CaretDraw& caret : caretDraws) {
        if (caret.line == line) {
            drawCaretOnLine(dc, layout, y, caret.column, caret.virtualColumnOffset);
        }
    }
}

void RenderPipeline::drawSelectionsOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                          TextPos lineStart, TextPos lineEnd) noexcept {
    for (const CursorVisual& cv : m_cursorVisuals) {
        if (cv.selectionRange.empty()) {
            continue;
        }
        const TextPos selStart     = std::min(cv.selectionRange.start, cv.selectionRange.end);
        const TextPos selEnd       = std::max(cv.selectionRange.start, cv.selectionRange.end);
        const TextPos overlapStart = std::max(lineStart, selStart);
        const TextPos overlapEnd   = std::min(lineEnd, selEnd);
        if (overlapStart < overlapEnd) {
            drawSelectionOnLine(dc, layout, y, static_cast<std::uint32_t>(overlapStart - lineStart),
                               static_cast<std::uint32_t>(overlapEnd - lineStart));
        }
    }
}

void RenderPipeline::drawMatchesOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                       TextPos lineStart, TextPos lineEnd) noexcept {
    for (const MatchVisual& match : m_matchVisuals) {
        if (match.range.empty()) {
            continue;
        }
        const TextPos overlapStart = std::max(lineStart, match.range.start);
        const TextPos overlapEnd   = std::min(lineEnd, match.range.end);
        if (overlapStart < overlapEnd) {
            drawMatchOnLine(dc, layout, y, static_cast<std::uint32_t>(overlapStart - lineStart),
                           static_cast<std::uint32_t>(overlapEnd - lineStart), match.isCurrent);
        }
    }
}

void RenderPipeline::drawCaretOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                     std::uint32_t column,
                                     std::uint32_t virtualColumnOffset) noexcept {
    if (!m_textBrush) {
        return;
    }
    DWRITE_HIT_TEST_METRICS metrics{};
    float                    caretX = 0.0F;
    float                    caretY = 0.0F;
    const HRESULT hr = layout.HitTestTextPosition(column, FALSE, &caretX, &caretY, &metrics);
    if (FAILED(hr)) {
        return;
    }
    if (virtualColumnOffset > 0) {
        caretX += static_cast<float>(virtualColumnOffset) * m_charWidthDips;
    }
    constexpr float kCaretWidthDips = 1.5F;
    // HitTestTextPosition() returns coordinates local to `layout`'s own
    // origin (0,0), independent of whatever origin DrawTextLayout() is
    // called with - kGutterWidthDips must be added explicitly here to line
    // up with the glyphs, which DO get shifted by DrawTextLayout()'s origin
    // parameter (Phase 4b8c, confirmed by reading the actual D2D/DWrite
    // call sequence - see drawVisibleLines()).
    const D2D1_RECT_F caretRect = D2D1::RectF(kGutterWidthDips + caretX, y,
                                              kGutterWidthDips + caretX + kCaretWidthDips,
                                              y + m_lineHeightDips);
    dc.FillRectangle(caretRect, m_textBrush.Get());
}

void RenderPipeline::drawSelectionOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                         std::uint32_t startColumn, std::uint32_t endColumn) noexcept {
    if (!m_selectionBrush) {
        return;
    }
    DWRITE_HIT_TEST_METRICS startMetrics{};
    DWRITE_HIT_TEST_METRICS endMetrics{};
    float   startX = 0.0F;
    float   startY = 0.0F;
    float   endX   = 0.0F;
    float   endY   = 0.0F;
    HRESULT hr = layout.HitTestTextPosition(startColumn, FALSE, &startX, &startY, &startMetrics);
    if (FAILED(hr)) {
        return;
    }
    hr = layout.HitTestTextPosition(endColumn, FALSE, &endX, &endY, &endMetrics);
    if (FAILED(hr)) {
        return;
    }
    // See drawCaretOnLine()'s comment - layout-local coordinates need the
    // gutter offset added explicitly (Phase 4b8c).
    const D2D1_RECT_F selectionRect = D2D1::RectF(kGutterWidthDips + startX, y, kGutterWidthDips + endX,
                                                  y + m_lineHeightDips);
    dc.FillRectangle(selectionRect, m_selectionBrush.Get());
}

void RenderPipeline::drawMatchOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                     std::uint32_t startColumn, std::uint32_t endColumn,
                                     bool isCurrent) noexcept {
    ID2D1SolidColorBrush* brush = isCurrent ? m_currentMatchBrush.Get() : m_matchBrush.Get();
    if (brush == nullptr) {
        return;
    }
    DWRITE_HIT_TEST_METRICS startMetrics{};
    DWRITE_HIT_TEST_METRICS endMetrics{};
    float   startX = 0.0F;
    float   startY = 0.0F;
    float   endX   = 0.0F;
    float   endY   = 0.0F;
    HRESULT hr = layout.HitTestTextPosition(startColumn, FALSE, &startX, &startY, &startMetrics);
    if (FAILED(hr)) {
        return;
    }
    hr = layout.HitTestTextPosition(endColumn, FALSE, &endX, &endY, &endMetrics);
    if (FAILED(hr)) {
        return;
    }
    // See drawCaretOnLine()'s comment - layout-local coordinates need the
    // gutter offset added explicitly (Phase 4b8c).
    const D2D1_RECT_F matchRect =
        D2D1::RectF(kGutterWidthDips + startX, y, kGutterWidthDips + endX, y + m_lineHeightDips);
    dc.FillRectangle(matchRect, brush);
}

void RenderPipeline::drawGutterOnLine(ID2D1DeviceContext6& dc, float y, LineNumber line) noexcept {
    if (m_bookmarkBrush && std::ranges::find(m_bookmarkedLines, line) != m_bookmarkedLines.end()) {
        const float centerX = kGutterWidthDips / 2.0F;
        const float centerY = y + (m_lineHeightDips / 2.0F);
        const float radius  = kBookmarkDotSizeDips / 2.0F;
        const D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(centerX, centerY), radius, radius);
        dc.FillEllipse(dot, m_bookmarkBrush.Get());
    }

    // Phase 7i: a small chevron at the gutter's right edge for any fold
    // header line (folded or not) - drawn with DrawLine() rather than a
    // ID2D1PathGeometry so no COM geometry object is allocated per visible
    // line per frame. ">" (pointing right) while folded, "v" (pointing
    // down, disclosure-triangle convention) while expanded. Click handling
    // is deliberately deferred to a later sub-phase (Phase 7i plan's
    // Context point 6) - this is a visual-only indicator for now.
    if (!m_foldMarkerBrush) {
        return;
    }
    const auto headerRegion = std::ranges::find(m_foldRegions, line, &FoldVisual::headerLine);
    if (headerRegion == m_foldRegions.end()) {
        return;
    }
    constexpr float kMarkerHalfSize = 3.5F;
    const float     markerRight     = kGutterWidthDips - 4.0F;
    const float     markerLeft      = markerRight - (kMarkerHalfSize * 2.0F);
    const float     centerY         = y + (m_lineHeightDips / 2.0F);
    if (headerRegion->folded) {
        dc.DrawLine(D2D1::Point2F(markerLeft, centerY - kMarkerHalfSize), D2D1::Point2F(markerRight, centerY),
                    m_foldMarkerBrush.Get(), 1.5F);
        dc.DrawLine(D2D1::Point2F(markerRight, centerY), D2D1::Point2F(markerLeft, centerY + kMarkerHalfSize),
                    m_foldMarkerBrush.Get(), 1.5F);
    } else {
        const float midX = (markerLeft + markerRight) / 2.0F;
        dc.DrawLine(D2D1::Point2F(markerLeft, centerY - kMarkerHalfSize), D2D1::Point2F(midX, centerY),
                    m_foldMarkerBrush.Get(), 1.5F);
        dc.DrawLine(D2D1::Point2F(markerRight, centerY - kMarkerHalfSize), D2D1::Point2F(midX, centerY),
                    m_foldMarkerBrush.Get(), 1.5F);
    }
}

void RenderPipeline::drawTokensOnLine(IDWriteTextLayout& layout, TextPos lineStart, TextPos lineEnd,
                                      std::size_t& tokenCursor) noexcept {
    // Retire tokens that ended at or before this line's start - m_tokens is
    // sorted left-to-right (see this method's declaration comment), so once
    // a token is behind us it never needs revisiting. A token spanning
    // multiple lines (e.g. a block comment) is NOT retired here - its
    // range.end still lies past lineStart, so it stays at/after tokenCursor
    // and gets reconsidered by the next line's call too.
    while (tokenCursor < m_tokens.size() && m_tokens[tokenCursor].range.end <= lineStart) {
        ++tokenCursor;
    }
    for (std::size_t i = tokenCursor; i < m_tokens.size(); ++i) {
        const syntax::Token& token = m_tokens[i];
        if (token.range.start >= lineEnd) {
            break;  // sorted - nothing from here on can overlap this line either
        }
        ID2D1SolidColorBrush* brush = tokenBrush(token.kind);
        if (brush == nullptr) {
            continue;  // Text/Variable/Punctuation - falls through to DrawTextLayout()'s default brush
        }
        const TextPos overlapStart = std::max(lineStart, token.range.start);
        const TextPos overlapEnd   = std::min(lineEnd, token.range.end);
        if (overlapStart >= overlapEnd) {
            continue;
        }
        const DWRITE_TEXT_RANGE dwRange{
            .startPosition = static_cast<UINT32>(overlapStart - lineStart),
            .length        = static_cast<UINT32>(overlapEnd - overlapStart),
        };
        layout.SetDrawingEffect(brush, dwRange);
    }
}

void RenderPipeline::drawIndentGuidesOnLine(ID2D1DeviceContext6& dc, float y,
                                            std::u16string_view lineSpan, bool isActiveLine) noexcept {
    if (!m_indentGuideBrush || !m_activeIndentGuideBrush) {
        return;
    }
    constexpr std::uint32_t kTabWidth            = 4;  // matches main.cpp's kTabWidth (Phase 4b8d)
    constexpr float          kIndentGuideWidthDips = 1.0F;
    const std::uint32_t indentColumns = computeIndentColumns(lineSpan, kTabWidth);
    const std::uint32_t guideCount    = computeIndentGuideCount(indentColumns, kTabWidth);
    ID2D1SolidColorBrush* brush = isActiveLine ? m_activeIndentGuideBrush.Get() : m_indentGuideBrush.Get();
    for (std::uint32_t level = 1; level <= guideCount; ++level) {
        const float x =
            kGutterWidthDips + (static_cast<float>(level * kTabWidth) * m_charWidthDips);
        const D2D1_RECT_F guideRect =
            D2D1::RectF(x, y, x + kIndentGuideWidthDips, y + m_lineHeightDips);
        dc.FillRectangle(guideRect, brush);
    }
}

void RenderPipeline::drawBreadcrumb(ID2D1DeviceContext6& dc) noexcept {
    // No primary cursor to anchor the path on (e.g. no cursors set at all,
    // as in a bare render-smoke test) - draw nothing, not even the
    // background band, rather than show an empty strip with no meaning.
    const auto primaryIt = std::ranges::find_if(
        m_cursorVisuals, [](const CursorVisual& cursor) { return cursor.isPrimary; });
    if (primaryIt == m_cursorVisuals.end() || !m_breadcrumbBackgroundBrush) {
        return;
    }

    const float widthDips = static_cast<float>(m_width) / m_dpiScale;
    dc.FillRectangle(D2D1::RectF(0.0F, 0.0F, widthDips, kBreadcrumbHeightDips),
                      m_breadcrumbBackgroundBrush.Get());

    const std::vector<const syntax::OutlineNode*> path =
        syntax::findBreadcrumbPath(primaryIt->position, m_cachedOutline);
    if (path.empty() || !m_dwriteFactory || !m_textFormat || !m_textBrush) {
        return;  // background band alone still communicates "no symbol here"
    }

    std::u16string joined;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            joined += u" > ";
        }
        joined += path[i]->name;
    }

    // One-off layout, not TextLayoutCache (that cache is keyed by document
    // line number and reused across frames for unchanged lines - this string
    // is synthesized fresh from m_cachedOutline every frame and has no line
    // number to key on; the cost of laying out one short line per frame is
    // negligible next to drawVisibleLines()' per-visible-line work).
    const std::wstring_view wJoined = util::toWstringView(joined);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = m_dwriteFactory->CreateTextLayout(
        wJoined.data(), static_cast<UINT32>(wJoined.size()), m_textFormat.Get(),
        std::max(0.0F, widthDips - kGutterWidthDips), kBreadcrumbHeightDips, layout.GetAddressOf());
    if (FAILED(hr) || !layout) {
        return;
    }
    dc.DrawTextLayout(D2D1::Point2F(kGutterWidthDips, 0.0F), layout.Get(), m_textBrush.Get());
}

std::optional<document::TextPos> RenderPipeline::hitTest(std::int32_t xPx, std::int32_t yPx) noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F || !m_dwriteFactory ||
        m_dpiScale <= 0.0F) {
        return std::nullopt;
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return std::nullopt;
    }

    // Clicks within the gutter strip itself clamp to column 0 of that line -
    // no separate "toggle bookmark on gutter click" interaction exists yet
    // (Phase 4b8c, deliberately deferred).
    const float xDip = std::max(0.0F, (static_cast<float>(xPx) / m_dpiScale) - kGutterWidthDips);
    // Phase 7h: clicks within the Breadcrumb strip clamp to the first visible
    // line's row offset - same "clamp to a sane default" convention as the
    // gutter's xDip clamp above.
    const float yDip = std::max(0.0F, (static_cast<float>(yPx) / m_dpiScale) - kBreadcrumbHeightDips);

    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);
    const LineNumber rowOffset =
        yDip >= 0.0F ? static_cast<LineNumber>(yDip / m_lineHeightDips) : LineNumber{0};
    // Phase 7i/7j: resolves `rowOffset` (a drawn-row count) to the same
    // logical line drawVisibleLines() actually drew there, skipping
    // folded-hidden lines - see visibleLineAtRow()'s comment.
    const LineNumber targetLine = visibleLineAtRow(startLine, rowOffset);

    const TextPos lineStart = m_document->lineToOffset(targetLine);
    const TextPos lineEndExclusive = (targetLine + 1 >= totalLines)
                                          ? m_cachedSnapshot->length()
                                          : m_document->lineToOffset(targetLine + 1);
    const std::u16string lineText =
        m_cachedSnapshot->extract(TextRange{.start = lineStart, .end = lineEndExclusive});
    std::u16string_view lineSpan(lineText);
    const auto newlinePos = lineSpan.find(u'\n');
    if (newlinePos != std::u16string_view::npos) {
        lineSpan = lineSpan.substr(0, newlinePos);
    }

    const auto layoutResult =
        m_layoutCache.getOrCreate(targetLine, lineSpan, *m_dwriteFactory.Get(), *m_textFormat.Get(),
                                  kMaxLayoutWidthDips, kMaxLayoutHeightDips);
    if (!layoutResult.has_value()) {
        return std::nullopt;
    }

    BOOL                     isTrailingHit = FALSE;
    BOOL                     isInside      = FALSE;
    DWRITE_HIT_TEST_METRICS  metrics{};
    const HRESULT hr = (*layoutResult)->HitTestPoint(xDip, 0.0F, &isTrailingHit, &isInside, &metrics);
    if (FAILED(hr)) {
        return std::nullopt;
    }
    const std::uint32_t column =
        isTrailingHit ? (metrics.textPosition + metrics.length) : metrics.textPosition;
    return lineStart + column;
}

document::LineNumber RenderPipeline::visibleLineAtRow(LineNumber startLine,
                                                       LineNumber visibleRowOffset) const noexcept {
    const std::uint64_t totalLines = m_document->lineCount();
    LineNumber          targetLine   = startLine;
    LineNumber          visibleSteps = 0;
    for (;;) {
        if (!isLineHidden(targetLine)) {
            if (visibleSteps == visibleRowOffset) {
                break;
            }
            ++visibleSteps;
        }
        if (targetLine + 1 >= totalLines) {
            break;
        }
        ++targetLine;
    }
    return targetLine;
}

std::optional<document::LineNumber> RenderPipeline::hitTestFoldMarker(std::int32_t xPx,
                                                                       std::int32_t yPx) noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F || m_dpiScale <= 0.0F) {
        return std::nullopt;
    }
    const float xDip = static_cast<float>(xPx) / m_dpiScale;
    if (xDip < 0.0F || xDip >= kGutterWidthDips) {
        return std::nullopt;  // click landed outside the gutter strip entirely
    }
    const std::uint64_t totalLines = m_document->lineCount();
    if (totalLines == 0) {
        return std::nullopt;
    }
    const float yDip = std::max(0.0F, (static_cast<float>(yPx) / m_dpiScale) - kBreadcrumbHeightDips);
    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);
    const auto        rowOffset  = static_cast<LineNumber>(yDip / m_lineHeightDips);
    const LineNumber   targetLine = visibleLineAtRow(startLine, rowOffset);
    if (std::ranges::find(m_foldRegions, targetLine, &FoldVisual::headerLine) == m_foldRegions.end()) {
        return std::nullopt;
    }
    return targetLine;
}

RenderExpected<void> RenderPipeline::renderOnce() noexcept {
    // render() already checks isAttached() before calling this, but
    // renderOnce() is a private helper reachable from two call sites in
    // render() - re-checking here keeps it self-contained rather than
    // relying on caller discipline for a std::optional dereference.
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }
    // Bound once, right after the check above: refreshDocumentCacheIfStale()/
    // ensureTextFormat() below are opaque member calls from a static
    // analyzer's point of view, so re-dereferencing m_device-> after them
    // would look like an unchecked std::optional access even though neither
    // call touches m_device.
    RenderDevice& device = *m_device;

    auto docResult = refreshDocumentCacheIfStale();
    if (!docResult) {
        return docResult;
    }
    auto formatResult = ensureTextFormat();
    if (!formatResult) {
        return formatResult;
    }

    auto beginResult = device.beginFrame();
    if (!beginResult) {
        return std::unexpected(beginResult.error());
    }
    ID2D1DeviceContext6* dc = *beginResult;

    auto brushResult = ensureTextBrush(*dc);
    if (!brushResult) {
        // Best-effort close; frame content is moot on this error path, but
        // the original brush failure is still what gets reported.
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return brushResult;
    }
    auto selectionBrushResult = ensureSelectionBrush(*dc);
    if (!selectionBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return selectionBrushResult;
    }
    auto matchBrushResult = ensureMatchBrushes(*dc);
    if (!matchBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return matchBrushResult;
    }
    auto bookmarkBrushResult = ensureBookmarkBrush(*dc);
    if (!bookmarkBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return bookmarkBrushResult;
    }
    auto tokenBrushResult = ensureTokenBrushes(*dc);
    if (!tokenBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return tokenBrushResult;
    }
    auto indentGuideBrushResult = ensureIndentGuideBrushes(*dc);
    if (!indentGuideBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return indentGuideBrushResult;
    }
    auto breadcrumbBrushResult = ensureBreadcrumbBrush(*dc);
    if (!breadcrumbBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return breadcrumbBrushResult;
    }
    auto foldMarkerBrushResult = ensureFoldMarkerBrush(*dc);
    if (!foldMarkerBrushResult) {
        [[maybe_unused]] const auto closeResult = device.endFrame();
        return foldMarkerBrushResult;
    }

    // Matches the previous GDI placeholder fill (RGB 30,30,30) so the
    // GDI->D2D handoff (ADR-009) stays visually seamless as a background.
    constexpr D2D1_COLOR_F kBackgroundColor = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F, 1.0F};
    dc->Clear(kBackgroundColor);
    drawVisibleLines(*dc);
    drawBreadcrumb(*dc);

    return device.endFrame();
}

}  // namespace neomifes::render
