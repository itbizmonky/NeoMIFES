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
    // resize()/clearAndPresent() return an error on a headless instance.
    [[nodiscard]] static RenderExpected<RenderDevice> createHeadless() noexcept;

    // Releases the D2D target bitmap's reference to the old back buffer,
    // resizes the swap chain, and rebinds a fresh target bitmap.
    [[nodiscard]] RenderExpected<void> resize(std::uint32_t width, std::uint32_t height) noexcept;

    // Clears the frame to `color` and presents. Phase 3a has no content to
    // draw yet - this exists to prove the device/swap-chain/present pipe
    // works end-to-end; real content drawing lands in Phase 3b.
    [[nodiscard]] RenderExpected<void> clearAndPresent(const D2D1_COLOR_F& color) noexcept;

private:
    RenderDevice() = default;

    Microsoft::WRL::ComPtr<ID3D11Device>        m_d3dDevice;
    Microsoft::WRL::ComPtr<IDXGISwapChain2>     m_swapChain;    // null for headless instances
    Microsoft::WRL::ComPtr<ID2D1Device6>        m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext6> m_dc;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1>        m_targetBitmap; // null for headless instances
};

}  // namespace neomifes::render
