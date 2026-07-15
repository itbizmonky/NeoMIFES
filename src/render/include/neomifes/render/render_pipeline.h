#pragma once

// RenderPipeline - the facade MainWindow/app code actually talks to.
// Owns at most one RenderDevice; on device loss, drops and recreates it
// wholesale (per Direct3D/DXGI guidance - a lost device invalidates the
// entire object graph, not just the swap chain).
//
// Phase 3a scope: attach/resize/render(solid clear) only. Text layout,
// glyph/line caching, and dirty-rect damage tracking land in later
// sub-phases (see docs/design/detailed_design.md sec.4 for the eventual
// full shape - RenderFrame/invalidate(TextRange)/TextLayoutCache/GlyphCache/
// DamageTracker are deliberately not part of this facade yet).

#include <windows.h>

#include <cstdint>
#include <optional>

#include "neomifes/render/render_device.h"
#include "neomifes/render/render_error.h"

namespace neomifes::render {

class RenderPipeline {
public:
    // Queries the current client-area size itself (GetClientRect) rather
    // than trusting a prior WM_SIZE - Windows doesn't reliably fire WM_SIZE
    // for the initial CreateWindowExW size, so this is the only correct
    // source of truth for the first frame's dimensions.
    [[nodiscard]] RenderExpected<void> attach(HWND hwnd) noexcept;

    [[nodiscard]] RenderExpected<void> resize(std::uint32_t width, std::uint32_t height) noexcept;

    // Clears to the placeholder color and presents. Retries once after a
    // device-lost recreation; a second failure propagates to the caller and
    // that frame is simply skipped (the next WM_PAINT retries).
    [[nodiscard]] RenderExpected<void> render() noexcept;

    [[nodiscard]] bool isAttached() const noexcept { return m_device.has_value(); }

private:
    [[nodiscard]] RenderExpected<void> recreateDevice() noexcept;

    HWND                         m_hwnd   = nullptr;
    std::uint32_t                m_width  = 0;
    std::uint32_t                m_height = 0;
    std::optional<RenderDevice>  m_device;
};

}  // namespace neomifes::render
