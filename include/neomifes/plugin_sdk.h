#pragma once

// NeoMIFES Plugin SDK - C ABI header (Phase 8a: minimal plugin host PoC;
// Phase 8b: NeoMifesCoreApi document-manipulation bridge; Phase 8d:
// self-declared permissions bitfield; Phase 8e: showToast, headless;
// Phase 8f: registerCommand, deferred SEH-protected invocation via
// ui::PluginCommandRegistry, see ADR-020).
//
// STABLE, DISTRIBUTABLE contract between NeoMIFES.exe and third-party
// plugin DLLs. Zero dependencies on any other NeoMIFES header (src/**) - a
// plugin author's project needs only this file + the Win32 SDK.
//
// Phase 8a trims docs/design/master_roadmap.md sec.8.3's full sketch (see
// ADR-015 for what is deferred and why):
//   - NeoMifesPluginInfo: id/name/version/author/apiVersion only at first
//     (permissions added in Phase 8d, see below).
//   - NeoMifesPluginVTable: onLoad/onUnload only. NO onDocumentChanged
//     (needs async worker + PostMessageW plumbing this PoC does not build).
//   - NeoMifesPluginContext is a TRANSPARENT struct with a `userData`
//     field, not the roadmap's opaque forward-declared handle - a common
//     C-ABI idiom (cf. Win32 GWLP_USERDATA, libuv's void* data). Deliberate
//     deviation, documented in ADR-015.
//
// Phase 8b adds NeoMifesCoreApi (insertText/deleteRange/getLineCount/
// getLineText only - see that struct's own comment below for the full
// contract, and ADR-016 for what is still deferred: registerCommand/
// showToast/network+filesystem functions/permissions).
//
// Phase 8d adds NeoMifesPluginInfo::permissions (self-declared bitfield,
// see NEOMIFES_PLUGIN_PERMISSION_* below) and gates NeoMifesCoreApi's 4
// functions on NEOMIFES_PLUGIN_PERMISSION_DOCUMENT - see ADR-018 for the
// full design (what is/isn't a security boundary, why manifest.json5 +
// signature verification are still deferred).
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

// Phase 8b: independent from NEOMIFES_PLUGIN_API_VERSION on purpose - the
// CoreApi surface below (insertText/deleteRange/getLineCount/getLineText
// today, registerCommand/showToast/network functions in later sub-phases
// per master_roadmap.md sec.8.3) is expected to grow on its own schedule,
// separate from onLoad/onUnload/NeoMifesPluginInfo compatibility. It
// exists so a future plugin can read ctx->coreApi->apiVersion defensively
// before calling a function that might not exist in an older host.
//
// Phase 8e: bumped 1 -> 2, the first actual growth of this struct
// (showToast added below) - a plugin checking this value can now tell
// whether showToast exists before calling it.
//
// Phase 8f: bumped 2 -> 3 (registerCommand added below).
#define NEOMIFES_CORE_API_VERSION 3u

