// hello_plugin - Phase 8a minimal sample plugin. The smallest possible
// NeoMIFES plugin: export the two required C-ABI entry points, report
// static NeoMifesPluginInfo, touch ctx->userData from onLoad/onUnload only
// to give plugin_load_test.cpp observable proof both callbacks ran (see
// plugin_sdk.h's NeoMifesPluginContext comment - not a real plugin's actual
// job). Depends on NOTHING from this repo except <neomifes/plugin_sdk.h> -
// a real third-party plugin author's source file would look exactly like
// this (their build system, unlike this CMakeLists.txt, would of course be
// their own).

#include <neomifes/plugin_sdk.h>

#include <cstdint>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id         = L"com.neomifes.samples.hello_plugin",
    .name       = L"Hello Plugin",
    .version    = L"0.1.0",
    .author     = L"NeoMIFES",
    .apiVersion = NEOMIFES_PLUGIN_API_VERSION,
};

void onLoad(NeoMifesPluginContext* ctx) {
    if (ctx != nullptr) {
        // Arbitrary nonzero sentinel - the test only checks it round-trips
        // (non-null after onLoad, null again after onUnload).
        ctx->userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1234ABCDU));
    }
}

void onUnload(NeoMifesPluginContext* ctx) {
    if (ctx != nullptr) {
        ctx->userData = nullptr;
    }
}

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
