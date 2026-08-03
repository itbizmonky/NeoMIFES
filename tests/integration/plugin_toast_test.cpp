// Integration test: loads the REAL built toast_plugin sample DLL (Phase 8e,
// ADR-019) via PluginHost::load(), passing a real ui::ToastState bound
// through toNeoMifesToastSink(), and asserts the plugin's onLoad actually
// set the toast's message through the C-ABI boundary - proof (not
// assumption, CLAUDE.md rule 3) that showToast round-trips end-to-end, and
// that it works without NEOMIFES_PLUGIN_PERMISSION_DOCUMENT.
//
// Receives the sample DLL path via argv (same custom-main() pattern
// plugin_load_test.cpp/plugin_document_editing_test.cpp use), since
// CMake's $<TARGET_FILE:...> generator expression is the only way to learn
// another target's build output path.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>

#include "neomifes/app/plugin_core_api_bridge.h"
#include "neomifes/plugin/plugin_host.h"
#include "neomifes/ui/toast_state.h"

namespace fs = std::filesystem;

namespace {

std::wstring g_toastPluginPath;

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

TEST(PluginToastTest, PluginShowToastSetsTheRealToastStateThroughTheDllBoundary) {
    ASSERT_FALSE(g_toastPluginPath.empty()) << "toast_plugin path not provided via argv[1]";

    neomifes::ui::ToastState toast;
    neomifes::plugin::PluginHost host;

    const auto loadResult = host.load(fs::path{g_toastPluginPath}, neomifes::app::buildPluginCoreApi,
                                       /*document=*/nullptr, neomifes::app::toNeoMifesToastSink(toast));
    ASSERT_TRUE(loadResult.has_value())
        << "load failed: " << neomifes::plugin::describe(loadResult.error());
    EXPECT_TRUE(host.isLoaded());
    // toast_plugin.cpp declares NEOMIFES_PLUGIN_PERMISSION_NONE - confirms
    // showToast worked without NEOMIFES_PLUGIN_PERMISSION_DOCUMENT.
    EXPECT_EQ(host.grantedPermissions(), static_cast<unsigned int>(NEOMIFES_PLUGIN_PERMISSION_NONE));

    // toast_plugin.cpp's onLoad calls showToast with this exact string.
    EXPECT_TRUE(toast.isVisible());
    EXPECT_EQ(toast.message(), u"Hello from plugin!");

    const auto unloadResult = host.unload();
    EXPECT_TRUE(unloadResult.has_value());
    EXPECT_FALSE(host.isLoaded());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        g_toastPluginPath = widenArg(argv[1]);
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
