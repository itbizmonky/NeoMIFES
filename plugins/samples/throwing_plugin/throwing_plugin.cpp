// Test fixture (Phase 8a), NOT a real sample. onLoad throws a C++
// exception, so plugin_load_test.cpp can prove neomifes::plugin::PluginHost's
// SEH trampoline also isolates a plugin that throws (not just one that
// faults on real hardware) - the host is built with /EHsc, which assumes
// extern "C"-linkage functions don't throw across that boundary, but this
// call happens through an indirect function pointer (plugin_host.cpp's
// invokePluginCallbackSafe()), which the compiler cannot statically prove
// won't throw - verified empirically rather than assumed (CLAUDE.md rule 3).

#include <neomifes/plugin_sdk.h>

#include <stdexcept>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id         = L"com.neomifes.samples.throwing_plugin",
    .name       = L"Throwing Plugin",
    .version    = L"0.1.0",
    .author     = L"NeoMIFES",
    .apiVersion = NEOMIFES_PLUGIN_API_VERSION,
};

void onLoad([[maybe_unused]] NeoMifesPluginContext* ctx) {
    throw std::runtime_error("boom");
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
