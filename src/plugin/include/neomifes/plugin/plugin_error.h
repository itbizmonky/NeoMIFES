#pragma once

// Error type for neomifes::plugin, following the std::expected idiom
// established by neomifes::render::RenderExpected (render_error.h) - see
// that file's header comment for the CLAUDE.md sec.4 rationale. Kept
// independent of render::RenderError (not reused) since neomifes::plugin
// must not depend on neomifes::render.

#include <cstdint>
#include <expected>
#include <string>

#include <windows.h>

namespace neomifes::plugin {

enum class PluginErrorCode : std::uint8_t {
    LoadLibraryFailed,   // LoadLibraryW itself failed (win32Error set)
    MissingExport,       // GetProcAddress couldn't resolve a required export (win32Error set)
    NullInfoOrVTable,    // export resolved but returned nullptr, or a vtable callback is null
    ApiVersionMismatch,  // NeoMifesPluginInfo::apiVersion != NEOMIFES_PLUGIN_API_VERSION (reportedApiVersion set)
    AlreadyLoaded,       // load() called while this PluginHost already owns a plugin
    NotLoaded,           // unload() called while nothing is loaded
    OnLoadCrashed,       // SEH caught a fault/exception inside vtable->onLoad
    OnUnloadCrashed,     // SEH caught a fault/exception inside vtable->onUnload
};

struct PluginError {
    PluginErrorCode code;
    DWORD           win32Error         = 0;  // only for LoadLibraryFailed/MissingExport
    unsigned int    reportedApiVersion = 0;  // only for ApiVersionMismatch
};

// Human-readable diagnostic string. For logging only - format has no
// stability guarantee, never parse it (same contract as render::describe).
[[nodiscard]] std::string describe(const PluginError& err);

template <typename T>
using PluginExpected = std::expected<T, PluginError>;

}  // namespace neomifes::plugin
