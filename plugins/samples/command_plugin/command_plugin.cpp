// command_plugin - Phase 8f sample plugin. Proves NeoMifesCoreApi::
// registerCommand round-trips through the DLL boundary: registers a
// command during onLoad, and when that command is later run (invoked
// outside onLoad's own call stack - see plugin_sdk.h's "EXCEPTION (Phase
// 8f)" threading-contract paragraph), its callback receives a full
// NeoMifesPluginContext* and can call ctx->coreApi->showToast - proving the
// round trip WITHOUT NEOMIFES_PLUGIN_PERMISSION_DOCUMENT (both
// registerCommand and showToast are never permission-gated).

#include <neomifes/plugin_sdk.h>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id          = L"com.neomifes.samples.command_plugin",
    .name        = L"Command Plugin",
    .version     = L"0.1.0",
    .author      = L"NeoMIFES",
    .apiVersion  = NEOMIFES_PLUGIN_API_VERSION,
    .permissions = NEOMIFES_PLUGIN_PERMISSION_NONE,  // registerCommand doesn't need Document
};

void sampleCommandCallback(NeoMifesPluginContext* ctx) {
    if (ctx == nullptr || ctx->coreApi == nullptr || ctx->toastSink == nullptr ||
        ctx->coreApi->showToast == nullptr) {
        return;
    }
    ctx->coreApi->showToast(ctx->toastSink, L"Command ran!");
}

void onLoad(NeoMifesPluginContext* ctx) {
    if (ctx == nullptr || ctx->coreApi == nullptr || ctx->coreApi->registerCommand == nullptr) {
        return;
    }
    ctx->coreApi->registerCommand(ctx, L"sample.greet", L"Sample Greeting", &sampleCommandCallback);
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
