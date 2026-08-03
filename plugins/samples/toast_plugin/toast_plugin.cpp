// toast_plugin - Phase 8e sample plugin. Proves NeoMifesCoreApi::showToast
// round-trips through the DLL boundary into a real ui::ToastState, and that
// it works WITHOUT NEOMIFES_PLUGIN_PERMISSION_DOCUMENT (showToast is never
// permission-gated - see plugin_sdk.h's showToast comment).

#include <neomifes/plugin_sdk.h>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id          = L"com.neomifes.samples.toast_plugin",
    .name        = L"Toast Plugin",
    .version     = L"0.1.0",
    .author      = L"NeoMIFES",
    .apiVersion  = NEOMIFES_PLUGIN_API_VERSION,
    .permissions = NEOMIFES_PLUGIN_PERMISSION_NONE,  // showToast doesn't need Document
};

void onLoad(NeoMifesPluginContext* ctx) {
    if (ctx == nullptr || ctx->coreApi == nullptr || ctx->toastSink == nullptr ||
        ctx->coreApi->showToast == nullptr) {
        return;
    }
    ctx->coreApi->showToast(ctx->toastSink, L"Hello from plugin!");
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