// Phase 8d: self-declared capability request bitfield (NeoMifesPluginInfo::
// permissions below). Matches master_roadmap.md sec.8.3's original 5-category
// sketch (Network/Filesystem/Subprocess/Registry/Clipboard) for future
// naming stability, PLUS a new NEOMIFES_PLUGIN_PERMISSION_DOCUMENT bit not
// in that sketch - see ADR-018 for why: the sketch's 5 categories don't
// cover the ONE capability that actually exists today (NeoMifesCoreApi's
// document-editing functions), so gating only those 5 would gate nothing at
// all yet. Only NEOMIFES_PLUGIN_PERMISSION_DOCUMENT is actually enforced
// anywhere in this header today (see NeoMifesCoreApi's own comment below);
// the other 5 are reserved placeholders with no corresponding CoreApi
// function to gate - declaring them costs nothing and avoids renaming
// later once httpRequest/readPluginData/writePluginData etc. exist.
//
// SELF-DECLARED, NOT VERIFIED: the host trusts this value as reported by
// the plugin's own neomifes_plugin_info() export - there is no manifest
// file, no signature check, no user-consent dialog yet (all deferred, see
// ADR-018). This is NOT a defense against a malicious plugin (which could
// simply declare NEOMIFES_PLUGIN_PERMISSION_DOCUMENT unconditionally) - see
// NeoMifesCoreApi's "NOT A SECURITY BOUNDARY" comment, unchanged by this.
#define NEOMIFES_PLUGIN_PERMISSION_NONE       0x00000000u
#define NEOMIFES_PLUGIN_PERMISSION_DOCUMENT   0x00000001u  // gates insertText/deleteRange/getLineCount/getLineText below
#define NEOMIFES_PLUGIN_PERMISSION_NETWORK    0x00000002u  // reserved - no CoreApi function exists yet to gate
#define NEOMIFES_PLUGIN_PERMISSION_FILESYSTEM 0x00000004u  // reserved
#define NEOMIFES_PLUGIN_PERMISSION_SUBPROCESS 0x00000008u  // reserved
#define NEOMIFES_PLUGIN_PERMISSION_REGISTRY   0x00000010u  // reserved
#define NEOMIFES_PLUGIN_PERMISSION_CLIPBOARD  0x00000020u  // reserved

// Opaque handle for the live document a plugin callback was invoked
// against. Never defined in this header (deliberately incomplete) - the
// real type is neomifes::document::Document, reinterpret_cast to/from this
// pointer type entirely inside src/app/plugin_core_api_bridge.cpp (this
// repo's internal implementation, not part of the distributable SDK
// contract). Plugin authors only ever pass this pointer through.
typedef struct NeoMifesDocument NeoMifesDocument;

// Opaque handle for the toast-notification sink a plugin callback was
// invoked against (Phase 8e). Same "never defined here, reinterpret_cast
// entirely inside src/app/plugin_core_api_bridge.cpp" idiom as
// NeoMifesDocument above - the real type is neomifes::ui::ToastState.
typedef struct NeoMifesToastSink NeoMifesToastSink;

// Opaque handle for the plugin-registered-command registry a plugin
// callback was invoked against (Phase 8f). Same "never defined here,
// reinterpret_cast entirely inside src/app/plugin_core_api_bridge.cpp"
// idiom as NeoMifesDocument/NeoMifesToastSink above - the real type is
// neomifes::ui::PluginCommandRegistry.
typedef struct NeoMifesCommandRegistry NeoMifesCommandRegistry;

// Forward declaration only (defined further below) - needed here because
// NeoMifesCoreApi::registerCommand (Phase 8f) is the first NeoMifesCoreApi
// function to take a NeoMifesPluginContext* parameter, and this struct is
// declared before NeoMifesPluginContext's own full definition.
typedef struct NeoMifesPluginContext NeoMifesPluginContext;

