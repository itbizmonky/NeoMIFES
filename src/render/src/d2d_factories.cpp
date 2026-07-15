#include "neomifes/render/d2d_factories.h"

namespace neomifes::render {

namespace {

struct D2DFactoryHolder {
    Microsoft::WRL::ComPtr<ID2D1Factory7> ptr;
    HRESULT                               hr = E_FAIL;
};

// Magic-static init is thread-safe (guaranteed since C++11) and runs the
// D2D1CreateFactory call exactly once per process regardless of how many
// callers/threads request the factory.
const D2DFactoryHolder& d2dFactoryHolder() {
    static const D2DFactoryHolder holder = [] {
        D2DFactoryHolder h;
        D2D1_FACTORY_OPTIONS options{};
#ifndef NDEBUG
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        h.hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory7),
                                   &options, reinterpret_cast<void**>(h.ptr.GetAddressOf()));
        return h;
    }();
    return holder;
}

struct DWriteFactoryHolder {
    Microsoft::WRL::ComPtr<IDWriteFactory7> ptr;
    HRESULT                                 hr = E_FAIL;
};

const DWriteFactoryHolder& dwriteFactoryHolder() {
    static const DWriteFactoryHolder holder = [] {
        DWriteFactoryHolder h;
        h.hr = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory7),
                                     reinterpret_cast<IUnknown**>(h.ptr.GetAddressOf()));
        return h;
    }();
    return holder;
}

}  // namespace

RenderExpected<Microsoft::WRL::ComPtr<ID2D1Factory7>> sharedD2DFactory() noexcept {
    const auto& holder = d2dFactoryHolder();
    if (FAILED(holder.hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::D2DFactory, .hr = holder.hr});
    }
    return holder.ptr;
}

RenderExpected<Microsoft::WRL::ComPtr<IDWriteFactory7>> sharedDWriteFactory() noexcept {
    const auto& holder = dwriteFactoryHolder();
    if (FAILED(holder.hr)) {
        return std::unexpected(RenderError{.stage = RenderStage::DWriteFactory, .hr = holder.hr});
    }
    return holder.ptr;
}

}  // namespace neomifes::render
