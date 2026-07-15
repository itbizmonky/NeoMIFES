#pragma once

// Error type for the render layer. Every COM/Direct2D/DXGI call that can
// fail returns RenderExpected<T> rather than throwing - this is the first
// layer in the codebase to use std::expected directly (Phase 2's document
// layer predates full std::expected availability and used std::variant
// instead; CLAUDE.md sec.4 prescribes std::expected/std::optional for
// recoverable errors, so render adopts it as written).

#include <windows.h>

#include <cstdint>
#include <expected>
#include <string>

namespace neomifes::render {

enum class RenderStage : std::uint8_t {
    D2DFactory,
    DWriteFactory,
    D3D11Device,
    DxgiSwapChain,
    D2DDevice,
    D2DDeviceContext,
    BackBufferBitmap,
    ResizeBuffers,
    Present,
    NotAttached,
};

struct RenderError {
    RenderStage stage;
    HRESULT     hr = S_OK;

    // True for HRESULTs that invalidate the entire device graph, not just
    // the call that returned them (DXGI_ERROR_DEVICE_REMOVED/RESET/HUNG,
    // D2DERR_RECREATE_TARGET) - callers must recreate RenderDevice wholesale
    // rather than retrying the failing call in isolation.
    [[nodiscard]] bool isDeviceLost() const noexcept;
};

// Human-readable diagnostic string, e.g. "stage=Present hr=0x887A0005 (...)".
// For logging only - format has no stability guarantee, never parse it.
[[nodiscard]] std::string describe(const RenderError& err);

template <typename T>
using RenderExpected = std::expected<T, RenderError>;

}  // namespace neomifes::render
