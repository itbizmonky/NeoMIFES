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
using neomifes::git::GitFileStatus;
using neomifes::git::GitRepository;
using neomifes::git::GitStatusEntry;
using neomifes::git::LineDiffKind;
using neomifes::git::UnifiedDiffLine;
using neomifes::git::UnifiedDiffLineKind;

fs::path uniqueTempDir() {
    fs::path dir = fs::temp_directory_path() / (std::string("nmfs_git_") + std::to_string(std::rand()));
    // std::rand() is never seeded here, so the sequence of names this
    // function hands out is IDENTICAL across every process run of this test
    // binary. A gtest ASSERT_* failure skips the caller's trailing
    // fs::remove_all(dir), so a run that fails partway through can leave a
    // stale directory that a LATER run's same-numbered call collides with
    // (found via StatusListHandlesNonAsciiFileName colliding with its own
    // leftover .git from an earlier failed run of itself). Clearing any
    // pre-existing contents here - rather than trying to make the name
    // itself collision-proof - guarantees every test starts from a truly
    // empty directory regardless of what a previous run left behind.
    fs::remove_all(dir);
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
    // WI-17e: git_index_write_tree() builds a tree object in the object
    // database directly from the in-memory index state - it does NOT also
    // persist that state to the on-disk .git/index file (confirmed against
    // libgit2's own doc comment on git_index_write_tree(), per CLAUDE.md
    // rule 3). Every pre-existing test in this file only ever exercises
    // diffAgainstHead(), which never reads the on-disk index at all (it
    // resolves "HEAD:path" directly against the commit tree and compares
    // against an in-memory Document/BufferSnapshot) - so this gap was
    // invisible until statusList()'s own tests below, the first in this
    // file to actually call git_status_list_new(), which DOES read
    // .git/index from disk. Without this call, every file this fixture
    // just committed shows as GIT_STATUS_INDEX_DELETED (HEAD has it, the
    // stale/empty on-disk index does not) regardless of what the test
    // itself does afterward - discovered via a real, unexplained test
    // failure, not anticipated in advance.
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

// WI-17e: finds the entry whose relativePath matches `name` (path
// comparison, not string comparison, so platform separator differences
// don't matter) - shared by the statusList() tests below, same "small
// local test-only helper" shape uniqueTempDir()/writeFile()/makeDoc() above
// already establish for this file.
//
// `name`'s bytes are UTF-8 (this file compiles with /utf-8, so both the
// source and the narrow-literal execution charset are UTF-8) - but
// fs::path's own std::string_view constructor decodes narrow sources using
// the OS-native (ANSI codepage) encoding on Windows, not UTF-8, silently
// mis-decoding non-ASCII names like "日本語ファイル.txt" into a DIFFERENT
// wide string than the one GitRepository::statusList() itself produces via
// platform::convertToUtf16(..., CP_UTF8). Routing through fs::path's
// char8_t constructor instead - which the standard guarantees always
// decodes as UTF-8 regardless of locale - fixes this (found via
// StatusListHandlesNonAsciiFileName failing even after ruling out every
// other cause).
[[nodiscard]] const GitStatusEntry* findEntry(const std::vector<GitStatusEntry>& entries, std::string_view name) {
    const std::u8string_view utf8Name(reinterpret_cast<const char8_t*>(name.data()), name.size());
    for (const auto& entry : entries) {
        if (entry.relativePath == fs::path(utf8Name)) {
            return &entry;
        }
    }
    return nullptr;
}

TEST_F(GitRepositoryTest, StatusListReturnsEmptyVectorForCleanWorkingTree) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const auto result = repo->statusList();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, StatusListReturnsNulloptForBareRepository) {
    const fs::path    dir     = uniqueTempDir();
    const std::string dirUtf8 = dir.string();
    git_repository*    rawRepo = nullptr;
    ASSERT_EQ(::git_repository_init(&rawRepo, dirUtf8.c_str(), /*is_bare=*/1), 0);
    ::git_repository_free(rawRepo);

    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const auto result = repo->statusList();
    EXPECT_FALSE(result.has_value());

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, StatusListClassifiesModifiedFile) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "original\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    writeFile(dir / "a.txt", "changed\n");

    const auto result = repo->statusList();
    ASSERT_TRUE(result.has_value());
    const GitStatusEntry* entry = findEntry(*result, "a.txt");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, GitFileStatus::Modified);
    EXPECT_EQ(entry->absolutePath, dir / "a.txt");

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, StatusListClassifiesUntrackedFile) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "original\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    writeFile(dir / "new.txt", "brand new\n");

    const auto result = repo->statusList();
    ASSERT_TRUE(result.has_value());
    const GitStatusEntry* entry = findEntry(*result, "new.txt");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, GitFileStatus::Untracked);

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, StatusListClassifiesStagedAddedFile) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "original\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    writeFile(dir / "staged.txt", "staged content\n");
    git_repository* rawRepo = nullptr;
    ASSERT_EQ(::git_repository_open(&rawRepo, dir.string().c_str()), 0);
    git_index* index = nullptr;
    ASSERT_EQ(::git_repository_index(&index, rawRepo), 0);
    ASSERT_EQ(::git_index_add_bypath(index, "staged.txt"), 0);
    ASSERT_EQ(::git_index_write(index), 0);
    ::git_index_free(index);
    ::git_repository_free(rawRepo);

    const auto result = repo->statusList();
    ASSERT_TRUE(result.has_value());
    const GitStatusEntry* entry = findEntry(*result, "staged.txt");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, GitFileStatus::Added);

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, StatusListClassifiesDeletedFile) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "original\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    fs::remove(dir / "a.txt");

    const auto result = repo->statusList();
    ASSERT_TRUE(result.has_value());
    const GitStatusEntry* entry = findEntry(*result, "a.txt");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, GitFileStatus::Deleted);

    fs::remove_all(dir);
}

