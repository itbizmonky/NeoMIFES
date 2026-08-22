// git_repository_test - headless tests for GitRepository (WI-17a) against a
// real, self-constructed temp-directory Git repository (no external git.exe
// or checked-in fixture repo - see git_repository.h's own build_plan.md
// entry for why: libgit2 can build a minimal repo+commit itself, matching
// this codebase's "own the whole test fixture, no external process" style
// app_autosave_test.cpp already established for filesystem-backed tests).

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <git2.h>

#include "neomifes/document/document.h"
#include "neomifes/git/git_init.h"
#include "neomifes/git/git_repository.h"

namespace fs = std::filesystem;

namespace {

using neomifes::document::Document;
using neomifes::git::GitRepository;
using neomifes::git::LineDiffKind;

fs::path uniqueTempDir() {
    fs::path dir = fs::temp_directory_path() / (std::string("nmfs_git_") + std::to_string(std::rand()));
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

// Creates a fresh repository at `dir` (already fs::create_directories()'d by
// uniqueTempDir()) with one file (`filename`, containing `content`)
// committed to HEAD. Uses libgit2 directly (not GitRepository, which has no
// write/commit API of its own - out of this WI's scope) - `dir` only ever
// holds ASCII temp-path segments in this test file, so plain .string() is
// safe here even though production code paths must go through
// GitRepository's own internal UTF-16->UTF-8 conversion for non-ASCII paths.
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

class GitRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(neomifes::git::initializeLibgit2()); }
    void TearDown() override { neomifes::git::shutdownLibgit2(); }
};

TEST_F(GitRepositoryTest, DiscoverFindsRepositoryFromNestedSubdirectory) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    const fs::path subdir = dir / "sub" / "dir";
    fs::create_directories(subdir);

    const auto repo = GitRepository::discover(subdir);
    EXPECT_TRUE(repo.has_value());

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, DiscoverReturnsNulloptOutsideAnyRepository) {
    const fs::path dir = uniqueTempDir();  // no git_repository_init() call - not a repo

    const auto repo = GitRepository::discover(dir);
    EXPECT_FALSE(repo.has_value());

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, DiffAgainstHeadReturnsNulloptForUntrackedFile) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc    = makeDoc(u"anything\n");
    const auto      result = repo->diffAgainstHead(dir / "never-committed.txt", doc);
    EXPECT_FALSE(result.has_value());

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, DiffAgainstHeadDetectsAddedRegion) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc    = makeDoc(u"line1\nline2\nline3\n");
    const auto      result = repo->diffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].kind, LineDiffKind::Added);

    fs::remove_all(dir);
}

// WI-17b: diffAgainstHead()'s primary entry point is now the BufferSnapshot
// overload (the Document overload above merely delegates via *doc.snapshot()) -
// this test calls it directly, the shape git::GitDiffWorker's background
// thread actually uses (it never touches a live Document).
TEST_F(GitRepositoryTest, DiffAgainstHeadBufferSnapshotOverloadDetectsAddedRegion) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());
    const GitRepository& repository = *repo;

    const Document doc    = makeDoc(u"line1\nline2\nline3\n");
    const auto      result = repository.diffAgainstHead(dir / "a.txt", *doc.snapshot());
    ASSERT_TRUE(result.has_value());
    const auto& regions = *result;
    ASSERT_EQ(regions.size(), 1U);
    EXPECT_EQ(regions[0].kind, LineDiffKind::Added);

    fs::remove_all(dir);
}

// Confirms the Document overload's delegation is exact, not just similarly-
// shaped - both overloads must agree bit-for-bit on the same input.
TEST_F(GitRepositoryTest, DiffAgainstHeadDocumentAndBufferSnapshotOverloadsAgree) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\nline3\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());
    const GitRepository& repository = *repo;

    const Document doc = makeDoc(u"line1\nCHANGED\nline3\n");
    const auto viaDocument       = repository.diffAgainstHead(dir / "a.txt", doc);
    const auto viaBufferSnapshot = repository.diffAgainstHead(dir / "a.txt", *doc.snapshot());
    ASSERT_TRUE(viaDocument.has_value());
    ASSERT_TRUE(viaBufferSnapshot.has_value());
    EXPECT_EQ(*viaDocument, *viaBufferSnapshot);

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, DiffAgainstHeadDetectsModifiedRegion) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\nline3\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc    = makeDoc(u"line1\nCHANGED\nline3\n");
    const auto      result = repo->diffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].kind, LineDiffKind::Modified);

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, DiffAgainstHeadDetectsDeletedRegion) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\nline3\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc    = makeDoc(u"line1\nline3\n");
    const auto      result = repo->diffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].kind, LineDiffKind::Deleted);
    EXPECT_EQ((*result)[0].lineCount, 0U);

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, DiffAgainstHeadReturnsEmptyForIdenticalContent) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc    = makeDoc(u"line1\nline2\n");
    const auto      result = repo->diffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());

    fs::remove_all(dir);
}

// The DoD-critical case: diffAgainstHead() must compare against `doc`'s
// in-memory text, not whatever happens to be on disk (an unsaved edit is
// exactly what a "gutter shows uncommitted changes as you type" feature
// needs to see). This test deliberately makes disk content, HEAD content,
// and the in-memory Document's content all three DIFFERENT from each
// other, so a bug that accidentally reads the file from disk instead of
// `doc` would produce a visibly wrong (disk-vs-HEAD, not doc-vs-HEAD) diff.
TEST_F(GitRepositoryTest, DiffAgainstHeadUsesInMemoryDocumentNotDiskContent) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    // Overwrite the on-disk file with content that would show NO diff
    // against HEAD if diffAgainstHead() mistakenly read from disk.
    writeFile(dir / "a.txt", "line1\nline2\n");

    // The in-memory document differs from BOTH HEAD and disk.
    const Document doc    = makeDoc(u"line1\nline2\nline3 (unsaved)\n");
    const auto      result = repo->diffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1U);
    EXPECT_EQ((*result)[0].kind, LineDiffKind::Added);

    fs::remove_all(dir);
}

}  // namespace
