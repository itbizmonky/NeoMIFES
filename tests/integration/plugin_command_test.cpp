// Integration test (not a unit test): exercises PluginHost::load() actually
// loading REAL, separately-built sample plugin DLLs (Phase 8f, ADR-020)
// with a real NeoMifesCoreApi factory bound to a real
// ui::PluginCommandRegistry/ui::ToastState, and asserts that
// NeoMifesCoreApi::registerCommand's callback - invoked LATER, outside
// PluginHost::load()'s own call stack, simulating "the user ran the
// command from the palette" - genuinely reaches ctx->coreApi, and that a
// crash inside such a callback is still SEH-isolated (a code path Phase
// 8a's crashing_plugin never exercised, since that crash happened during
// onLoad itself).
//
// Deliberately does NOT test "invoke a registered command after
// PluginHost::unload()" - that is documented undefined behavior (see
// plugin_sdk.h's threading-contract comment and ADR-020's scope-out), and
// an earlier version of this file that exercised it directly demonstrated
// exactly why: under the ubsan preset, AddressSanitizer correctly and
// reliably detects the heap-use-after-free (the freed NeoMifesPluginContext
// the action() closure captured) and reports it - which is ASan doing its
// job, not a bug in registerCommand's design. Writing an automated test
// that expects UB to "not crash" would either be flaky (Debug/Release
// happen not to fault because the freed block isn't reused yet) or would
// require suppressing ASan's own detection of a real use-after-free class,
// which would defeat the purpose of running ASan at all (CLAUDE.md's
// quality gate requires zero ASan crashes) - so this scenario is
// documented, not automated.
//
// Receives the sample DLL paths via argv (same custom-main() pattern
// plugin_document_editing_test.cpp/plugin_toast_test.cpp use), since
// CMake's $<TARGET_FILE:...> generator expression is the only way to learn
// another target's build output path.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>

#include "neomifes/app/plugin_core_api_bridge.h"
#include "neomifes/plugin/plugin_error.h"
#include "neomifes/plugin/plugin_host.h"
#include "neomifes/ui/plugin_command_registry.h"
#include "neomifes/ui/toast_state.h"

namespace fs = std::filesystem;

namespace {

std::wstring g_commandPluginPath;
std::wstring g_crashingCommandPluginPath;

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

TEST(PluginCommandTest, RegisterCommandAddsToTheRegistryAndDeferredInvocationReachesCoreApi) {
    ASSERT_FALSE(g_commandPluginPath.empty()) << "command_plugin path not provided via argv[1]";

    neomifes::ui::ToastState            toast;
    neomifes::ui::PluginCommandRegistry registry;
    neomifes::plugin::PluginHost        host;

    const auto loadResult =
        host.load(fs::path{g_commandPluginPath}, neomifes::app::buildPluginCoreApi,
                  /*document=*/nullptr, neomifes::app::toNeoMifesToastSink(toast),
                  neomifes::app::toNeoMifesCommandRegistry(registry));
    ASSERT_TRUE(loadResult.has_value())
        << "load failed: " << neomifes::plugin::describe(loadResult.error());
    EXPECT_TRUE(host.isLoaded());
    // command_plugin.cpp declares NEOMIFES_PLUGIN_PERMISSION_NONE - confirms
    // registerCommand worked without NEOMIFES_PLUGIN_PERMISSION_DOCUMENT.
    EXPECT_EQ(host.grantedPermissions(), static_cast<unsigned int>(NEOMIFES_PLUGIN_PERMISSION_NONE));

    // command_plugin.cpp's onLoad registers exactly this command.
    ASSERT_EQ(registry.commands().size(), 1U);
    EXPECT_EQ(registry.commands()[0].id, u"sample.greet");
    EXPECT_EQ(registry.commands()[0].title, u"Sample Greeting");
    EXPECT_FALSE(toast.isVisible());  // not run yet - registering alone must not invoke it

    // Simulates "the user selected this command in the palette" - there is
    // no real ui::CommandPalette involved in this sub-phase (see ADR-020).
    registry.commands()[0].action();
    EXPECT_TRUE(toast.isVisible());
    EXPECT_EQ(toast.message(), u"Command ran!");

    const auto unloadResult = host.unload();
    EXPECT_TRUE(unloadResult.has_value());
    EXPECT_FALSE(host.isLoaded());
}

// Phase 8f - proves the SEH trampoline (neomifes::plugin::
// invokePluginCallbackSafe(), reused via registerCommand's action()
// closure) isolates a crash that happens OUTSIDE PluginHost::load()'s own
// call stack, unlike Phase 8a's crashing_plugin (which only exercised a
// crash during onLoad itself).
TEST(PluginCommandTest, InvokingACrashingRegisteredCommandDoesNotCrashTheHostProcess) {
    ASSERT_FALSE(g_crashingCommandPluginPath.empty())
        << "crashing_command_plugin path not provided via argv[2]";

    neomifes::ui::PluginCommandRegistry registry;
    neomifes::plugin::PluginHost        host;

    const auto loadResult =
        host.load(fs::path{g_crashingCommandPluginPath}, neomifes::app::buildPluginCoreApi,
                  /*document=*/nullptr, /*toastSink=*/nullptr,
                  neomifes::app::toNeoMifesCommandRegistry(registry));
    ASSERT_TRUE(loadResult.has_value())
        << "load failed: " << neomifes::plugin::describe(loadResult.error());
    ASSERT_EQ(registry.commands().size(), 1U);

    // The callback writes through a null pointer. If the SEH trampoline
    // didn't catch it, this test process would terminate here instead of
    // reaching the assertion below.
    registry.commands()[0].action();
    SUCCEED() << "process survived invoking a crashing registered command";

    const auto unloadResult = host.unload();
    EXPECT_TRUE(unloadResult.has_value());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        g_commandPluginPath = widenArg(argv[1]);
    }
    if (argc > 2) {
        g_crashingCommandPluginPath = widenArg(argv[2]);
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
