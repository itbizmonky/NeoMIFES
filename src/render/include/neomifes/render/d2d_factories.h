#pragma once

// Process-wide singleton Direct2D/DirectWrite factories.
// basic_design.md sec.4.1: "COM/DirectWrite factories are reused as
// singletons" (not recreated per window/device).

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include "neomifes/render/render_error.h"

namespace neomifes::render {

// D2D1_FACTORY_TYPE_SINGLE_THREADED / DWRITE_FACTORY_TYPE_SHARED are safe as
// of Phase 3a because only the UI thread ever creates D2D/DWrite objects
// (ADR-009) - revisit if a later phase moves layout/rasterization off the UI
// thread.
[[nodiscard]] RenderExpected<Microsoft::WRL::ComPtr<ID2D1Factory7>> sharedD2DFactory() noexcept;
[[nodiscard]] RenderExpected<Microsoft::WRL::ComPtr<IDWriteFactory7>> sharedDWriteFactory() noexcept;

}  // namespace neomifes::render
