#include "neomifes/render/render_device.h"

#include <array>

#include "neomifes/render/d2d_factories.h"
#include "neomifes/render/resize_math.h"

using Microsoft::WRL::ComPtr;

namespace neomifes::render {

namespace {

// Tries D3D_DRIVER_TYPE_HARDWARE first; falls back to D3D_DRIVER_TYPE_WARP
// (a software rasterizer, no GPU driver dependency) so device creation
// still succeeds on GPU-less CI runners.
[[nodiscard]] RenderExpected<ComPtr<ID3D11Device>> createD3D11DeviceWithFallback() noexcept {
    static constexpr std::array<D3D_FEATURE_LEVEL, 2> kFeatureLevels = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    static constexpr std::array<D3D_DRIVER_TYPE, 2> kDriverTypes = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
    };

    HRESULT lastHr = E_FAIL;
    for (const D3D_DRIVER_TYPE driverType : kDriverTypes) {
        ComPtr<ID3D11Device> device;
        lastHr = ::D3D11CreateDevice(
            nullptr, driverType, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kFeatureLevels.data(),
            static_cast<UINT>(kFeatureLevels.size()), D3D11_SDK_VERSION, device.GetAddressOf(),
            nullptr, nullptr);
        if (SUCCEEDED(lastHr)) {
            return device;
        }
    }
    return std::unexpected(RenderError{.stage = RenderStage::D3D11Device, .hr = lastHr});
}

struct D2DDeviceAndContext {
    ComPtr<ID2D1Device6>        device;
    ComPtr<ID2D1DeviceContext6> context;
};

// D2D's newer device/context interfaces (ID2D1Device6/ID2D1DeviceContext6)
// aren't produced directly by the factory/device creation calls - only the
// base ID2D1Device/ID2D1DeviceContext are. Create the base interface, then
// QueryInterface (ComPtr::As) up to the version this codebase targets.
[[nodiscard]] RenderExpected<D2DDeviceAndContext>
    createD2DDeviceAndContext(const ComPtr<ID3D11Device>& d3dDevice) noexcept {
    const auto factory = sharedD2DFactory();
    if (!factory) {
        return std::unexpected(factory.error());
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDevice, .hr = hr});
    }

    ComPtr<ID2D1Device> baseDevice;
    hr = (*factory)->CreateDevice(dxgiDevice.Get(), baseDevice.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDevice, .hr = hr});
    }

    D2DDeviceAndContext result;
    hr = baseDevice.As(&result.device);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDevice, .hr = hr});
    }

    ComPtr<ID2D1DeviceContext> baseContext;
    hr = result.device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                            baseContext.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }

    hr = baseContext.As(&result.context);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DDeviceContext, .hr = hr});
    }
    return result;
}

[[nodiscard]] RenderExpected<ComPtr<IDXGISwapChain2>>
    createSwapChain(const ComPtr<ID3D11Device>& d3dDevice, HWND hwnd, std::uint32_t width,
                    std::uint32_t height) noexcept {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DxgiSwapChain, .hr = hr});
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DxgiSwapChain, .hr = hr});
    }

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DxgiSwapChain, .hr = hr});
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width            = width;
    desc.Height           = height;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(d3dDevice.Get(), hwnd, &desc, nullptr, nullptr,
                                         swapChain1.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DxgiSwapChain, .hr = hr});
    }

    ComPtr<IDXGISwapChain2> swapChain2;
    hr = swapChain1.As(&swapChain2);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DxgiSwapChain, .hr = hr});
    }

    // This is a plain editor window, not a fullscreen-capable game surface -
    // opt out of DXGI's automatic Alt+Enter fullscreen toggle handling.
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    return swapChain2;
}

[[nodiscard]] RenderExpected<ComPtr<ID2D1Bitmap1>>
    bindTargetBitmap(const ComPtr<ID2D1DeviceContext6>& dc,
                     const ComPtr<IDXGISwapChain2>& swapChain) noexcept {
    ComPtr<IDXGISurface> surface;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()));
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::BackBufferBitmap, .hr = hr});
    }

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

    ComPtr<ID2D1Bitmap1> bitmap;
    hr = dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, bitmap.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::BackBufferBitmap, .hr = hr});
    }

    dc->SetTarget(bitmap.Get());
    return bitmap;
}

}  // namespace

