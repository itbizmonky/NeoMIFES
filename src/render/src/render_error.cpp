#include "neomifes/render/render_error.h"

#include <d2derr.h>
#include <dxgi.h>

#include <array>
#include <cstdio>

namespace neomifes::render {

bool RenderError::isDeviceLost() const noexcept {
    switch (hr) {
        case DXGI_ERROR_DEVICE_REMOVED:
        case DXGI_ERROR_DEVICE_RESET:
        case DXGI_ERROR_DEVICE_HUNG:
        case D2DERR_RECREATE_TARGET:
            return true;
        default:
            return false;
    }
}

namespace {

[[nodiscard]] const char* stageName(RenderStage stage) noexcept {
    switch (stage) {
        case RenderStage::D2DFactory:       return "D2DFactory";
        case RenderStage::DWriteFactory:    return "DWriteFactory";
        case RenderStage::D3D11Device:      return "D3D11Device";
        case RenderStage::DxgiSwapChain:    return "DxgiSwapChain";
        case RenderStage::D2DDevice:        return "D2DDevice";
        case RenderStage::D2DDeviceContext: return "D2DDeviceContext";
        case RenderStage::BackBufferBitmap: return "BackBufferBitmap";
        case RenderStage::ResizeBuffers:    return "ResizeBuffers";
        case RenderStage::Present:          return "Present";
        case RenderStage::NotAttached:      return "NotAttached";
    }
    return "Unknown";
}

// FormatMessageW's system table text for the HRESULTs this layer produces
// (DXGI_ERROR_*, D2DERR_*, generic Win32-mapped HRESULTs) is always
// English/ASCII regardless of user locale, so a straightforward narrowing
// via WideCharToMultiByte is sufficient for this log-only diagnostic string.
[[nodiscard]] std::string narrowMessage(const wchar_t* wide, DWORD wideLen) {
    if (wideLen == 0) {
        return {};
    }
    const int narrowLen = ::WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(wideLen),
                                                 nullptr, 0, nullptr, nullptr);
    if (narrowLen <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(narrowLen), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(wideLen), result.data(), narrowLen,
                          nullptr, nullptr);
    return result;
}

}  // namespace

std::string describe(const RenderError& err) {
    std::array<wchar_t, 256> msgBuf{};
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(err.hr), 0, msgBuf.data(), static_cast<DWORD>(msgBuf.size()), nullptr);

    std::string message = narrowMessage(msgBuf.data(), len);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }

    std::array<char, 32> hrBuf{};
    const int hrLen = std::snprintf(hrBuf.data(), hrBuf.size(), "0x%08lX",
                                    static_cast<unsigned long>(err.hr));
    std::string result = "stage=";
    result += stageName(err.stage);
    result += " hr=";
    result.append(hrBuf.data(), hrLen > 0 ? static_cast<std::size_t>(hrLen) : 0);
    if (!message.empty()) {
        result += " (";
        result += message;
        result += ")";
    }
    return result;
}

}  // namespace neomifes::render
