// Integration test (not a unit test): exercises neomifes::plugin::PluginHost
// actually loading REAL, separately-built sample plugin DLLs via
// LoadLibraryW (Phase 8a, ADR-015) - not a mock. First test in this
// codebase to LoadLibraryW a sibling-built CMake artifact rather than
// spawning it as a subprocess (contrast startup_measure_test.cpp/
// frame_measure_test.cpp).
//
// Receives the four sample DLL paths via argv (same custom-main() pattern
// startup_measure_test.cpp uses to receive NeoMIFES.exe's path), since
// CMake's $<TARGET_FILE:...> generator expression is the only way to learn
// another target's build output path.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>

#include "neomifes/plugin/plugin_host.h"

namespace fs = std::filesystem;

namespace {

std::wstring g_helloPluginPath;
std::wstring g_helloPluginBadApiVersionPath;
std::wstring g_crashingPluginPath;
std::wstring g_throwingPluginPath;

// Widens one argv entry to std::wstring for std::filesystem::path/LoadLibraryW
// - same CP_UTF8 MultiByteToWideChar approach startup_measure_test.cpp's
// main() uses for its own argv[1] (NeoMIFES.exe's path), extracted into a
// helper since this file has four paths to convert instead of one.
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

TEST(PluginLoadTest, LoadsRunsOnLoadAndOnUnloadOnTheRealSampleDll) {
    ASSERT_FALSE(g_helloPluginPath.empty()) << "hello_plugin path not provided via argv[1]";
    neomifes::plugin::PluginHost host;

    const auto loadResult = host.load(fs::path{g_helloPluginPath});
    ASSERT_TRUE(loadResult.has_value())
        << "load failed: " << neomifes::plugin::describe(loadResult.error());
    EXPECT_TRUE(host.isLoaded());
    EXPECT_NE(host.contextUserData(), nullptr);  // onLoad ran
    // Phase 8d: hello_plugin.cpp declares NEOMIFES_PLUGIN_PERMISSION_NONE -
    // confirms the (unused-by-this-plugin) permission read-through works.
    EXPECT_EQ(host.grantedPermissions(), static_cast<unsigned int>(NEOMIFES_PLUGIN_PERMISSION_NONE));

    const auto secondLoad = host.load(fs::path{g_helloPluginPath});
    ASSERT_FALSE(secondLoad.has_value());
    EXPECT_EQ(secondLoad.error().code, neomifes::plugin::PluginErrorCode::AlreadyLoaded);

    const auto unloadResult = host.unload();
    EXPECT_TRUE(unloadResult.has_value());
    EXPECT_FALSE(host.isLoaded());
    EXPECT_EQ(host.contextUserData(), nullptr);  // onUnload ran

    const auto secondUnload = host.unload();
    ASSERT_FALSE(secondUnload.has_value());
    EXPECT_EQ(secondUnload.error().code, neomifes::plugin::PluginErrorCode::NotLoaded);
}

TEST(PluginLoadTest, RejectsApiVersionMismatchWithoutCrashing) {
    ASSERT_FALSE(g_helloPluginBadApiVersionPath.empty())
        << "hello_plugin_bad_api_version path not provided via argv[2]";
    neomifes::plugin::PluginHost host;

    const auto loadResult = host.load(fs::path{g_helloPluginBadApiVersionPath});
    ASSERT_FALSE(loadResult.has_value());
    EXPECT_EQ(loadResult.error().code, neomifes::plugin::PluginErrorCode::ApiVersionMismatch);
    EXPECT_EQ(loadResult.error().reportedApiVersion, NEOMIFES_PLUGIN_API_VERSION + 1000U);
    EXPECT_FALSE(host.isLoaded());
}

// Empirically verifies (not assumes, per CLAUDE.md rule 3) that
// invokePluginCallbackSafe()'s SEH trampoline isolates a plugin that faults
// on real hardware (a null-pointer write, EXCEPTION_ACCESS_VIOLATION) -
// this test process itself must survive to run the assertions below AND
// the tests that follow it in the same binary.
TEST(PluginLoadTest, IsolatesAHardwareFaultInOnLoadWithoutCrashingTheHost) {
    ASSERT_FALSE(g_crashingPluginPath.empty()) << "crashing_plugin path not provided via argv[3]";
    neomifes::plugin::PluginHost host;

    const auto loadResult = host.load(fs::path{g_crashingPluginPath});
    ASSERT_FALSE(loadResult.has_value());
    EXPECT_EQ(loadResult.error().code, neomifes::plugin::PluginErrorCode::OnLoadCrashed);
    EXPECT_FALSE(host.isLoaded());
}

// Empirically verifies that the same SEH trampoline also isolates a plugin
// whose onLoad throws a C++ exception (rather than faulting on real
// hardware) - the host is built with /EHsc, which assumes extern
// "C"-linkage functions the compiler can see by name don't throw across
// that boundary, but invokePluginCallbackSafe() calls through an indirect
// function pointer, which the compiler cannot make that assumption about.
TEST(PluginLoadTest, IsolatesAThrownExceptionInOnLoadWithoutCrashingTheHost) {
    ASSERT_FALSE(g_throwingPluginPath.empty()) << "throwing_plugin path not provided via argv[4]";
    neomifes::plugin::PluginHost host;

    const auto loadResult = host.load(fs::path{g_throwingPluginPath});
    ASSERT_FALSE(loadResult.has_value());
    EXPECT_EQ(loadResult.error().code, neomifes::plugin::PluginErrorCode::OnLoadCrashed);
    EXPECT_FALSE(host.isLoaded());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        g_helloPluginPath = widenArg(argv[1]);
    }
    if (argc > 2) {
        g_helloPluginBadApiVersionPath = widenArg(argv[2]);
    }
    if (argc > 3) {
        g_crashingPluginPath = widenArg(argv[3]);
    }
    if (argc > 4) {
        g_throwingPluginPath = widenArg(argv[4]);
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
