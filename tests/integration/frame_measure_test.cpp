// Integration test: spawn NeoMIFES.exe with --measure-frame and verify the
// produced JSON. This proves the frame-timing measurement pipeline (Phase
// 3c, ADR-011) is wired up end-to-end. Absolute frame-time targets are
// tracked as CI metrics, not hard assertions here (same rationale as
// startup_measure_test.cpp) - a shared CI runner is not representative of
// end-user frame-time hardware.

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

class FrameMeasureTest : public ::testing::Test {
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

    // Extract a decimal (possibly negative) integer field. The hand-rolled
    // JSON writer emits one field per line; a substring scan is sufficient
    // for this PoC.
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

TEST_F(FrameMeasureTest, ProducesValidProfile) {
    ASSERT_FALSE(g_neomifesExePath.empty()) << "exe path not provided via argv[1]";

    const fs::path out    = outputPath();
    const std::wstring cl = L"\"" + g_neomifesExePath + L"\" --measure-frame \""
                          + out.wstring() + L"\"";

    // 300 vsync-gated frames (~5s at 60fps) plus process startup, plus
    // headroom for a loaded CI runner or WARP software fallback.
    const DWORD exitCode = spawnAndWait(cl, /*timeoutMs=*/30'000);
    ASSERT_EQ(exitCode, 0u) << "NeoMIFES --measure-frame returned " << exitCode;

    std::ifstream in(out);
    ASSERT_TRUE(in.is_open()) << "profile output not created: " << out.string();
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();

    const long long frameCount     = readIntField(json, "frameCount");
    const long long minFrameNs     = readIntField(json, "minFrameNs");
    const long long maxFrameNs     = readIntField(json, "maxFrameNs");
    const long long syntheticLines = readIntField(json, "syntheticLineCount");
    const long long cacheMisses    = readIntField(json, "layoutCacheMisses");

    EXPECT_GT(frameCount, 0);
    EXPECT_GE(maxFrameNs, minFrameNs);
    // No --open was given, so the synthetic-document path (main.cpp's
    // synthesizeMeasurementDocument()) must have been used.
    EXPECT_GT(syntheticLines, 0);
    // The synthetic scroll visits many distinct lines for the first time -
    // proves the TextLayoutCache was actually exercised, not bypassed.
    EXPECT_GT(cacheMisses, 0);

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
