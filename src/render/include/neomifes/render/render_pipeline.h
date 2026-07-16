#pragma once

// RenderPipeline - the facade MainWindow/app code actually talks to.
// Owns at most one RenderDevice; on device loss, drops and recreates it
// wholesale (per Direct3D/DXGI guidance - a lost device invalidates the
// entire object graph, not just the swap chain).
//
// Phase 3b scope: attach/resize/render draw the visible lines of an
// attached Document with a single fixed-pitch IDWriteTextFormat, no word
// wrap, topLine always 0 (no interactive scrolling yet - Editor Core /
// Viewport is Phase 4). Layout/glyph caching and dirty-rect damage tracking
// land in Phase 3c (see docs/design/detailed_design.md sec.4 for the
// eventual full shape - TextLayoutCache/GlyphCache/DamageTracker are
// deliberately not part of this facade yet).

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "neomifes/document/text_pos.h"
#include "neomifes/render/render_device.h"
#include "neomifes/render/render_error.h"

namespace neomifes::document {
class Document;
class BufferSnapshot;
}  // namespace neomifes::document

namespace neomifes::render {

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

private:
    [[nodiscard]] RenderExpected<void> recreateDevice() noexcept;
    [[nodiscard]] RenderExpected<void> refreshDocumentCacheIfStale() noexcept;
    [[nodiscard]] RenderExpected<void> ensureTextFormat() noexcept;
    [[nodiscard]] RenderExpected<void> ensureTextBrush(ID2D1DeviceContext6& dc) noexcept;
    [[nodiscard]] RenderExpected<void> renderOnce() noexcept;
    void drawVisibleLines(ID2D1DeviceContext6& dc) noexcept;

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

    // m_textFormat is DPI-independent (DIPs) and survives device loss;
    // m_textBrush is bound to the device context and must be reset whenever
    // the device is (re)created (recreateDevice()/attach()).
    Microsoft::WRL::ComPtr<IDWriteTextFormat>     m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_textBrush;
    float                                          m_lineHeightDips = 0.0F;  // 0 == not yet measured
};

}  // namespace neomifes::render
