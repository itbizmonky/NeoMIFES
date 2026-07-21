#include "neomifes/platform/app_data_dir.h"

#include <combaseapi.h>
#include <shlobj.h>

#include <system_error>

namespace neomifes::platform {

std::optional<std::filesystem::path> resolveAppDataDir() {
    PWSTR rawPath = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &rawPath);
    if (FAILED(hr) || rawPath == nullptr) {
        if (rawPath != nullptr) {
            ::CoTaskMemFree(rawPath);
        }
        return std::nullopt;
    }
    // Not const: `return dir;` below relies on automatic move (NRVO
    // fallback) into the std::optional, which a const local would suppress.
    std::filesystem::path dir = std::filesystem::path(rawPath) / L"NeoMIFES";
    ::CoTaskMemFree(rawPath);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // create_directories() sets ec both on genuine failure and (in some
    // implementations) when the directory already exists as a no-op success
    // path that still touches ec - is_directory() is the authoritative check
    // for whether `dir` is actually usable, not ec alone.
    if (!std::filesystem::is_directory(dir, ec)) {
        return std::nullopt;
    }
    return dir;
}

}  // namespace neomifes::platform
