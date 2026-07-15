#include "neomifes/render/render_pipeline.h"

#include <utility>

namespace neomifes::render {

RenderExpected<void> RenderPipeline::attach(HWND hwnd) noexcept {
    RECT rect{};
    if (::GetClientRect(hwnd, &rect) == 0) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached,
                                           .hr = HRESULT_FROM_WIN32(::GetLastError())});
    }
    m_hwnd   = hwnd;
    m_width  = static_cast<std::uint32_t>(rect.right - rect.left);
    m_height = static_cast<std::uint32_t>(rect.bottom - rect.top);

    auto device = RenderDevice::create(m_hwnd, m_width, m_height);
    if (!device) {
        return std::unexpected(device.error());
    }
    m_device = std::move(*device);
    return {};
}

RenderExpected<void> RenderPipeline::resize(std::uint32_t width, std::uint32_t height) noexcept {
    m_width  = width;
    m_height = height;
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }
    auto result = m_device->resize(width, height);
    if (!result && result.error().isDeviceLost()) {
        return recreateDevice();
    }
    return result;
}

RenderExpected<void> RenderPipeline::render() noexcept {
    if (!m_device) {
        return std::unexpected(RenderError{.stage = RenderStage::NotAttached, .hr = E_NOT_VALID_STATE});
    }
    // Matches the previous GDI placeholder fill (RGB 30,30,30) so the
    // GDI->D2D handoff (see ADR-009) is visually seamless. Real content
    // drawing replaces this in Phase 3b.
    constexpr D2D1_COLOR_F kPlaceholderColor = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F,
                                                1.0F};

    auto result = m_device->clearAndPresent(kPlaceholderColor);
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
        return m_device->clearAndPresent(kPlaceholderColor);
    }
    return result;
}

RenderExpected<void> RenderPipeline::recreateDevice() noexcept {
    m_device.reset();
    auto device = RenderDevice::create(m_hwnd, m_width, m_height);
    if (!device) {
        return std::unexpected(device.error());
    }
    m_device = std::move(*device);
    return {};
}

}  // namespace neomifes::render
