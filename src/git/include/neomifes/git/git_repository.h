#pragma once

// GitRepository - headless, file-scoped Git diff computation (WI-17a,
// Phase 11.1 core, ADR-022). Mirrors neomifes::logmode/jsontree/csvmode's
// "headless first" staging: no async worker, no EditorSession integration,
// no UI - see this WI's build_plan.md entry for the full sub-WI breakdown.
// Win32-mechanics-free and libgit2-opaque at the boundary: `git_repository`
// is forward-declared here (never defined - it is libgit2's own opaque
// handle type) so this header never requires <git2.h> from its own callers,
// the same "consumers of this module never need the vendored library's own
// headers" boundary src/git/CMakeLists.txt's own comment establishes for
// the whole module.
//
// Requires neomifes::git::initializeLibgit2() (git_init.h) to have already
// succeeded once for this process before any GitRepository method is
// called - this class does not initialize libgit2's runtime itself, the
// same "assume the shared resource already exists" contract this
// codebase's render:: types already have toward Direct2D/DirectWrite
// factories.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "neomifes/document/text_pos.h"  // document::LineNumber

struct git_repository;  // libgit2's own opaque handle type (git2/types.h) - never defined here

namespace neomifes::document {
class Document;
class BufferSnapshot;
}  // namespace neomifes::document

namespace neomifes::git {

enum class LineDiffKind : std::uint8_t { Added, Modified, Deleted };

// One contiguous diff region in the CURRENT document's line-number space
// (0-based, matching document::LineNumber's own convention throughout this
// codebase) - one entry per libgit2 diff hunk, not one entry per line, so a
// large localized change stays compact rather than exploding into one
// region per line.
//
// Added/Modified: [startLine, startLine + lineCount) are the CURRENT
// document's own lines this region covers. Deleted: HEAD had lines here
// that the current document no longer has at all - there is no current-
// document line range to report (the deleted content simply isn't present
// any more), so this is a POINT marker: startLine is the current-document
// line immediately AFTER where the deleted content used to be (matching
// the gutter-marker convention GitLens/VSCode use - a small indicator
// between two existing lines, not a highlighted range), and lineCount is
// always 0 for this kind.
struct LineDiffRegion {
    document::LineNumber startLine = 0;
    document::LineNumber lineCount = 0;
    LineDiffKind          kind      = LineDiffKind::Added;

    friend bool operator==(const LineDiffRegion&, const LineDiffRegion&) = default;
};

// WI-17e: one file's status in a repo-wide "git status" scan (statusList()
// below) - a DIFFERENT feature from LineDiffRegion/diffAgainstHead() above,
// which is scoped to a single file's line-level hunks. Collapses libgit2's
// many GIT_STATUS_* bits (see statusList()'s own .cpp comment for the
// priority order) into one value per file - a Git pane listing changed
// files needs "what kind of change is this file" as a single glyph, not a
// bitmask.
enum class GitFileStatus : std::uint8_t { Modified, Added, Deleted, Renamed, Untracked };

struct GitStatusEntry {
    // Workdir-joined - ready to pass straight to Workspace::openFile(). For
    // a Renamed entry, this is the NEW (current) path, matching what a user
    // clicking this entry to open the file would expect.
    std::filesystem::path absolutePath;
    // Workdir-relative - display-only, matches `git status`'s own
    // convention of showing paths relative to the repository root rather
    // than the (possibly deeply nested) absolute path.
    std::filesystem::path relativePath;
    GitFileStatus          status = GitFileStatus::Modified;

    friend bool operator==(const GitStatusEntry&, const GitStatusEntry&) = default;
};

class GitRepository {
public:
    // Walks upward from startPath (a file OR directory path) looking for a
    // .git directory/file, the same search libgit2's own git_repository_
    // open_ext() performs by default (GIT_REPOSITORY_OPEN_NO_SEARCH is
    // deliberately NOT passed) - matches `git`'s own CLI behavior of
    // working from any subdirectory of a repo. std::nullopt if no
    // repository is found, or if libgit2 itself fails to open one it did
    // find (never throws).
    [[nodiscard]] static std::optional<GitRepository> discover(const std::filesystem::path& startPath);

    GitRepository(const GitRepository&)            = delete;
    GitRepository& operator=(const GitRepository&) = delete;
    GitRepository(GitRepository&&) noexcept;
    GitRepository& operator=(GitRepository&&) noexcept;
    ~GitRepository();

    // Computes a line-level diff between HEAD's own blob for
    // `absoluteFilePath` and `snapshot`'s CURRENT in-memory text (NOT
    // whatever is currently on disk - the snapshot may hold unsaved edits,
    // and diffing against the live buffer rather than the file is the whole
    // point of a "gutter shows your uncommitted changes as you type"
    // feature). This is the primary entry point (WI-17b) - takes a
    // BufferSnapshot rather than a Document so a background thread
    // (git::GitDiffWorker) can call it safely without touching the UI-
    // thread-owned Document (document::Document is UI-thread-only per
    // ADR-009; document::BufferSnapshot is not), the same reasoning
    // jsontree::parseJsonTree()'s own BufferSnapshot overload documents.
    //
    // std::nullopt (not an empty vector) when: `absoluteFilePath` is
    // outside this repository's working directory, OR HEAD has no blob for
    // this path (an untracked/newly-added file, or a repository with no
    // commits yet) - callers should treat either case as "the whole file
    // is new, don't ask this class about it" rather than "no changes".
    // An empty (non-null) vector means HEAD's blob and the snapshot's
    // current text are identical from libgit2's own diff algorithm's
    // perspective.
    [[nodiscard]] std::optional<std::vector<LineDiffRegion>> diffAgainstHead(
        const std::filesystem::path& absoluteFilePath, const document::BufferSnapshot& snapshot) const;

    // Convenience overload for UI-thread callers that only have a Document
    // at hand - snapshots it and delegates to the BufferSnapshot overload
    // above. Same shape as jsontree::parseJsonTree()'s Document overload.
    [[nodiscard]] std::optional<std::vector<LineDiffRegion>> diffAgainstHead(
        const std::filesystem::path& absoluteFilePath, const document::Document& doc) const;

    // WI-17e: repo-wide "git status" scan (HEAD vs index vs working
    // directory) - powers the Git pane's changed-file list. Deliberately
    // does NOT take a document::BufferSnapshot/Document the way
    // diffAgainstHead() does: that method diffs against a document's live,
    // possibly-unsaved in-memory text because "the gutter reacts as you
    // type" is its whole point, but there is no sane way to synthesize "as
    // if every open tab were saved" for a repo-wide scan covering
    // potentially dozens of files most of which aren't even open - this
    // always reports real, on-disk status, exactly like running `git
    // status` in a terminal or VSCode's own Source Control view.
    //
    // std::nullopt only for a bare repository (no working directory to
    // scan) - same guard diffAgainstHead() already uses. An empty
    // (non-null) vector means a clean working tree. Never throws except
    // std::bad_alloc (same documented-not-noexcept-enforced contract as
    // discover()/diffAgainstHead()).
    [[nodiscard]] std::optional<std::vector<GitStatusEntry>> statusList() const;

private:
    struct RepoDeleter {
        void operator()(git_repository* repo) const noexcept;
    };

    explicit GitRepository(std::unique_ptr<git_repository, RepoDeleter> repo) noexcept;

    std::unique_ptr<git_repository, RepoDeleter> m_repo;
};

}  // namespace neomifes::git
