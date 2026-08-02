// Test fixture (Phase 8a), NOT a real sample. Byte-for-byte like
// hello_plugin.cpp except apiVersion is deliberately wrong, so
// plugin_load_test.cpp can assert PluginHost::load() rejects a real,
// separately-built bad-apiVersion DLL without crashing. Lives in its own
// plugins/samples/ subdirectory (rather than a build flag on hello_plugin)
// to keep hello_plugin.cpp a clean, unconditional reference sample.

#include <neomifes/plugin_sdk.h>

namespace {

const NeoMifesPluginInfo kInfo = {
    .id          = L"com.neomifes.samples.hello_plugin_bad_api_version",
    .name        = L"Hello Plugin (Bad API Version)",
    .version     = L"0.1.0",
    .author      = L"NeoMIFES",
    .apiVersion  = NEOMIFES_PLUGIN_API_VERSION + 1000U,  // deliberately wrong
    .permissions = NEOMIFES_PLUGIN_PERMISSION_NONE,
};

void onLoad([[maybe_unused]] NeoMifesPluginContext* ctx) {}
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