// WI-17e: without GIT_STATUS_OPT_RENAMES_*, a worktree rename decomposes
// into a Deleted+Untracked pair instead (verified via this WI's own
// standalone probe, git_status_probe.cpp) - this test confirms
// statusList() sets the flags needed to collapse that pair into ONE
// Renamed entry.
TEST_F(GitRepositoryTest, StatusListClassifiesRenamedFileAsOneEntry) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "oldname.txt", "renamed content, long enough for rename detection\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    fs::rename(dir / "oldname.txt", dir / "newname.txt");

    const auto result = repo->statusList();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(findEntry(*result, "oldname.txt"), nullptr);
    const GitStatusEntry* entry = findEntry(*result, "newname.txt");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, GitFileStatus::Renamed);
    EXPECT_EQ(entry->absolutePath, dir / "newname.txt");

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, StatusListHandlesNonAsciiFileName) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "original\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const fs::path japaneseName = fs::path(u8"日本語ファイル.txt");
    writeFile(dir / japaneseName, "japanese filename test\n");

    const auto result = repo->statusList();
    ASSERT_TRUE(result.has_value());
    const GitStatusEntry* entry = findEntry(*result, "日本語ファイル.txt");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, GitFileStatus::Untracked);
    EXPECT_TRUE(fs::exists(entry->absolutePath));

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadReturnsNulloptForUntrackedFile) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc = makeDoc(u"anything\n");
    const auto result  = repo->unifiedDiffAgainstHead(dir / "never-committed.txt", doc);
    EXPECT_FALSE(result.has_value());

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadReturnsNulloptOutsideRepository) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const fs::path outsideDir = uniqueTempDir();
    const Document  doc        = makeDoc(u"anything\n");
    const auto      result     = repo->unifiedDiffAgainstHead(outsideDir / "a.txt", doc);
    EXPECT_FALSE(result.has_value());

    fs::remove_all(dir);
    fs::remove_all(outsideDir);
}

TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadReturnsAllContextForIdenticalContent) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc = makeDoc(u"line1\nline2\n");
    const auto     result = repo->unifiedDiffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[0].kind, UnifiedDiffLineKind::Context);
    EXPECT_EQ((*result)[0].text, u"line1");
    EXPECT_EQ((*result)[1].kind, UnifiedDiffLineKind::Context);
    EXPECT_EQ((*result)[1].text, u"line2");

    fs::remove_all(dir);
}

