#include "neomifes/render/render_pipeline.h"

#include <algorithm>
#include <string_view>
#include <utility>

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
        .caretPosition   = m_caretPosition,
        .selectionRange  = m_selectionRange,
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

    m_textFormat     = std::move(format);
    m_lineHeightDips = metrics.height;
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

    // Caret line/column, computed once (Phase 4b1). Only meaningful if it
    // falls within [startLine, endLineExclusive) - checked per-iteration
    // below rather than skipped here, since a caret outside the visible
    // range (e.g. ensureVisible() hasn't run yet for this frame) simply
    // never matches `line == caretLine` in the loop.
    const LineNumber caretLine = m_document->offsetToLine(m_caretPosition);
    const auto caretColumn =
        static_cast<std::uint32_t>(m_caretPosition - m_document->lineToOffset(caretLine));

    std::u16string_view remaining(text);
    float                y = 0.0F;
    for (LineNumber line = startLine; line < endLineExclusive; ++line) {
        const auto newlinePos = remaining.find(u'\n');
        const std::u16string_view lineSpan =
            (newlinePos == std::u16string_view::npos) ? remaining : remaining.substr(0, newlinePos);

        const auto layoutResult =
            m_layoutCache.getOrCreate(line, lineSpan, *m_dwriteFactory.Get(), *m_textFormat.Get(),
                                      kMaxLayoutWidthDips, kMaxLayoutHeightDips);
        if (layoutResult.has_value()) {
            // Drawn before DrawTextLayout so glyphs render on top of the
            // highlight (Phase 4b2). Skipped entirely when there's no
            // selection - avoids a lineToOffset() lookup on the common
            // no-selection path.
            if (!m_selectionRange.empty()) {
                const TextPos lineStart    = m_document->lineToOffset(line);
                const TextPos lineEnd      = lineStart + lineSpan.size();
                const TextPos selStart     = std::min(m_selectionRange.start, m_selectionRange.end);
                const TextPos selEnd       = std::max(m_selectionRange.start, m_selectionRange.end);
                const TextPos overlapStart = std::max(lineStart, selStart);
                const TextPos overlapEnd   = std::min(lineEnd, selEnd);
                if (overlapStart < overlapEnd) {
                    drawSelectionOnLine(dc, **layoutResult, y,
                                        static_cast<std::uint32_t>(overlapStart - lineStart),
                                        static_cast<std::uint32_t>(overlapEnd - lineStart));
                }
            }
            dc.DrawTextLayout(D2D1::Point2F(0.0F, y), *layoutResult, m_textBrush.Get());
            if (line == caretLine) {
                drawCaretOnLine(dc, **layoutResult, y, caretColumn);
            }
        }
        // A layout-creation failure for a single line is no worse than the
        // pre-Phase-3c behavior of DrawText() silently failing per-call - it
        // skips just that line, not the whole frame.

        y += m_lineHeightDips;
        if (newlinePos == std::u16string_view::npos) {
            break;
        }
        remaining = remaining.substr(newlinePos + 1);
    }
}

void RenderPipeline::drawCaretOnLine(ID2D1DeviceContext6& dc, IDWriteTextLayout& layout, float y,
                                     std::uint32_t column) noexcept {
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
    constexpr float kCaretWidthDips = 1.5F;
    const D2D1_RECT_F caretRect =
        D2D1::RectF(caretX, y, caretX + kCaretWidthDips, y + m_lineHeightDips);
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
    const D2D1_RECT_F selectionRect = D2D1::RectF(startX, y, endX, y + m_lineHeightDips);
    dc.FillRectangle(selectionRect, m_selectionBrush.Get());
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

    const float xDip = static_cast<float>(xPx) / m_dpiScale;
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

    // Matches the previous GDI placeholder fill (RGB 30,30,30) so the
    // GDI->D2D handoff (ADR-009) stays visually seamless as a background.
    constexpr D2D1_COLOR_F kBackgroundColor = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F, 1.0F};
    dc->Clear(kBackgroundColor);
    drawVisibleLines(*dc);

    return device.endFrame();
}

}  // namespace neomifes::render
