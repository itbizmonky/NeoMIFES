// Integration test: spawn NeoMIFES.exe with --measure-startup and verify the
// produced JSON. This proves the startup measurement pipeline is wired up
// end-to-end. The absolute 300ms / 20MB targets are tracked as CI metrics,
// not hard assertions (Phase 1 focuses on the ability to measure).

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Passed by ctest as argv[1]; see tests/integration/CMakeLists.txt.
std::wstring g_neomifesExePath;

class StartupMeasureTest : public ::testing::Test {
protected:
    static fs::path outputPath() {
        wchar_t tempDir[MAX_PATH]  = {};
        wchar_t tempFile[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, tempDir);
        ::GetTempFileNameW(tempDir, L"nmfs", 0, tempFile);
        return fs::path{tempFile};
    }

    static DWORD spawnAndWait(const std::wstring& cmdLine, DWORD timeoutMs) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        // CreateProcessW mutates the command line buffer, so use a local writable copy.
        std::wstring buf = cmdLine;
        if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return ERROR_FILE_NOT_FOUND;
        }
        DWORD waitRc = ::WaitForSingleObject(pi.hProcess, timeoutMs);
        DWORD exitRc = 0xFFFFFFFF;
        if (waitRc == WAIT_OBJECT_0) {
            ::GetExitCodeProcess(pi.hProcess, &exitRc);
        } else {
            ::TerminateProcess(pi.hProcess, 1);
        }
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        return exitRc;
    }

    // Extract a decimal (possibly negative) integer field.
    // The hand-rolled JSON writer emits one field per line; a substring scan is
    // sufficient for this PoC.
    static long long readIntField(const std::string& json, std::string_view key) {
        const auto pos = json.find(key);
        if (pos == std::string::npos) {
            return -1;
        }
        const auto colon = json.find(':', pos);
        if (colon == std::string::npos) {
            return -1;
        }
        return std::strtoll(json.c_str() + colon + 1, nullptr, 10);
    }
};

TEST_F(StartupMeasureTest, ProducesValidProfile) {
    ASSERT_FALSE(g_neomifesExePath.empty()) << "exe path not provided via argv[1]";

    const fs::path out    = outputPath();
    const std::wstring cl = L"\"" + g_neomifesExePath + L"\" --measure-startup \""
                          + out.wstring() + L"\"";

    const DWORD exitCode = spawnAndWait(cl, /*timeoutMs=*/10'000);
    ASSERT_EQ(exitCode, 0u) << "NeoMIFES --measure-startup returned " << exitCode;

    std::ifstream in(out);
    ASSERT_TRUE(in.is_open()) << "profile output not created: " << out.string();
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();

    const long long winMain     = readIntField(json, "winMainEnterNs");
    const long long created     = readIntField(json, "windowCreatedNs");
    const long long firstPaint  = readIntField(json, "firstPaintNs");
    const long long ws          = readIntField(json, "workingSetBytesAtFirstPaint");

    EXPECT_EQ(winMain, 0);          // by contract, WinMain enter is the origin
    EXPECT_GT(created, 0);
    EXPECT_GE(firstPaint, created); // paint happens after window creation
    EXPECT_GT(ws, 0);

    // Soft target - the intent is to notice regressions long before we breach it.
    // A CI environment on a busy runner will not hit 0.3s reliably, so we use a
    // relaxed 2s ceiling here; tightening happens in later phases with dedicated
    // benchmark hardware.
    EXPECT_LT(firstPaint, 2'000'000'000LL) << "first paint > 2s (very slow launcher)";

    std::error_code ec;
    fs::remove(out, ec);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        // Widen argv[1] to std::wstring for CreateProcessW.
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        g_neomifesExePath.resize(static_cast<std::size_t>(n));
        ::MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, g_neomifesExePath.data(), n);
        if (!g_neomifesExePath.empty() && g_neomifesExePath.back() == L'\0') {
            g_neomifesExePath.pop_back();
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
