#pragma once

// plugin_core_api_bridge - the shim between neomifes::plugin's opaque
// NeoMifesDocument*/NeoMifesCoreApi C-ABI types (plugin_sdk.h) and the real
// document::Document those types stand in for (Phase 8b).
//
// Lives under src/app/ (NOT src/plugin/) because neomifes::plugin::
// PluginHost must not depend on neomifes::document - CLAUDE.md sec.3's
// layered architecture places "Plugin Engine" below "Document Engine" in
// its down-only dependency arrows. This module is the one place allowed to
// depend on both neomifes::plugin_sdk (the struct shapes) and
// neomifes::document (the real mutation methods), the same "*_bridge.h
// thin adapter" role document_open.h/outline_bridge.h already play between
// other pairs of engines that must not depend on each other.
//
// See plugin_sdk.h's NeoMifesCoreApi comment for the full threading
// contract and "not a security boundary" disclaimer, and
// docs/decisions/ADR-016-plugin-core-api-bridge.md for the design
// rationale.

#include <neomifes/plugin_sdk.h>

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::ui {
class ToastState;
class PluginCommandRegistry;
}  // namespace neomifes::ui

namespace neomifes::app {

// Two stateless, process-lifetime NeoMifesCoreApi instances, chosen by
// `grantedPermissions` (Phase 8d): the full 4-function struct if
// NEOMIFES_PLUGIN_PERMISSION_DOCUMENT is set, or one whose 4 function
// pointers are all NULL otherwise (see plugin_sdk.h's NeoMifesCoreApi
// comment for what happens when a plugin calls a NULL one anyway). Safe to
// share across every loaded plugin/document, since every function it
// points to takes the NeoMifesDocument* it operates on as an explicit
// argument rather than capturing shared mutable state.
//
// Signature matches neomifes::plugin::PluginHost::CoreApiFactory exactly
// (unsigned int -> const NeoMifesCoreApi*, noexcept) - pass the function
// itself, not a call result, to PluginHost::load()'s `coreApiFactory`
// parameter (e.g. `host.load(path, neomifes::app::buildPluginCoreApi,
// doc)`), since load() must invoke it AFTER learning the plugin's declared
// permissions.
[[nodiscard]] const NeoMifesCoreApi* buildPluginCoreApi(unsigned int grantedPermissions) noexcept;

// The other half of the opaque-handle idiom this bridge's .cpp uses
// internally (reinterpret_cast, both directions, confined to that one
// file - see plugin_sdk.h's NeoMifesDocument comment). Pass the result to
// PluginHost::load()'s `document` parameter.
[[nodiscard]] NeoMifesDocument* toNeoMifesDocument(document::Document& document) noexcept;

// Same opaque-handle idiom as toNeoMifesDocument() above, for
// NeoMifesToastSink/ui::ToastState (Phase 8e). Pass the result to
// PluginHost::load()'s `toastSink` parameter.
[[nodiscard]] NeoMifesToastSink* toNeoMifesToastSink(ui::ToastState& toastState) noexcept;

// Same opaque-handle idiom as toNeoMifesDocument() above, for
// NeoMifesCommandRegistry/ui::PluginCommandRegistry (Phase 8f). Pass the
// result to PluginHost::load()'s `commandRegistry` parameter.
[[nodiscard]] NeoMifesCommandRegistry* toNeoMifesCommandRegistry(ui::PluginCommandRegistry& registry) noexcept;

}  // namespace neomifes::app
