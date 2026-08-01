#pragma once

// NeoMIFES Plugin SDK - C ABI header (Phase 8a: minimal plugin host PoC).
//
// STABLE, DISTRIBUTABLE contract between NeoMIFES.exe and third-party
// plugin DLLs. Zero dependencies on any other NeoMIFES header (src/**) - a
// plugin author's project needs only this file + the Win32 SDK.
//
// Phase 8a trims docs/design/master_roadmap.md sec.8.3's full sketch (see
// ADR-015 for what is deferred and why):
//   - NeoMifesPluginInfo: id/name/version/author/apiVersion only. NO
//     `permissions` bitfield (sandboxing/permission model is a later
//     Phase 8 sub-phase).
//   - NeoMifesPluginVTable: onLoad/onUnload only. NO onDocumentChanged
//     (needs async worker + PostMessageW plumbing this PoC does not build).
//   - NO NeoMifesCoreApi: the roadmap sketch's insertText/getLineText/...
//     (expressed in terms of line+column) does not match
//     document::Document's actual public API (no getLineText(), no
//     line+column<->offset conversion) - designing that bridge properly is
//     real work deferred to its own sub-phase (CLAUDE.md rule 3: no
//     guessing). See docs/issues/plugin_core_api_document_gap.md.
//   - NeoMifesPluginContext is a TRANSPARENT struct with a `userData`
//     field, not the roadmap's opaque forward-declared handle - a common
//     C-ABI idiom (cf. Win32 GWLP_USERDATA, libuv's void* data). Deliberate
//     deviation, documented in ADR-015.
//
// apiVersion contract: NeoMifesPluginInfo::apiVersion must equal
// NEOMIFES_PLUGIN_API_VERSION EXACTLY. neomifes::plugin::PluginHost::load()
// rejects any other value - see ADR-015 "apiVersion strategy" for why
// exact-match (not a min/max range) was chosen for this first version.
//
// x64-only (CLAUDE.md / root CMakeLists.txt enforce this): no calling-
// convention decorator on any function pointer below - __cdecl/__stdcall
// are unified into a single ABI on x64, unlike x86.

#include <stddef.h>  // wchar_t (not a keyword outside C++)

#ifdef __cplusplus
extern "C" {
#endif

#define NEOMIFES_PLUGIN_API_VERSION 1u

// Host-owned, plugin-writable. `userData` is a plain C-ABI idiom, not scope
// creep toward a richer host<->plugin API. The host zero-initializes it
// before calling onLoad and leaves it untouched between onLoad and
// onUnload, so a plugin can round-trip an opaque token through it.
typedef struct NeoMifesPluginContext {
    void* userData;
} NeoMifesPluginContext;

typedef struct NeoMifesPluginInfo {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* version;
    const wchar_t* author;
    unsigned int   apiVersion;
} NeoMifesPluginInfo;

typedef struct NeoMifesPluginVTable {
    void (*onLoad)(NeoMifesPluginContext* ctx);
    void (*onUnload)(NeoMifesPluginContext* ctx);
} NeoMifesPluginVTable;

// Every plugin DLL must export exactly these two functions (resolved by
// name via GetProcAddress, not by ordinal).
__declspec(dllexport) const NeoMifesPluginInfo*   neomifes_plugin_info(void);
__declspec(dllexport) const NeoMifesPluginVTable* neomifes_plugin_vtable(void);

#ifdef __cplusplus
}
#endif
