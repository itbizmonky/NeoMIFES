// Integration test (not a unit test): exercises GitStatusWorker's real
// background std::thread + PostMessageW-based completion handoff (WI-17e) -
// modeled directly on git_diff_worker_test.cpp, this codebase's existing
// template for testing this exact kind of worker. The Git fixture-
// construction helpers below duplicate tests/unit/git_repository_test.cpp's
// own (GitRepository itself has no write/commit API - out of scope - so
// every consumer of a real test repo builds one directly against libgit2).

#include <gtest/gtest.h>

#include <windows.h>

#include <git2.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/git/git_init.h"
#include "neomifes/git/git_repository.h"
#include "neomifes/git/git_status_worker.h"

namespace fs = std::filesystem;

namespace {

using neomifes::git::GitFileStatus;
using neomifes::git::GitStatusEntry;
using neomifes::git::GitStatusWorker;
using neomifes::git::kMsgGitStatusReady;

fs::path uniqueTempDir() {
    fs::path dir = fs::temp_directory_path() / (std::string("nmfs_gitstatusworker_") + std::to_string(std::rand()));
    // See git_repository_test.cpp's own uniqueTempDir() comment: unseeded
    // std::rand() hands out the same sequence of names every process run,
    // so a previous failed run's un-cleaned-up leftover can collide with a
    // later run's same-numbered call unless cleared here first.
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// Same fixture-construction shape as git_repository_test.cpp's own
// makeRepoWithCommit(), including the git_index_write() call
// (git_index_write_tree() alone never persists to the on-disk .git/index,
// which statusList() - unlike diffAgainstHead() - actually reads).
void makeRepoWithCommit(const fs::path& dir, std::string_view filename, std::string_view content) {
    const std::string dirUtf8 = dir.string();
    git_repository*    repo    = nullptr;
    ASSERT_EQ(::git_repository_init(&repo, dirUtf8.c_str(), 0), 0);

    writeFile(dir / filename, content);

    git_index* index = nullptr;
    ASSERT_EQ(::git_repository_index(&index, repo), 0);
    ASSERT_EQ(::git_index_add_bypath(index, std::string(filename).c_str()), 0);
    git_oid treeOid;
    ASSERT_EQ(::git_index_write_tree(&treeOid, index), 0);
    ASSERT_EQ(::git_index_write(index), 0);
    ::git_index_free(index);

    git_tree* tree = nullptr;
    ASSERT_EQ(::git_tree_lookup(&tree, repo, &treeOid), 0);

    git_signature* sig = nullptr;
    ASSERT_EQ(::git_signature_now(&sig, "Test", "test@example.com"), 0);

    git_oid commitOid;
    ASSERT_EQ(::git_commit_create(&commitOid, repo, "HEAD", sig, sig, nullptr, "Initial commit", tree, 0, nullptr), 0);

    ::git_signature_free(sig);
    ::git_tree_free(tree);
    ::git_repository_free(repo);
}

// Same hidden-window pattern as git_diff_worker_test.cpp - a plain
// message-capable window is all GitStatusWorker's PostMessageW target needs.
class HiddenWindow {
public:
    HiddenWindow()
        : m_hwnd(::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100, nullptr, nullptr, nullptr,
                                   nullptr)) {}
    ~HiddenWindow() {
        if (m_hwnd != nullptr) {
            ::DestroyWindow(m_hwnd);
        }
    }
    HiddenWindow(const HiddenWindow&)            = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
    HiddenWindow(HiddenWindow&&)                 = delete;
    HiddenWindow& operator=(HiddenWindow&&)      = delete;

    [[nodiscard]] HWND get() const noexcept { return m_hwnd; }

private:
    HWND m_hwnd = nullptr;
};

struct GitStatusResult {
    const void*                                       sessionToken;
    std::unique_ptr<std::optional<std::vector<GitStatusEntry>>> entries;
};

// Pumps this thread's message queue until `expectedCount` kMsgGitStatusReady
// messages have been observed or `timeoutMs` elapses. Collects EVERY result
// (not just the latest) - the whole point of GitStatusWorker's FIFO queue is
// that no request is ever silently dropped.
std::vector<GitStatusResult> pumpForGitStatusResults(std::size_t expectedCount, std::uint32_t timeoutMs) {
    std::vector<GitStatusResult> results;
    const ULONGLONG                deadline = ::GetTickCount64() + timeoutMs;
    while (::GetTickCount64() < deadline && results.size() < expectedCount) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            if (msg.message == kMsgGitStatusReady) {
                results.push_back(GitStatusResult{
                    .sessionToken = reinterpret_cast<const void*>(msg.wParam),
                    .entries      = std::unique_ptr<std::optional<std::vector<GitStatusEntry>>>(
                        reinterpret_cast<std::optional<std::vector<GitStatusEntry>>*>(msg.lParam)),
                });
            } else {
                ::DispatchMessageW(&msg);
            }
        }
        ::Sleep(5);
    }
    return results;
}

class GitStatusWorkerTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(neomifes::git::initializeLibgit2()); }
    void TearDown() override { neomifes::git::shutdownLibgit2(); }
};

TEST_F(GitStatusWorkerTest, RequestStatusDeliversChangedFileViaWindowMessage) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\n");
    writeFile(dir / "a.txt", "line1\nmodified\n");

    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const int        token = 42;
    GitStatusWorker worker(window.get());
    worker.requestStatus(dir / "a.txt", &token);

    auto results = pumpForGitStatusResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgGitStatusReady never arrived within the timeout";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].entries, nullptr);
    ASSERT_TRUE(results[0].entries->has_value());
    const auto& entries = **results[0].entries;
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].status, GitFileStatus::Modified);

    fs::remove_all(dir);
}

// Same "never silently drop" contract as GitDiffWorker: a path outside any
// Git repository is an everyday, path-dependent outcome, not a caller
// mistake - it must still deliver a message (carrying std::nullopt).
TEST_F(GitStatusWorkerTest, RequestStatusOutsideAnyRepositoryStillDeliversNulloptMessage) {
    const fs::path dir = uniqueTempDir();  // no git_repository_init() call - not a repo

    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const int        token = 7;
    GitStatusWorker worker(window.get());
    worker.requestStatus(dir / "never-a-repo.txt", &token);

    auto results = pumpForGitStatusResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "a non-repository request must still post kMsgGitStatusReady";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].entries, nullptr);
    EXPECT_FALSE(results[0].entries->has_value());

    fs::remove_all(dir);
}

// Same "FIFO, not latest-only" guarantee every other worker in this codebase
// provides - two distinct session tokens requested back-to-back (no pump
// between them), both results must arrive.
TEST_F(GitStatusWorkerTest, MultipleSessionsAreAllProcessedNotJustTheLatest) {
    const fs::path dirA = uniqueTempDir();
    makeRepoWithCommit(dirA, "a.txt", "x\n");
    writeFile(dirA / "a.txt", "x\nchanged\n");
    const fs::path dirB = uniqueTempDir();
    makeRepoWithCommit(dirB, "b.txt", "y\n");
    writeFile(dirB / "b.txt", "y\nchanged\n");

    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const int        tokenA = 1;
    const int        tokenB = 2;
    GitStatusWorker worker(window.get());
    worker.requestStatus(dirA / "a.txt", &tokenA);
    worker.requestStatus(dirB / "b.txt", &tokenB);

    const auto results = pumpForGitStatusResults(2, 5000);
    ASSERT_EQ(results.size(), 2U) << "both requests must be delivered, not just the latest";

    bool sawTokenA = false;
    bool sawTokenB = false;
    for (const auto& result : results) {
        if (result.sessionToken == &tokenA) {
            sawTokenA = true;
        } else if (result.sessionToken == &tokenB) {
            sawTokenB = true;
        }
    }
    EXPECT_TRUE(sawTokenA);
    EXPECT_TRUE(sawTokenB);

    fs::remove_all(dirA);
    fs::remove_all(dirB);
}

TEST_F(GitStatusWorkerTest, WorkerDestructorJoinsCleanlyWithPendingRequests) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "x\n1\n");
    writeFile(dir / "a.txt", "x\n1\n2\n");

    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const int token = 99;
    {
        GitStatusWorker worker(window.get());
        worker.requestStatus(dir / "a.txt", &token);
        // Deliberately no pump here - the destructor below must join the
        // worker thread cleanly regardless of whether it already started
        // (or even finished) processing the pending request.
    }
    // Drain and discard whatever the worker managed to post before
    // shutdown - reaching this line without hanging/crashing is the test.
    (void)pumpForGitStatusResults(1, 200);
    SUCCEED();

    fs::remove_all(dir);
}

}  // namespace
