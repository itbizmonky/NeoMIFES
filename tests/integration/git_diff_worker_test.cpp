// Integration test (not a unit test): exercises GitDiffWorker's real
// background std::thread + PostMessageW-based completion handoff (WI-17b) -
// modeled directly on csvmode_csv_model_worker_test.cpp, this codebase's
// existing template for testing this exact kind of worker. The Git fixture-
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

#include "neomifes/document/document.h"
#include "neomifes/git/git_diff_worker.h"
#include "neomifes/git/git_init.h"
#include "neomifes/git/git_repository.h"

namespace fs = std::filesystem;

namespace {

using neomifes::document::Document;
using neomifes::git::GitDiffWorker;
using neomifes::git::kMsgGitDiffReady;
using neomifes::git::LineDiffKind;
using neomifes::git::LineDiffRegion;

fs::path uniqueTempDir() {
    fs::path dir = fs::temp_directory_path() / (std::string("nmfs_gitworker_") + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// Same fixture-construction shape as git_repository_test.cpp's own
// makeRepoWithCommit() - `dir` only ever holds ASCII temp-path segments in
// this test file, so plain .string() is safe here.
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

// Same hidden-window pattern as csvmode_csv_model_worker_test.cpp - a plain
// message-capable window is all GitDiffWorker's PostMessageW target needs.
class HiddenWindow {
public:
    HiddenWindow() {
        m_hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100, nullptr, nullptr, nullptr, nullptr);
    }
    ~HiddenWindow() {
        if (m_hwnd != nullptr) {
            ::DestroyWindow(m_hwnd);
        }
    }
    HiddenWindow(const HiddenWindow&)            = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND get() const noexcept { return m_hwnd; }

private:
    HWND m_hwnd = nullptr;
};

struct GitDiffResult {
    const void*                                  sessionToken;
    std::unique_ptr<std::optional<std::vector<LineDiffRegion>>> regions;
};

// Pumps this thread's message queue until `expectedCount` kMsgGitDiffReady
// messages have been observed or `timeoutMs` elapses. Collects EVERY result
// (not just the latest) - the whole point of GitDiffWorker's FIFO queue is
// that no request is ever silently dropped.
std::vector<GitDiffResult> pumpForGitDiffResults(std::size_t expectedCount, std::uint32_t timeoutMs) {
    std::vector<GitDiffResult> results;
    const ULONGLONG              deadline = ::GetTickCount64() + timeoutMs;
    while (::GetTickCount64() < deadline && results.size() < expectedCount) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            if (msg.message == kMsgGitDiffReady) {
                results.push_back(GitDiffResult{
                    .sessionToken = reinterpret_cast<const void*>(msg.wParam),
                    .regions      = std::unique_ptr<std::optional<std::vector<LineDiffRegion>>>(
                        reinterpret_cast<std::optional<std::vector<LineDiffRegion>>*>(msg.lParam)),
                });
            } else {
                ::DispatchMessageW(&msg);
            }
        }
        ::Sleep(5);
    }
    return results;
}

class GitDiffWorkerTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(neomifes::git::initializeLibgit2()); }
    void TearDown() override { neomifes::git::shutdownLibgit2(); }
};

TEST_F(GitDiffWorkerTest, RequestDiffDeliversAddedRegionViaWindowMessage) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");

    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const Document doc = makeDoc(u"line1\nline2\nline3\n");
    const int       token = 42;
    GitDiffWorker   worker(window.get());
    worker.requestDiff(doc.snapshot(), dir / "a.txt", &token);

    auto results = pumpForGitDiffResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgGitDiffReady never arrived within the timeout";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].regions, nullptr);
    ASSERT_TRUE(results[0].regions->has_value());
    ASSERT_EQ((*results[0].regions)->size(), 1U);
    EXPECT_EQ((**results[0].regions)[0].kind, LineDiffKind::Added);

    fs::remove_all(dir);
}

// The core departure from CsvModelWorker (see git_diff_worker.h's own header
// comment): a file outside any Git repository is an everyday, path-dependent
// outcome, not a caller-configuration mistake - it must still deliver a
// message (carrying std::nullopt), never be silently dropped, so the
// requesting EditorSession's "diff in flight" flag can be cleared.
TEST_F(GitDiffWorkerTest, RequestDiffOutsideAnyRepositoryStillDeliversNulloptMessage) {
    const fs::path dir = uniqueTempDir();  // no git_repository_init() call - not a repo

    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const Document doc = makeDoc(u"anything\n");
    const int       token = 7;
    GitDiffWorker   worker(window.get());
    worker.requestDiff(doc.snapshot(), dir / "never-a-repo.txt", &token);

    auto results = pumpForGitDiffResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "a non-repository request must still post kMsgGitDiffReady";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].regions, nullptr);
    EXPECT_FALSE(results[0].regions->has_value());

    fs::remove_all(dir);
}

// Same "FIFO, not latest-only" guarantee every other worker in this codebase
// provides - two distinct session tokens requested back-to-back (no pump
// between them), both results must arrive.
TEST_F(GitDiffWorkerTest, MultipleSessionsAreAllProcessedNotJustTheLatest) {
    const fs::path dirA = uniqueTempDir();
    makeRepoWithCommit(dirA, "a.txt", "x\n");
    const fs::path dirB = uniqueTempDir();
    makeRepoWithCommit(dirB, "b.txt", "y\n");

    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const Document docA = makeDoc(u"x\nnew\n");
    const Document docB = makeDoc(u"y\nnew\n");
    const int       tokenA = 1;
    const int       tokenB = 2;
    GitDiffWorker   worker(window.get());
    worker.requestDiff(docA.snapshot(), dirA / "a.txt", &tokenA);
    worker.requestDiff(docB.snapshot(), dirB / "b.txt", &tokenB);

    const auto results = pumpForGitDiffResults(2, 5000);
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

TEST_F(GitDiffWorkerTest, WorkerDestructorJoinsCleanlyWithPendingRequests) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "x\n1\n");

    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const Document doc = makeDoc(u"x\n1\n2\n");
    const int       token = 99;
    {
        GitDiffWorker worker(window.get());
        worker.requestDiff(doc.snapshot(), dir / "a.txt", &token);
        // Deliberately no pump here - the destructor below must join the
        // worker thread cleanly regardless of whether it already started
        // (or even finished) processing the pending request.
    }
    // Drain and discard whatever the worker managed to post before
    // shutdown - reaching this line without hanging/crashing is the test.
    (void)pumpForGitDiffResults(1, 200);
    SUCCEED();

    fs::remove_all(dir);
}

}  // namespace
