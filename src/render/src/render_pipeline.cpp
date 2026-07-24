#include "neomifes/render/render_pipeline.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/render/d2d_factories.h"
#include "neomifes/render/resize_math.h"
#include "neomifes/render/viewport_math.h"

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

    const std::uint32_t visibleCount = computeVisibleLineCount(m_height, m_dpiScale, m_lineHeightDips);
    if (visibleCount == 0) {
        return;
    }
    const LineNumber endLineExclusive =
        (startLine + visibleCount < totalLines) ? startLine + visibleCount
                                                 : static_cast<LineNumber>(totalLines);

    const TextPos startOffset = m_document->lineToOffset(startLine);
    const TextPos endOffset   = (endLineExclusive >= totalLines)
                                     ? m_cachedSnapshot->length()
                                     : m_document->lineToOffset(endLineExclusive);
    const std::u16string text =
        m_cachedSnapshot->extract(TextRange{.start = startOffset, .end = endOffset});

    const std::vector<CaretDraw> caretDraws = computeCaretDraws();
    std::size_t tokenCursor = 0;  // Phase 7b: threaded forward across the line loop, see drawTokensOnLine()'s comment

    std::u16string_view remaining(text);
    float                y         = 0.0F;
    TextPos              lineStart = startOffset;
    for (LineNumber line = startLine; line < endLineExclusive; ++line) {
        const auto newlinePos = remaining.find(u'\n');
        const std::u16string_view lineSpan =
            (newlinePos == std::u16string_view::npos) ? remaining : remaining.substr(0, newlinePos);
        const TextPos lineEnd = lineStart + lineSpan.size();

        const auto layoutResult =
            m_layoutCache.getOrCreate(line, lineSpan, *m_dwriteFactory.Get(), *m_textFormat.Get(),
                                      kMaxLayoutWidthDips, kMaxLayoutHeightDips);
        if (layoutResult.has_value()) {
            // Drawn before DrawTextLayout so glyphs render on top of the
            // highlight (Phase 4b2, N-cursor generalization Phase 4b7a).
            // Matches drawn first (Phase 5b3a) so an active text selection
            // layers visibly above match highlighting, both still behind
            // the glyphs. Token colors (Phase 7b) are applied to the layout
            // itself (not a background rect), so they must be set before
            // DrawTextLayout - order relative to the two highlight calls
            // above doesn't matter.
            drawMatchesOnLine(dc, **layoutResult, y, lineStart, lineEnd);
            drawSelectionsOnLine(dc, **layoutResult, y, lineStart, lineEnd);
            drawTokensOnLine(**layoutResult, lineStart, lineEnd, tokenCursor);
            dc.DrawTextLayout(D2D1::Point2F(kGutterWidthDips, y), *layoutResult, m_textBrush.Get());
            drawCaretsOnLine(dc, **layoutResult, y, line, caretDraws);
            drawGutterOnLine(dc, y, line);
        }
        // A layout-creation failure for a single line is no worse than the
        // pre-Phase-3c behavior of DrawText() silently failing per-call - it
        // skips just that line, not the whole frame.

        y += m_lineHeightDips;
        if (newlinePos == std::u16string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;  // +1 for the '\n' this line's span excluded
        remaining = remaining.substr(newlinePos + 1);
    }
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
    if (!m_bookmarkBrush) {
        return;
    }
    if (std::ranges::find(m_bookmarkedLines, line) == m_bookmarkedLines.end()) {
        return;
    }
    const float centerX = kGutterWidthDips / 2.0F;
    const float centerY = y + (m_lineHeightDips / 2.0F);
    const float radius  = kBookmarkDotSizeDips / 2.0F;
    const D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(centerX, centerY), radius, radius);
    dc.FillEllipse(dot, m_bookmarkBrush.Get());
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
    const float yDip = static_cast<float>(yPx) / m_dpiScale;

    const LineNumber startLine =
        m_topLine < totalLines ? m_topLine : static_cast<LineNumber>(totalLines - 1);
    const LineNumber rowOffset =
        yDip >= 0.0F ? static_cast<LineNumber>(yDip / m_lineHeightDips) : LineNumber{0};
    LineNumber targetLine = startLine + rowOffset;
    if (targetLine >= totalLines) {
        targetLine = static_cast<LineNumber>(totalLines - 1);
    }

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

    // Matches the previous GDI placeholder fill (RGB 30,30,30) so the
    // GDI->D2D handoff (ADR-009) stays visually seamless as a background.
    constexpr D2D1_COLOR_F kBackgroundColor = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F, 1.0F};
    dc->Clear(kBackgroundColor);
    drawVisibleLines(*dc);

    return device.endFrame();
}

}  // namespace neomifes::render