RenderExpected<RenderDevice> RenderDevice::create(HWND hwnd, std::uint32_t width,
                                                   std::uint32_t height) noexcept {
    const auto [w, h] = sanitizeSwapChainSize(width, height);

    auto d3dDevice = createD3D11DeviceWithFallback();
    if (!d3dDevice) {
        return std::unexpected(d3dDevice.error());
    }
    auto d2d = createD2DDeviceAndContext(*d3dDevice);
    if (!d2d) {
        return std::unexpected(d2d.error());
    }
    auto swapChain = createSwapChain(*d3dDevice, hwnd, w, h);
    if (!swapChain) {
        return std::unexpected(swapChain.error());
    }
    auto bitmap = bindTargetBitmap(d2d->context, *swapChain);
    if (!bitmap) {
        return std::unexpected(bitmap.error());
    }

    RenderDevice device;
    device.m_d3dDevice   = *d3dDevice;
    device.m_swapChain   = *swapChain;
    device.m_d2dDevice   = d2d->device;
    device.m_dc          = d2d->context;
    device.m_targetBitmap = *bitmap;
    return device;
}

RenderExpected<RenderDevice> RenderDevice::createHeadless() noexcept {
    auto d3dDevice = createD3D11DeviceWithFallback();
    if (!d3dDevice) {
        return std::unexpected(d3dDevice.error());
    }
    auto d2d = createD2DDeviceAndContext(*d3dDevice);
    if (!d2d) {
        return std::unexpected(d2d.error());
    }

    RenderDevice device;
    device.m_d3dDevice = *d3dDevice;
    device.m_d2dDevice = d2d->device;
    device.m_dc        = d2d->context;
    // m_swapChain / m_targetBitmap stay null - no presentation target.
    return device;
}

RenderExpected<void> RenderDevice::resize(std::uint32_t width, std::uint32_t height) noexcept {
    if (!m_swapChain) {
        return std::unexpected(
            RenderError{.stage = RenderStage::ResizeBuffers, .hr = E_NOT_VALID_STATE});
    }
    const auto [w, h] = sanitizeSwapChainSize(width, height);

    // The D2D bitmap holds a reference to the current back buffer; DXGI
    // rejects ResizeBuffers while any such reference is outstanding.
    m_dc->SetTarget(nullptr);
    m_targetBitmap.Reset();

    const HRESULT hr = m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::ResizeBuffers, .hr = hr});
    }

    auto bitmap = bindTargetBitmap(m_dc, m_swapChain);
    if (!bitmap) {
        return std::unexpected(bitmap.error());
    }
    m_targetBitmap = *bitmap;
    return {};
}

void RenderDevice::setDpi(float dpiScale) noexcept {
    // 96 DPI (USER_DEFAULT_SCREEN_DPI) == scale 1.0, matching resize_math.h::dpiToScale().
    const float dpi = 96.0F * dpiScale;
    m_dc->SetDpi(dpi, dpi);
}

RenderExpected<ID2D1DeviceContext6*> RenderDevice::beginFrame() noexcept {
    if (!m_swapChain) {
        return std::unexpected(
            RenderError{.stage = RenderStage::Present, .hr = E_NOT_VALID_STATE});
    }
    if (m_frameOpen) {
        return std::unexpected(
            RenderError{.stage = RenderStage::Present, .hr = E_NOT_VALID_STATE});
    }

    m_dc->BeginDraw();
    m_frameOpen = true;
    return m_dc.Get();
}

RenderExpected<void> RenderDevice::endFrame() noexcept {
    if (!m_frameOpen) {
        return std::unexpected(
            RenderError{.stage = RenderStage::Present, .hr = E_NOT_VALID_STATE});
    }
    m_frameOpen = false;

    const HRESULT endHr = m_dc->EndDraw();
    if (FAILED(endHr)) {
        return std::unexpected(RenderError{.stage = RenderStage::Present, .hr = endHr});
    }

    // Plain Present1(1, 0, ...): v-sync interval 1, no special flags, empty
    // dirty-rect list (present the whole frame). DXGI_PRESENT_DO_NOT_SEQUENCE
    // (basic_design.md sec.4.4) is deferred until dirty-rect partial
    // presentation exists (Phase 3c) - it has no benefit for a full-frame
    // clear and risks flag/swap-effect mismatches this phase doesn't need to
    // take on.
    const DXGI_PRESENT_PARAMETERS presentParams{};
    const HRESULT presentHr = m_swapChain->Present1(1, 0, &presentParams);
    if (FAILED(presentHr) && presentHr != DXGI_STATUS_OCCLUDED) {
        return std::unexpected(RenderError{.stage = RenderStage::Present, .hr = presentHr});
    }
    return {};
}

}  // namespace neomifes::render
