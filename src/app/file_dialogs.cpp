#include "neomifes/app/file_dialogs.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace neomifes::app {

namespace {

using Microsoft::WRL::ComPtr;

// First COM usage in this codebase requiring CoInitializeEx (WI-02, see
// file_dialogs.h's header comment for why the existing D2D/DXGI/D3D11 COM
// usage never needed this). RAII, scoped to a single dialog call - COM
// permits repeated CoInitializeEx calls on the same thread (each balanced
// by its own CoUninitialize, including an S_FALSE "already initialized"
// result per MSDN), so there is no need for a process-wide singleton
// guard for these two infrequent, modal, single-threaded (UI thread only,
// ADR-009) call sites.
class ComInitGuard {
public:
    ComInitGuard() noexcept : m_hr(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ComInitGuard(const ComInitGuard&)            = delete;
    ComInitGuard& operator=(const ComInitGuard&) = delete;
    ComInitGuard(ComInitGuard&&)                 = delete;
    ComInitGuard& operator=(ComInitGuard&&)      = delete;
    ~ComInitGuard() {
        if (SUCCEEDED(m_hr)) {
            ::CoUninitialize();
        }
    }

    [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(m_hr); }

private:
    HRESULT m_hr;
};

// Shared IFileOpenDialog/IFileSaveDialog result extraction - both
// interfaces expose an identical GetResult()->IShellItem->
// GetDisplayName(SIGDN_FILESYSPATH) path, so IFileDialog (their common
// base) is enough here.
[[nodiscard]] std::optional<std::filesystem::path> extractResultPath(IFileDialog& dialog) noexcept {
    ComPtr<IShellItem> item;
    if (FAILED(dialog.GetResult(item.GetAddressOf()))) {
        return std::nullopt;
    }
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || rawPath == nullptr) {
        return std::nullopt;
    }
    std::filesystem::path result(rawPath);
    ::CoTaskMemFree(rawPath);
    return result;
}

}  // namespace

std::optional<std::filesystem::path> showOpenFileDialog(HWND owner) {
    const ComInitGuard com;
    if (!com.ok()) {
        return std::nullopt;
    }
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return std::nullopt;
    }
    // FAILED() here also covers the user cancelling (Show() returns
    // HRESULT_FROM_WIN32(ERROR_CANCELLED)) - both are "nothing to open",
    // see this function's own header comment.
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return extractResultPath(*dialog.Get());
}

std::optional<std::filesystem::path> showSaveFileDialog(
    HWND owner, const std::optional<std::filesystem::path>& suggestedPath) {
    const ComInitGuard com;
    if (!com.ok()) {
        return std::nullopt;
    }
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.GetAddressOf())))) {
        return std::nullopt;
    }
    if (suggestedPath) {
        const std::filesystem::path parent = suggestedPath->parent_path();
        if (!parent.empty()) {
            ComPtr<IShellItem> folder;
            // Best-effort only - a failure here just leaves the dialog at
            // its OS-default starting folder, not a reason to abort the
            // whole Save As.
            if (SUCCEEDED(::SHCreateItemFromParsingName(parent.c_str(), nullptr,
                                                        IID_PPV_ARGS(folder.GetAddressOf())))) {
                dialog->SetFolder(folder.Get());
            }
        }
        dialog->SetFileName(suggestedPath->filename().c_str());
    }
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return extractResultPath(*dialog.Get());
}

}  // namespace neomifes::app
