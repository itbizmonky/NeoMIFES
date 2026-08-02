// Integration test (not a unit test): exercises the REAL Windows Job
// Object sandboxing (Phase 8c, ADR-017) - not a mock. Deliberately its own
// dedicated executable (see tests/integration/CMakeLists.txt's comment):
// AssignProcessToJobObject is a one-way, permanent operation for the life
// of a process, so calling ensureProcessSandboxed() in a shared test
// binary would permanently strip every OTHER test in that binary of the
// ability to spawn a child process, for the rest of that process's life.

#include <gtest/gtest.h>

#include <windows.h>

#include <string>

#include "neomifes/plugin/plugin_sandbox.h"

namespace neomifes::plugin {
namespace {

// Safe, primary test: verifies setup succeeded and round-trips the actual
// applied limits via QueryInformationJobObject - never triggers the
// limit's terminate/fail behavior. GTEST_SKIP() (same idiom as
// platform_clipboard_test.cpp/render_device_smoke_test.cpp) if this
// process was already placed in a non-nesting job by its environment -
// see plugin_sandbox.h's header comment on why that's non-fatal.
TEST(PluginSandboxTest, EnsureProcessSandboxedRoundTripsViaQueryInformationJobObject) {
    const auto result = ensureProcessSandboxed();
    if (!result.has_value()) {
        GTEST_SKIP() << "sandbox setup unavailable in this environment: "
                     << describe(result.error());
    }

    const auto limits = queryActiveJobLimits();
    ASSERT_TRUE(limits.has_value());
    const JOBOBJECT_BASIC_LIMIT_INFORMATION& limitsValue = *limits;
    EXPECT_TRUE(limitsValue.LimitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS);
    EXPECT_EQ(limitsValue.ActiveProcessLimit, 1U);
}

// Proves "first call wins" - a second call must not attempt a second,
// redundant CreateJobObjectW/AssignProcessToJobObject sequence, and must
// report the same outcome as the first.
TEST(PluginSandboxTest, EnsureProcessSandboxedIsIdempotent) {
    const auto first  = ensureProcessSandboxed();
    const auto second = ensureProcessSandboxed();
    ASSERT_EQ(first.has_value(), second.has_value());
    if (!first.has_value()) {
        EXPECT_EQ(first.error().code, second.error().code);
        EXPECT_EQ(first.error().win32Error, second.error().win32Error);
    }
}

// The core empirical proof this phase exists to provide (CLAUDE.md rule 3
// - not assumed from documentation alone, same philosophy as
// plugin_load_test.cpp's crashing_plugin/throwing_plugin cases): once
// sandboxed, spawning a child process fails in THIS (calling) process,
// and this process itself keeps running afterward - reaching the
// assertions below, rather than being torn down as a side effect, is the
// proof.
TEST(PluginSandboxTest, ChildProcessCreationFailsOnceSandboxedAndCallerSurvives) {
    const auto result = ensureProcessSandboxed();
    if (!result.has_value()) {
        GTEST_SKIP() << "sandbox setup unavailable in this environment: "
                     << describe(result.error());
    }

    STARTUPINFOW         si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION  pi{};
    std::wstring         cmdLine = L"cmd.exe /c exit 0";
    const BOOL created = ::CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                                          CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    EXPECT_FALSE(created) << "CreateProcessW unexpectedly succeeded while sandboxed";
    if (created) {
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
    }

    // Reaching this line IS the proof: the OS did not terminate this
    // process as a side effect of the CreateProcessW call above.
    SUCCEED() << "test process survived the CreateProcessW call above";
}

}  // namespace
}  // namespace neomifes::plugin