// Mirrors the exact scenario this WI's own standalone probe
// (git_unified_diff_probe.cpp) verified against real libgit2 output: a
// single-line modification decomposes into one Removed line immediately
// followed by one Added line (not a combined "Modified" kind - unlike
// diffAgainstHead()'s hunk-level LineDiffKind::Modified, a unified diff has
// no such concept, only per-line Added/Removed/Context).
TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadDecomposesModifiedLineIntoRemovedThenAdded) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\nline3\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc = makeDoc(u"line1\nCHANGED\nline3\n");
    const auto     result = repo->unifiedDiffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    const auto& lines = *result;
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_EQ(lines[0].kind, UnifiedDiffLineKind::Context);
    EXPECT_EQ(lines[0].text, u"line1");
    EXPECT_EQ(lines[1].kind, UnifiedDiffLineKind::Removed);
    EXPECT_EQ(lines[1].text, u"line2");
    EXPECT_EQ(lines[2].kind, UnifiedDiffLineKind::Added);
    EXPECT_EQ(lines[2].text, u"CHANGED");
    EXPECT_EQ(lines[3].kind, UnifiedDiffLineKind::Context);
    EXPECT_EQ(lines[3].text, u"line3");

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadDetectsDeletedLine) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\nline3\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc = makeDoc(u"line1\nline3\n");
    const auto     result = repo->unifiedDiffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    const auto& lines = *result;
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0].kind, UnifiedDiffLineKind::Context);
    EXPECT_EQ(lines[1].kind, UnifiedDiffLineKind::Removed);
    EXPECT_EQ(lines[1].text, u"line2");
    EXPECT_EQ(lines[2].kind, UnifiedDiffLineKind::Context);

    fs::remove_all(dir);
}

TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadDetectsAddedLine) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc = makeDoc(u"line1\nline2\nline3\n");
    const auto     result = repo->unifiedDiffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    const auto& lines = *result;
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0].kind, UnifiedDiffLineKind::Context);
    EXPECT_EQ(lines[1].kind, UnifiedDiffLineKind::Context);
    EXPECT_EQ(lines[2].kind, UnifiedDiffLineKind::Added);
    EXPECT_EQ(lines[2].text, u"line3");

    fs::remove_all(dir);
}

// The DoD-critical case (same reasoning as
// DiffAgainstHeadUsesInMemoryDocumentNotDiskContent above): the Diff view
// must reflect unsaved edits, not whatever happens to be on disk.
TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadUsesInMemoryDocumentNotDiskContent) {
    const fs::path dir = uniqueTempDir();
    makeRepoWithCommit(dir, "a.txt", "line1\nline2\n");
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    writeFile(dir / "a.txt", "line1\nDISK-ONLY-CHANGE\n");  // disk differs from both HEAD and the in-memory doc

    const Document doc = makeDoc(u"line1\nMEMORY-CHANGE\n");
    const auto     result = repo->unifiedDiffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    const auto& lines = *result;
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[1].kind, UnifiedDiffLineKind::Removed);
    EXPECT_EQ(lines[1].text, u"line2");
    EXPECT_EQ(lines[2].kind, UnifiedDiffLineKind::Added);
    EXPECT_EQ(lines[2].text, u"MEMORY-CHANGE");

    fs::remove_all(dir);
}

// Distinguishes unifiedDiffAgainstHead()'s default context_lines=3 from
// diffAgainstHead()'s deliberate context_lines=0 (see this method's own
// header comment) - a change on line 5 of a 10-line file must be surrounded
// by exactly 3 Context lines on each side, not 0 and not the whole file.
TEST_F(GitRepositoryTest, UnifiedDiffAgainstHeadIncludesThreeLinesOfContext) {
    const fs::path dir = uniqueTempDir();
    std::string originalUtf8;
    for (int i = 1; i <= 10; ++i) {
        originalUtf8 += "line" + std::to_string(i) + "\n";
    }
    makeRepoWithCommit(dir, "a.txt", originalUtf8);
    auto repo = GitRepository::discover(dir);
    ASSERT_TRUE(repo.has_value());

    const Document doc = makeDoc(
        u"line1\nline2\nline3\nline4\nline5-CHANGED\nline6\nline7\nline8\nline9\nline10\n");
    const auto result = repo->unifiedDiffAgainstHead(dir / "a.txt", doc);
    ASSERT_TRUE(result.has_value());
    const auto& lines = *result;
    // line2,line3,line4 (3 context) + line5 removed + line5-CHANGED added +
    // line6,line7,line8 (3 context) = 8 entries. line1/line9/line10 fall
    // outside the 3-line context window on either side and are absent.
    ASSERT_EQ(lines.size(), 8U);
    EXPECT_EQ(lines[0].text, u"line2");
    EXPECT_EQ(lines[3].kind, UnifiedDiffLineKind::Removed);
    EXPECT_EQ(lines[3].text, u"line5");
    EXPECT_EQ(lines[4].kind, UnifiedDiffLineKind::Added);
    EXPECT_EQ(lines[4].text, u"line5-CHANGED");
    EXPECT_EQ(lines[7].text, u"line8");

    fs::remove_all(dir);
}

}  // namespace
