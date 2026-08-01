#include "neomifes/plugin/plugin_error.h"

#include <array>
#include <cstdio>

namespace neomifes::plugin {

namespace {

[[nodiscard]] const char* codeName(PluginErrorCode code) noexcept {
    switch (code) {
        case PluginErrorCode::LoadLibraryFailed:  return "LoadLibraryFailed";
        case PluginErrorCode::MissingExport:      return "MissingExport";
        case PluginErrorCode::NullInfoOrVTable:   return "NullInfoOrVTable";
        case PluginErrorCode::ApiVersionMismatch: return "ApiVersionMismatch";
        case PluginErrorCode::AlreadyLoaded:      return "AlreadyLoaded";
        case PluginErrorCode::NotLoaded:          return "NotLoaded";
        case PluginErrorCode::OnLoadCrashed:      return "OnLoadCrashed";
        case PluginErrorCode::OnUnloadCrashed:    return "OnUnloadCrashed";
    }
    return "Unknown";
}

// FormatMessageW's system table text for Win32 error codes is always
// English/ASCII regardless of user locale, so a straightforward narrowing
// via WideCharToMultiByte is sufficient for this log-only diagnostic string
// (same reasoning render_error.cpp's narrowMessage() uses).
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

[[nodiscard]] std::string describeWin32Error(DWORD win32Error) {
    std::array<wchar_t, 256> msgBuf{};
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, win32Error, 0,
        msgBuf.data(), static_cast<DWORD>(msgBuf.size()), nullptr);
    std::string message = narrowMessage(msgBuf.data(), len);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }
    return message;
}

}  // namespace

std::string describe(const PluginError& err) {
    std::string result = "code=";
    result += codeName(err.code);

    if (err.code == PluginErrorCode::LoadLibraryFailed || err.code == PluginErrorCode::MissingExport) {
        std::array<char, 32> errBuf{};
        const int errLen = std::snprintf(errBuf.data(), errBuf.size(), " win32Error=%lu",
                                         static_cast<unsigned long>(err.win32Error));
        result.append(errBuf.data(), errLen > 0 ? static_cast<std::size_t>(errLen) : 0);
        const std::string message = describeWin32Error(err.win32Error);
        if (!message.empty()) {
            result += " (";
            result += message;
            result += ")";
        }
    } else if (err.code == PluginErrorCode::ApiVersionMismatch) {
        std::array<char, 32> versionBuf{};
        const int versionLen = std::snprintf(versionBuf.data(), versionBuf.size(),
                                             " reportedApiVersion=%u", err.reportedApiVersion);
        result.append(versionBuf.data(), versionLen > 0 ? static_cast<std::size_t>(versionLen) : 0);
    }
    return result;
}

}  // namespace neomifes::plugin
