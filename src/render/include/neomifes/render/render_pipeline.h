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

#include "neomifes/document/text_pos.h"
#include "neomifes/render/render_device.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/text_layout_cache.h"

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

        friend bool operator==(const FrameState&, const FrameState&) = default;
    };
    [[nodiscard]] FrameState captureFrameState() const noexcept;

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

    // m_textFormat/m_dwriteFactory are DPI-independent (DIPs) and survive
    // device loss; m_textBrush is bound to the device context and must be
    // reset whenever the device is (re)created (recreateDevice()/attach()).
    Microsoft::WRL::ComPtr<IDWriteFactory7>       m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>     m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>  m_textBrush;
    float                                          m_lineHeightDips = 0.0F;  // 0 == not yet measured

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
