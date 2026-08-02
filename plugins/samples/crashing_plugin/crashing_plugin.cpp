// Test fixture (Phase 8a), NOT a real sample. onLoad immediately triggers a
// genuine hardware access-violation fault (write through a null pointer),
// so plugin_load_test.cpp can prove neomifes::plugin::PluginHost's SEH
// trampoline (invokePluginCallbackSafe(), plugin_host.cpp) actually
// isolates a crashing plugin instead of taking the whole test process down
// with it - verified empirically rather than assumed (CLAUDE.md rule 3).
// `p` is `volatile` so the write cannot be proven dead code and optimized
// away.

#include <neomifes/plugin_sdk.h>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id          = L"com.neomifes.samples.crashing_plugin",
    .name        = L"Crashing Plugin",
    .version     = L"0.1.0",
    .author      = L"NeoMIFES",
    .apiVersion  = NEOMIFES_PLUGIN_API_VERSION,
    .permissions = NEOMIFES_PLUGIN_PERMISSION_NONE,
};

void onLoad([[maybe_unused]] NeoMifesPluginContext* ctx) {
    volatile int* p = nullptr;
    *p               = 1;
}

void onUnload([[maybe_unused]] NeoMifesPluginContext* ctx) {}

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
