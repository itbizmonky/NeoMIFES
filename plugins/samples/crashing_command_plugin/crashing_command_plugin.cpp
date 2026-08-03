// Test fixture (Phase 8f), NOT a real sample. Registers a command whose
// callback immediately triggers a genuine hardware access-violation fault
// (write through a null pointer) - unlike crashing_plugin (Phase 8a), the
// fault happens inside a registerCommand callback invoked LATER (outside
// PluginHost::load()'s own call stack, when the test simulates "the user
// ran the command"), so plugin_command_test.cpp can prove
// neomifes::plugin::invokePluginCallbackSafe() still isolates a crash on
// this new invocation path - verified empirically rather than assumed
// (CLAUDE.md rule 3). `p` is `volatile` so the write cannot be proven dead
// code and optimized away.

#include <neomifes/plugin_sdk.h>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id          = L"com.neomifes.samples.crashing_command_plugin",
    .name        = L"Crashing Command Plugin",
    .version     = L"0.1.0",
    .author      = L"NeoMIFES",
    .apiVersion  = NEOMIFES_PLUGIN_API_VERSION,
    .permissions = NEOMIFES_PLUGIN_PERMISSION_NONE,
};

void crashingCommandCallback([[maybe_unused]] NeoMifesPluginContext* ctx) {
    volatile int* p = nullptr;
    *p               = 1;
}

void onLoad(NeoMifesPluginContext* ctx) {
    if (ctx == nullptr || ctx->coreApi == nullptr || ctx->coreApi->registerCommand == nullptr) {
        return;
    }
    ctx->coreApi->registerCommand(ctx, L"sample.crash", L"Sample Crash", &crashingCommandCallback);
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
