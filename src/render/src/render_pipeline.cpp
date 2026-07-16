#include "neomifes/render/render_pipeline.h"

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
    m_textBrush.Reset();  // stale binding to whatever device context existed before
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
    m_textBrush.Reset();  // bound to the device context that just went away
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

void RenderPipeline::drawVisibleLines(ID2D1DeviceContext6& dc) noexcept {
    if (!m_cachedSnapshot || m_document == nullptr || m_lineHeightDips <= 0.0F ||
        !m_dwriteFactory) {
        return;
    }

    // Fixed, window-size-independent bound rather than the live client
    // width: keeps layout cache entries valid across resize() (a resize
    // never needs to invalidate this cache), same spirit as
    // ensureTextFormat()'s 4096-DIP probe-layout box.
    constexpr float kMaxLayoutWidthDips  = 65536.0F;
    constexpr float kMaxLayoutHeightDips = 65536.0F;

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
            dc.DrawTextLayout(D2D1::Point2F(0.0F, y), *layoutResult, m_textBrush.Get());
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

    // Matches the previous GDI placeholder fill (RGB 30,30,30) so the
    // GDI->D2D handoff (ADR-009) stays visually seamless as a background.
    constexpr D2D1_COLOR_F kBackgroundColor = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F, 1.0F};
    dc->Clear(kBackgroundColor);
    drawVisibleLines(*dc);

    return device.endFrame();
}

}  // namespace neomifes::render
