#pragma once

// PluginHost - Phase 8a minimal plugin host. Loads exactly one native
// plugin DLL via LoadLibraryW, resolves its two required C-ABI exports,
// validates apiVersion, invokes onLoad/onUnload with SEH crash isolation
// (see plugin_host.cpp), and unloads the DLL. Synchronous, UI-thread-only -
// no background thread (contrast neomifes::render::SyntaxWorker's
// std::thread + PostMessageW pattern; "load a DLL and call two functions"
// has no async need yet).
//
// See ADR-015 for what is deferred: AppContainer/Job Object sandboxing,
// separate-process IPC, manifest.json5 + Authenticode verification,
// marketplace, onDocumentChanged, NeoMifesCoreApi. See ADR-018 for the
// self-declared `permissions` bitfield (Phase 8d).

#include <neomifes/plugin_sdk.h>

#include "neomifes/platform/handle_guard.h"
#include "neomifes/plugin/plugin_error.h"

#include <filesystem>
#include <memory>

namespace neomifes::plugin {

// Pure comparison, unit-testable with no DLL involved
// (tests/unit/plugin_plugin_host_test.cpp). Exact equality, not a
// min/max-range compatibility check - see ADR-015 "apiVersion strategy".
[[nodiscard]] bool isApiVersionCompatible(unsigned int reportedApiVersion) noexcept;

class PluginHost {
public:
    PluginHost() noexcept = default;
    ~PluginHost();

    PluginHost(const PluginHost&)            = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // Movable (not copyable) - keeps a future std::vector<PluginHost>-based
    // multi-plugin manager viable without redesigning this class. Move-ctor
    // is safely `= default` (each RAII member leaves the moved-from object
    // in an empty state, so moved-from's isLoaded()==false and its
    // destructor no-ops). Move-ASSIGNMENT is hand-written: the
    // compiler-generated member-wise version would silently FreeLibrary
    // `*this`'s existing DLL without ever calling its onUnload, which is a
    // real correctness gap for a class whose entire job is "run onUnload
    // before unmapping" - so it is NOT `= default`.
    PluginHost(PluginHost&&) noexcept = default;
    PluginHost& operator=(PluginHost&&) noexcept;

    // Loads `dllPath`, resolves + validates the two required exports and
    // apiVersion, then calls vtable->onLoad(ctx) (SEH-isolated). On ANY
    // failure (including onLoad crashing) the DLL is unloaded again before
    // returning - no partial state is ever observable via isLoaded(). NOT
    // noexcept: allocates a NeoMifesPluginContext (std::make_unique) - a
    // genuine std::bad_alloc is allowed to propagate rather than being
    // swallowed (CLAUDE.md forbids catch(...); matches
    // OriginalBuffer::view()'s documented precedent).
    //
    // Phase 8d: called at most once per load() call, AFTER
    // neomifes_plugin_info() has been resolved and read (permissions are
    // only known then) - receives info->permissions and returns the
    // (possibly permission-gated) NeoMifesCoreApi to hand the plugin. A raw
    // function pointer, not std::function: this is not a hot path (called
    // once per load, contrast Phase 7u's TSInput::read - called hundreds of
    // times per parse, the actual reason THAT one avoided std::function),
    // and neomifes::app::buildPluginCoreApi already has exactly this
    // signature, so callers just pass the function name directly (e.g.
    // `host.load(path, neomifes::app::buildPluginCoreApi, doc)`).
    using CoreApiFactory = const NeoMifesCoreApi* (*)(unsigned int grantedPermissions) noexcept;

    // `coreApi`/`document` (Phase 8b, `coreApiFactory` reshaped in Phase
    // 8d), `toastSink` (Phase 8e): `document`/`toastSink` are forwarded
    // verbatim into the NeoMifesPluginContext handed to onLoad/onUnload -
    // see plugin_sdk.h's NeoMifesPluginContext comment. `coreApiFactory`
    // is invoked once info->permissions is known (see CoreApiFactory's own
    // comment above) and ITS result is what gets forwarded as
    // context->coreApi. All three default to nullptr so existing callers
    // that only pass `dllPath` (e.g.
    // tests/integration/plugin_load_test.cpp's four call sites) compile
    // unchanged. This class deliberately never dereferences `document` or
    // `toastSink` itself, nor does it know what permission bits mean -
    // doing so would require depending on neomifes::document/neomifes::ui,
    // which the layering rule (CLAUDE.md sec.3) forbids for the Plugin
    // Engine (see neomifes::app::buildPluginCoreApi()/toNeoMifesDocument()/
    // toNeoMifesToastSink(), src/app/plugin_core_api_bridge.h, for the
    // actual implementation).
    [[nodiscard]] PluginExpected<void> load(const std::filesystem::path& dllPath,
                                             CoreApiFactory      coreApiFactory = nullptr,
                                             NeoMifesDocument*   document       = nullptr,
                                             NeoMifesToastSink*  toastSink      = nullptr);

    // Calls vtable->onUnload(ctx) (SEH-isolated) then frees the DLL
    // unconditionally, even if onUnload crashed (a stuck-but-still-mapped
    // DLL is strictly worse than one that's gone - the crash is still
    // reported to the caller). No-op (NotLoaded error) if nothing is loaded.
    [[nodiscard]] PluginExpected<void> unload() noexcept;

    [[nodiscard]] bool isLoaded() const noexcept { return static_cast<bool>(m_module); }

    // Test-fixture-only introspection: the userData token the loaded
    // plugin's onLoad wrote (see plugin_sdk.h's NeoMifesPluginContext
    // comment). Returns nullptr if not loaded.
    [[nodiscard]] void* contextUserData() const noexcept;

    // Test/diagnostic introspection (Phase 8d): the loaded plugin's
    // self-declared NeoMifesPluginInfo::permissions, or
    // NEOMIFES_PLUGIN_PERMISSION_NONE if nothing is loaded. Same "expose
    // internal state for assertions" role as contextUserData() above.
    [[nodiscard]] unsigned int grantedPermissions() const noexcept;

private:
    platform::ModuleHandle                 m_module;
    const NeoMifesPluginVTable*            m_vtable = nullptr;  // non-owning: valid only while m_module is loaded
    std::unique_ptr<NeoMifesPluginContext> m_context;
    unsigned int                           m_grantedPermissions = 0;
};

}  // namespace neomifes::plugin