// Document-manipulation functions available to a loaded plugin (Phase 8b),
// plus showToast (Phase 8e) and registerCommand (Phase 8f, both different,
// ungated capabilities - see their own comments below). See
// docs/decisions/ADR-016-plugin-core-api-bridge.md,
// ADR-019-plugin-show-toast-headless.md, and
// ADR-020-plugin-register-command.md for the full design rationale;
// master_roadmap.md sec.8.3 for the roadmap's fuller future sketch
// (network/filesystem functions - not implemented yet).
//
// THREADING CONTRACT: every function here may be called ONLY from inside a
// plugin callback (today: onLoad/onUnload - there is no onDocumentChanged
// yet), which itself only ever runs synchronously on whichever thread
// called PluginHost::load()/unload() (today: always the UI thread, since
// nothing calls PluginHost from a background thread). Calling any of these
// from any other thread, or after the callback that received `ctx` has
// returned, is undefined behavior - neomifes::document::Document itself is
// single-UI-thread-only (ADR-009) and none of these functions add
// synchronization of their own.
//
// EXCEPTION (Phase 8f): registerCommand's own `callback` parameter is
// intentionally invoked LATER - not synchronously, and after the plugin
// callback that called registerCommand has already returned - when the
// user runs the registered command. This is safe as long as it happens on
// the UI thread (true today: nothing drives command invocation off the UI
// thread) and the owning plugin is still loaded. If the plugin has since
// been unloaded, invoking a stale registered command's callback is
// undefined behavior (PluginHost::unload() frees the NeoMifesPluginContext
// the callback closed over) - the unconditional SEH trampoline
// (neomifes::plugin::invokePluginCallbackSafe) reduces the chance this
// crashes the host process, but does NOT guarantee it, and provides no
// guarantee about the correctness of anything the callback does before or
// if it faults. This phase does not automatically remove a plugin's
// registered commands on unload - see ADR-020.
//
// NOT A SECURITY BOUNDARY: a loaded plugin can already freely edit the
// live document through this struct with no permission gating - see
// ADR-016 (mirrors ADR-015's own "SEH trampoline is not a security
// boundary" disclaimer, for the same underlying reason: same-process,
// same-address-space execution).
//
// PERMISSION-GATED (Phase 8d): all 4 functions below are NULL unless the
// plugin declared NEOMIFES_PLUGIN_PERMISSION_DOCUMENT in its
// NeoMifesPluginInfo::permissions (see neomifes::app::buildPluginCoreApi()).
// A plugin that calls one anyway crashes on a null-pointer call - caught by
// PluginHost's existing unconditional SEH trampoline (Phase 8a) and
// reported as PluginErrorCode::OnLoadCrashed, so no new error code was
// needed for this. This is still not a security boundary against a
// malicious plugin (see this struct's own comment above and
// NEOMIFES_PLUGIN_PERMISSION_DOCUMENT's comment) - it only prevents
// ACCIDENTAL use by a plugin that never declared it needed document access.
//
// Text encoding: UTF-16 (wchar_t - matching this repo's internal
// std::u16string convention on Windows, both are 16-bit code units).
// `line`/`column` are UTF-16 code-unit positions, 0-based, same convention
// as document::TextPos/LineNumber throughout this codebase. Out-of-range
// line/column values are clamped, never a failure mode (see
// neomifes::document::Document::lineColumnToOffset()'s doc comment for the
// exact clamp rule) - deleteRange additionally normalizes a resolved
// end-before-start pair by swapping the two rather than passing it through
// as-is (an untrusted plugin's line/column input could otherwise silently
// delete nothing at all - see ADR-016).
typedef struct NeoMifesCoreApi {
    unsigned int apiVersion;

    // Inserts `text` (must be null-terminated - no length parameter) at
    // (line, column). No-op if `doc` or `text` is NULL.
    void (*insertText)(NeoMifesDocument* doc, const wchar_t* text, unsigned line, unsigned column);

    // Deletes the half-open range from (lineStart, columnStart) to
    // (lineEnd, columnEnd) (see this struct's own comment for the
    // end-before-start normalization). No-op if `doc` is NULL.
    void (*deleteRange)(NeoMifesDocument* doc, unsigned lineStart, unsigned columnStart,
                        unsigned lineEnd, unsigned columnEnd);

    // Returns 0 if `doc` is NULL. Saturates at UINT_MAX for a document with
    // more lines than fit in a 32-bit `unsigned` (see ADR-016 for why the
    // C ABI uses 32-bit line counts while document::Document itself uses
    // 64-bit).
    unsigned int (*getLineCount)(NeoMifesDocument* doc);

    // Win32-style bounded copy: writes up to bufferLen-1 UTF-16 code units
    // of line `line`'s text into `buffer`, always null-terminating when
    // bufferLen >= 1, and returns the number of code units actually
    // written (EXCLUDING the null terminator) - truncates safely, never
    // overflows `buffer`. Returns 0 and writes nothing if `doc` or
    // `buffer` is NULL, or if bufferLen == 0. Deliberate deviation from
    // master_roadmap.md sec.8.3's `void`-returning sketch (no "tell me the
    // required length" companion API for now - CLAUDE.md rule 10, see
    // ADR-016).
    unsigned int (*getLineText)(NeoMifesDocument* doc, unsigned line, wchar_t* buffer,
                                unsigned bufferLen);

    // Displays `message` via `sink` (Phase 8e). No-op if `sink` or
    // `message` is NULL. UNLIKE the 4 functions above, this is NEVER
    // permission-gated - always non-NULL regardless of the plugin's
    // declared permissions (see ADR-019): none of the
    // NEOMIFES_PLUGIN_PERMISSION_* categories above semantically cover
    // "display a message" (it reads/writes no document data), and adding
    // a new category for one low-risk display-only function would be
    // speculative (CLAUDE.md rule 3) - revisit once more UI-facing
    // functions exist and a real shared category emerges. Fits the same
    // synchronous, onLoad/onUnload-only threading contract as the other
    // functions here - no new contract needed (contrast registerCommand
    // below, which needed one).
    void (*showToast)(NeoMifesToastSink* sink, const wchar_t* message);

    // Registers a command the user can later run from the command palette
    // (Phase 8f) - call as ctx->coreApi->registerCommand(ctx, id, title,
    // callback). No-op if `ctx`, `ctx->commandRegistry`, `id`, `title`, or
    // `callback` is NULL. `callback` is invoked LATER, not synchronously -
    // see this struct's own "EXCEPTION (Phase 8f)" threading-contract
    // paragraph above for what that does and does not guarantee. Like
    // showToast, deliberately NEVER permission-gated: registering a
    // command carries no data-read/write risk by itself - the actual
    // capability boundary is enforced when `callback` eventually runs and
    // uses ctx->coreApi (already permission-gated per function). UNLIKE
    // showToast(sink, message), takes `ctx` directly rather than a bare
    // sink pointer, because `callback` must be re-invoked later with a
    // full `ctx` (it needs ctx->coreApi to do anything useful), and `ctx`
    // already carries every other capability surface - see ADR-020.
    void (*registerCommand)(NeoMifesPluginContext* ctx, const wchar_t* id, const wchar_t* title,
                            void (*callback)(NeoMifesPluginContext*));
} NeoMifesCoreApi;

