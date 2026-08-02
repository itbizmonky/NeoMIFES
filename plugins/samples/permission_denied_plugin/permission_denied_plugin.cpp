// Test fixture (Phase 8d), NOT a real sample. Declares
// NEOMIFES_PLUGIN_PERMISSION_NONE but calls ctx->coreApi->insertText()
// anyway, without the null-check hello_plugin/document_editing_plugin
// perform - so tests/integration/plugin_document_editing_test.cpp can prove
// the permission gate actually works: neomifes::app::buildPluginCoreApi()
// hands an undeclared-permission plugin a NeoMifesCoreApi whose insertText
// is NULL, and calling through it anyway crashes on a null-pointer call,
// caught by PluginHost's existing unconditional SEH trampoline (Phase 8a)
// and reported as OnLoadCrashed - the same empirical-verification
// philosophy as Phase 8a's crashing_plugin/throwing_plugin (CLAUDE.md
// rule 3: don't assume, verify).

#include <neomifes/plugin_sdk.h>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id          = L"com.neomifes.samples.permission_denied_plugin",
    .name        = L"Permission Denied Plugin",
    .version     = L"0.1.0",
    .author      = L"NeoMIFES",
    .apiVersion  = NEOMIFES_PLUGIN_API_VERSION,
    .permissions = NEOMIFES_PLUGIN_PERMISSION_NONE,  // deliberately does NOT request Document
};

void onLoad(NeoMifesPluginContext* ctx) {
    if (ctx == nullptr || ctx->coreApi == nullptr || ctx->document == nullptr) {
        return;
    }
    // Deliberately skips the ctx->coreApi->insertText == nullptr check that
    // document_editing_plugin performs - this plugin declared no Document
    // permission, so insertText is NULL here. Calling it anyway is exactly
    // the scenario this sample exists to prove.
    ctx->coreApi->insertText(ctx->document, L"should never appear", 0, 0);
}

void onUnload(NeoMifesPluginContext* /*ctx*/) {}

const NeoMifesPluginVTable kVTable = {
    .onLoad   = &onLoad,
    .onUnload = &onUnload,
};

}  // namespace

extern "C" __declspec(dllexport) const NeoMifesPluginInfo* neomifes_plugin_info() {
    return &kInfo;
}

extern "C" __declspec(dllexport) const NeoMifesPluginVTable* neomifes_plugin_vtable() {
    return &kVTable;
}
