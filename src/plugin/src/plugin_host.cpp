#include "neomifes/plugin/plugin_host.h"

#include <windows.h>

#include <utility>

namespace neomifes::plugin {

namespace {

// SEH trampoline around a single plugin callback invocation. Unlike
// OriginalBuffer's page-fault-specific filter (EXCEPTION_IN_PAGE_ERROR
// only - see original_buffer.cpp), this filter is UNCONDITIONAL
// (EXCEPTION_EXECUTE_HANDLER for any exception code): OriginalBuffer's
// filter exists for a specific, well-understood hardware fault in TRUSTED
// code; here the callee is untrusted third-party code that might do
// anything, so "no matter how badly it misbehaves, the host survives" is
// the actual requirement, and filtering by code would defeat that.
//
// Must have no local C++ objects with non-trivial destructors (MSVC's
// restriction on __try/__except mixed with object unwinding) - `fn`/`ctx`
// are raw pointers and `crashed` is a caller-owned out-parameter, so this
// is trivially satisfied. Free function (not a PluginHost member) since it
// needs no PluginHost internals.
void invokePluginCallbackSafe(void (*fn)(NeoMifesPluginContext*), NeoMifesPluginContext* ctx,
                              bool& crashed) noexcept {
    crashed = false;
    __try {
        fn(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        crashed = true;
    }
}

}  // namespace

bool isApiVersionCompatible(unsigned int reportedApiVersion) noexcept {
    return reportedApiVersion == NEOMIFES_PLUGIN_API_VERSION;
}

PluginHost::~PluginHost() {
    if (isLoaded()) {
        const auto result = unload();
        (void)result;  // destructor can't propagate further - best-effort, matches HandleGuard Deleters
    }
}

PluginHost& PluginHost::operator=(PluginHost&& other) noexcept {
    if (this != &other) {
        const auto result = unload();  // best-effort teardown of *this*'s prior state
        (void)result;
        m_module       = std::move(other.m_module);
        m_vtable       = other.m_vtable;
        m_context      = std::move(other.m_context);
        other.m_vtable = nullptr;
    }
    return *this;
}

PluginExpected<void> PluginHost::load(const std::filesystem::path& dllPath,
                                       const NeoMifesCoreApi* coreApi, NeoMifesDocument* document) {
    if (isLoaded()) {
        return std::unexpected(PluginError{.code = PluginErrorCode::AlreadyLoaded});
    }

    platform::ModuleHandle module(::LoadLibraryW(dllPath.c_str()));
    if (!module) {
        return std::unexpected(
            PluginError{.code = PluginErrorCode::LoadLibraryFailed, .win32Error = ::GetLastError()});
    }

    using InfoFn   = const NeoMifesPluginInfo* (*)();
    using VTableFn = const NeoMifesPluginVTable* (*)();

    // FARPROC->function-pointer reinterpret_cast has direct precedent:
    // main.cpp's enableHighDpi() does the same for
    // SetProcessDpiAwarenessContext.
    const auto infoFn = reinterpret_cast<InfoFn>(::GetProcAddress(module.get(), "neomifes_plugin_info"));
    const auto vtableFn =
        reinterpret_cast<VTableFn>(::GetProcAddress(module.get(), "neomifes_plugin_vtable"));
    if (infoFn == nullptr || vtableFn == nullptr) {
        return std::unexpected(
            PluginError{.code = PluginErrorCode::MissingExport, .win32Error = ::GetLastError()});
    }

    const NeoMifesPluginInfo*   info   = infoFn();
    const NeoMifesPluginVTable* vtable = vtableFn();
    if (info == nullptr || vtable == nullptr || vtable->onLoad == nullptr ||
        vtable->onUnload == nullptr) {
        return std::unexpected(PluginError{.code = PluginErrorCode::NullInfoOrVTable});
    }
    if (!isApiVersionCompatible(info->apiVersion)) {
        return std::unexpected(PluginError{
            .code = PluginErrorCode::ApiVersionMismatch, .reportedApiVersion = info->apiVersion});
    }

    auto context       = std::make_unique<NeoMifesPluginContext>();
    context->userData = nullptr;
    context->coreApi  = coreApi;
    context->document = document;

    bool crashed = false;
    invokePluginCallbackSafe(vtable->onLoad, context.get(), crashed);
    if (crashed) {
        // Don't call onUnload for a plugin whose onLoad already faulted -
        // its state is unknown/inconsistent. `module` unwinds -> FreeLibrary.
        return std::unexpected(PluginError{.code = PluginErrorCode::OnLoadCrashed});
    }

    m_module  = std::move(module);
    m_vtable  = vtable;
    m_context = std::move(context);
    return {};
}

PluginExpected<void> PluginHost::unload() noexcept {
    if (!isLoaded()) {
        return std::unexpected(PluginError{.code = PluginErrorCode::NotLoaded});
    }

    bool crashed = false;
    invokePluginCallbackSafe(m_vtable->onUnload, m_context.get(), crashed);

    m_vtable = nullptr;
    m_context.reset();
    m_module.reset();  // FreeLibrary

    if (crashed) {
        return std::unexpected(PluginError{.code = PluginErrorCode::OnUnloadCrashed});
    }
    return {};
}

void* PluginHost::contextUserData() const noexcept {
    return m_context ? m_context->userData : nullptr;
}

}  // namespace neomifes::plugin