// Host-owned, plugin-writable. `userData` is a plain C-ABI idiom, not scope
// creep toward a richer host<->plugin API. The host zero-initializes it
// before calling onLoad and leaves it untouched between onLoad and
// onUnload, so a plugin can round-trip an opaque token through it.
//
// `coreApi`/`document` (Phase 8b), `toastSink` (Phase 8e), `commandRegistry`
// (Phase 8f): non-owning, host-populated, all NULL unless PluginHost::load()
// was called with non-null arguments for them (all default to nullptr - see
// plugin_host.h). Delivered as context fields rather than as extra
// parameters on NeoMifesPluginVTable::onLoad/onUnload - see ADR-016 for
// why (keeps the vtable itself, and every existing plugin's
// onLoad/onUnload signature, unchanged).
typedef struct NeoMifesPluginContext {
    void*                    userData;
    const NeoMifesCoreApi*   coreApi;
    NeoMifesDocument*        document;
    NeoMifesToastSink*       toastSink;
    NeoMifesCommandRegistry* commandRegistry;
} NeoMifesPluginContext;

typedef struct NeoMifesPluginInfo {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* version;
    const wchar_t* author;
    unsigned int   apiVersion;
    // Phase 8d: bitwise OR of NEOMIFES_PLUGIN_PERMISSION_* above,
    // self-declared by the plugin. See those macros' own comment for what
    // this does and does not provide.
    unsigned int   permissions;
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
