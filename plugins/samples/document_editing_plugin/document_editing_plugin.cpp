// document_editing_plugin - Phase 8b sample plugin. Proves the
// NeoMifesCoreApi round trip end-to-end through the DLL boundary: onLoad
// calls ctx->coreApi->insertText() to insert a known marker string at
// (0,0) of the live document, so a host-side test
// (tests/integration/plugin_document_editing_test.cpp) can assert the
// document's content actually changed. Depends on nothing from this repo
// except <neomifes/plugin_sdk.h>, like hello_plugin.

#include <neomifes/plugin_sdk.h>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id         = L"com.neomifes.samples.document_editing_plugin",
    .name       = L"Document Editing Plugin",
    .version    = L"0.1.0",
    .author     = L"NeoMIFES",
    .apiVersion = NEOMIFES_PLUGIN_API_VERSION,
};

void onLoad(NeoMifesPluginContext* ctx) {
    if (ctx == nullptr || ctx->coreApi == nullptr || ctx->document == nullptr ||
        ctx->coreApi->insertText == nullptr) {
        return;  // host didn't wire coreApi/document - nothing to prove
    }
    ctx->coreApi->insertText(ctx->document, L"Hello from plugin!\n", 0, 0);
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
