#pragma once

// RenderDevice - RAII owner of one HWND's D3D11 + D2D + DXGI device graph
// (device, device context, swap chain, back-buffer target bitmap).
//
// Either fully valid or does not exist: construction happens only through
// the static create()/createHeadless() factories (private constructor),
// mirroring the OriginalBuffer::openMemoryMapped() pattern elsewhere in the
// codebase rather than a throwing constructor or two-phase init() call.

#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>

#include "neomifes/render/render_error.h"

namespace neomifes::render {

class RenderDevice {
public:
    RenderDevice(const RenderDevice&)            = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;
    RenderDevice(RenderDevice&&)                 = default;
    RenderDevice& operator=(RenderDevice&&)      = default;
    ~RenderDevice()                              = default;

    // Full device graph attached to `hwnd`, sized to [width, height] (both
    // clamped to >=1, see resize_math.h). Tries D3D_DRIVER_TYPE_HARDWARE
    // first, falls back to D3D_DRIVER_TYPE_WARP (needed for GPU-less CI
    // runners).
    [[nodiscard]] static RenderExpected<RenderDevice> create(HWND hwnd, std::uint32_t width,
                                                              std::uint32_t height) noexcept;

    // Device + device context only, no swap chain / target bitmap. Test-only:
    // exercises the HARDWARE->WARP fallback without needing a live HWND.
    // resize()/beginFrame() return an error on a headless instance.
    [[nodiscard]] static RenderExpected<RenderDevice> createHeadless() noexcept;

    // Releases the D2D target bitmap's reference to the old back buffer,
    // resizes the swap chain, and rebinds a fresh target bitmap.
    [[nodiscard]] RenderExpected<void> resize(std::uint32_t width, std::uint32_t height) noexcept;

    // Sets the device context's DPI (device pixels per DIP-inch, both axes
    // equal). ID2D1DeviceContext::SetDpi returns void - this cannot fail.
    // Call after every (re)creation and whenever WM_DPICHANGED reports a new
    // scale: D2D content is authored in DPI-independent units (DIPs), and
    // this is what maps 1 DIP to N device pixels for this context. A fresh
    // context defaults to 96 DPI (scale 1.0), so recreateDevice() callers
    // must re-apply the current scale after a device-lost rebuild.
    void setDpi(float dpiScale) noexcept;

    // Opens a frame: calls BeginDraw() and returns a non-owning pointer to
    // the device context for the caller to issue Clear()/DrawText()/etc
    // against. LIFETIME CONTRACT: the returned pointer is valid only until
    // the matching endFrame() call on this same RenderDevice - never store
    // it across frames or use it after endFrame()/resize()/destruction.
    // Not reentrant: calling beginFrame() again before endFrame() returns
    // an error (stage=Present, hr=E_NOT_VALID_STATE) rather than asserting,
    // so a caller bug surfaces as a skipped frame instead of a debug-layer
    // trap. Fails on a headless instance (no swap chain to draw into).
    [[nodiscard]] RenderExpected<ID2D1DeviceContext6*> beginFrame() noexcept;

    // Closes the frame opened by beginFrame(): EndDraw() then
    // Present1(1, 0, ...) (v-sync interval 1, whole-frame present).
    // DXGI_STATUS_OCCLUDED is treated as success (legitimate for a hidden/
    // occluded window). Errors if no beginFrame() is currently open.
    [[nodiscard]] RenderExpected<void> endFrame() noexcept;

private:
    RenderDevice() = default;

    Microsoft::WRL::ComPtr<ID3D11Device>        m_d3dDevice;
    Microsoft::WRL::ComPtr<IDXGISwapChain2>     m_swapChain;    // null for headless instances
    Microsoft::WRL::ComPtr<ID2D1Device6>        m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext6> m_dc;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1>        m_targetBitmap; // null for headless instances
    bool                                        m_frameOpen = false; // beginFrame()/endFrame() misuse guard
};

}  // namespace neomifes::render
