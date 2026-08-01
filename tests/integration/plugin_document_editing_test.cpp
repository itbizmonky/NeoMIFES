// Integration test (not a unit test): exercises PluginHost::load() actually
// loading a REAL, separately-built sample plugin DLL (Phase 8b, ADR-016)
// with a real NeoMifesCoreApi bound to a real document::Document, and
// asserts the plugin's onLoad mutated that document through the C-ABI
// boundary. Not a mock - see tests/integration/plugin_load_test.cpp (Phase
// 8a) for the same LoadLibraryW-a-sibling-build pattern this file reuses.
//
// Receives the sample DLL path via argv (same custom-main() pattern
// plugin_load_test.cpp/startup_measure_test.cpp use), since CMake's
// $<TARGET_FILE:...> generator expression is the only way to learn another
// target's build output path.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>

#include "neomifes/app/plugin_core_api_bridge.h"
#include "neomifes/document/document.h"
#include "neomifes/plugin/plugin_host.h"

namespace fs = std::filesystem;

namespace {

std::wstring g_documentEditingPluginPath;

// Same CP_UTF8 MultiByteToWideChar approach plugin_load_test.cpp's
// widenArg() uses.
std::wstring widenArg(const char* narrow) {
    if (narrow == nullptr) {
        return {};
    }
    const int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, narrow, -1, nullptr, 0);
    if (wideLen <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide.data(), wideLen);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();  // MultiByteToWideChar's -1 length includes the terminator
    }
    return wide;
}

TEST(PluginDocumentEditingTest, PluginInsertTextMutatesTheRealDocumentThroughTheDllBoundary) {
    ASSERT_FALSE(g_documentEditingPluginPath.empty())
        << "document_editing_plugin path not provided via argv[1]";

    neomifes::document::Document doc;
    neomifes::plugin::PluginHost host;

    const auto loadResult =
        host.load(fs::path{g_documentEditingPluginPath}, neomifes::app::buildPluginCoreApi(),
                  neomifes::app::toNeoMifesDocument(doc));
    ASSERT_TRUE(loadResult.has_value())
        << "load failed: " << neomifes::plugin::describe(loadResult.error());
    EXPECT_TRUE(host.isLoaded());

    // document_editing_plugin.cpp's onLoad inserts this exact string at (0,0).
    EXPECT_EQ(doc.toU16String(), u"Hello from plugin!\n");

    const auto unloadResult = host.unload();
    EXPECT_TRUE(unloadResult.has_value());
    EXPECT_FALSE(host.isLoaded());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        g_documentEditingPluginPath = widenArg(argv[1]);
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
