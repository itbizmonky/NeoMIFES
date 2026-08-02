// Integration test (not a unit test): exercises PluginHost::load() actually
// loading REAL, separately-built sample plugin DLLs (Phase 8b, ADR-016;
// Phase 8d permission gate, ADR-018) with a real NeoMifesCoreApi factory
// bound to a real document::Document, and asserts the plugins' onLoad
// mutated (or, for permission_denied_plugin, failed to mutate) that
// document through the C-ABI boundary. Not a mock - see
// tests/integration/plugin_load_test.cpp (Phase 8a) for the same
// LoadLibraryW-a-sibling-build pattern this file reuses.
//
// Receives the sample DLL paths via argv (same custom-main() pattern
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
#include "neomifes/plugin/plugin_error.h"
#include "neomifes/plugin/plugin_host.h"

namespace fs = std::filesystem;

namespace {

std::wstring g_documentEditingPluginPath;
std::wstring g_permissionDeniedPluginPath;

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
        host.load(fs::path{g_documentEditingPluginPath}, neomifes::app::buildPluginCoreApi,
                  neomifes::app::toNeoMifesDocument(doc));
    ASSERT_TRUE(loadResult.has_value())
        << "load failed: " << neomifes::plugin::describe(loadResult.error());
    EXPECT_TRUE(host.isLoaded());
    // document_editing_plugin.cpp declares NEOMIFES_PLUGIN_PERMISSION_DOCUMENT
    // (Phase 8d) - confirms the permission round-trips from the DLL's own
    // NeoMifesPluginInfo through PluginHost::load().
    EXPECT_EQ(host.grantedPermissions(), static_cast<unsigned int>(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT));

    // document_editing_plugin.cpp's onLoad inserts this exact string at (0,0).
    EXPECT_EQ(doc.toU16String(), u"Hello from plugin!\n");

    const auto unloadResult = host.unload();
    EXPECT_TRUE(unloadResult.has_value());
    EXPECT_FALSE(host.isLoaded());
}

// Phase 8d, ADR-018 - the inverse of the test above: proves the permission
// gate actually denies CoreApi access, and that the resulting crash (a
// call through a NULL function pointer) is isolated by PluginHost's
// existing SEH trampoline (Phase 8a) exactly like crashing_plugin/
// throwing_plugin (tests/integration/plugin_load_test.cpp) already proved
// for other fault shapes.
TEST(PluginDocumentEditingTest,
     PluginWithoutDocumentPermissionCrashesOnNullInsertTextAndLeavesDocumentUntouched) {
    ASSERT_FALSE(g_permissionDeniedPluginPath.empty())
        << "permission_denied_plugin path not provided via argv[2]";

    neomifes::document::Document doc;
    neomifes::plugin::PluginHost host;

    const auto loadResult = host.load(fs::path{g_permissionDeniedPluginPath},
                                       neomifes::app::buildPluginCoreApi,
                                       neomifes::app::toNeoMifesDocument(doc));
    ASSERT_FALSE(loadResult.has_value());
    EXPECT_EQ(loadResult.error().code, neomifes::plugin::PluginErrorCode::OnLoadCrashed);
    EXPECT_FALSE(host.isLoaded());
    // insertText was NULL - never wrote anything before the crash.
    EXPECT_TRUE(doc.toU16String().empty());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        g_documentEditingPluginPath = widenArg(argv[1]);
    }
    if (argc > 2) {
        g_permissionDeniedPluginPath = widenArg(argv[2]);
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
