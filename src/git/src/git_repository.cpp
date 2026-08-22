#include "neomifes/git/git_repository.h"

#include <git2.h>

#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/util/utf8_convert.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::git {

namespace {

// This project's u16string<->UTF-8 boundary is neomifes::util::
// toUtf8WithOffsets() (jsontree already uses it for whole-document-sized
// input, e.g. json_tree.cpp's parseJsonTree() - its own "not a bulk
// whole-document converter" header comment is a usage HINT, not an
// enforced limit, and this module follows the same established precedent
// rather than inventing a second UTF-16->UTF-8 helper). The offset table
// toUtf8WithOffsets() also builds is unneeded here (libgit2's own hunk/line
// positions are already reported as GIT-side line numbers, not byte
// offsets this project would need to map back) and is simply discarded.
[[nodiscard]] std::string wideToUtf8(std::wstring_view wide) {
    return util::toUtf8WithOffsets(util::fromWstringView(wide)).utf8;
}

// One LineDiffRegion per libgit2 diff hunk (see git_repository.h's own
// LineDiffRegion comment for the exact classification rule this mirrors):
// old_lines==0 is a pure addition, new_lines==0 is a pure deletion (a point
// marker, no current-document line range to report), otherwise the hunk
// contains both removed and added content and counts as a modification.
int onHunk(const git_diff_delta* /*delta*/, const git_diff_hunk* hunk, void* payload) {
    auto* regions = static_cast<std::vector<LineDiffRegion>*>(payload);
    LineDiffRegion region;
    if (hunk->old_lines == 0) {
        region.kind      = LineDiffKind::Added;
        region.startLine = static_cast<document::LineNumber>(hunk->new_start - 1);
        region.lineCount = static_cast<document::LineNumber>(hunk->new_lines);
    } else if (hunk->new_lines == 0) {
        region.kind      = LineDiffKind::Deleted;
        region.startLine = static_cast<document::LineNumber>(hunk->new_start);
        region.lineCount = 0;
    } else {
        region.kind      = LineDiffKind::Modified;
        region.startLine = static_cast<document::LineNumber>(hunk->new_start - 1);
        region.lineCount = static_cast<document::LineNumber>(hunk->new_lines);
    }
    regions->push_back(region);
    return 0;
}

}  // namespace

void GitRepository::RepoDeleter::operator()(git_repository* repo) const noexcept {
    ::git_repository_free(repo);
}

GitRepository::GitRepository(std::unique_ptr<git_repository, RepoDeleter> repo) noexcept : m_repo(std::move(repo)) {}

GitRepository::GitRepository(GitRepository&&) noexcept            = default;
GitRepository& GitRepository::operator=(GitRepository&&) noexcept = default;
GitRepository::~GitRepository()                                  = default;

std::optional<GitRepository> GitRepository::discover(const std::filesystem::path& startPath) {
    git_repository*   raw           = nullptr;
    const std::string startPathUtf8 = wideToUtf8(startPath.wstring());
    // flags=0 (GIT_REPOSITORY_OPEN_NO_SEARCH deliberately NOT passed) walks
    // upward through parent directories looking for .git, the same default
    // behavior `git` itself uses from any subdirectory of a repository -
    // confirmed against the vendored git2/repository.h's own doc comment
    // before writing this (CLAUDE.md rule 3), not assumed from memory.
    if (::git_repository_open_ext(&raw, startPathUtf8.c_str(), 0, nullptr) != 0) {
        return std::nullopt;
    }
    return GitRepository(std::unique_ptr<git_repository, RepoDeleter>(raw));
}

std::optional<std::vector<LineDiffRegion>> GitRepository::diffAgainstHead(
    const std::filesystem::path& absoluteFilePath, const document::Document& doc) const {
    const char* workdir = ::git_repository_workdir(m_repo.get());
    if (workdir == nullptr) {
        return std::nullopt;  // bare repository - no working directory to compare against
    }

    std::error_code            ec;
    const std::filesystem::path relative =
        std::filesystem::relative(absoluteFilePath, std::filesystem::path(workdir), ec);
    const std::wstring relativeGeneric = relative.generic_wstring();
    if (ec || relative.empty() || relativeGeneric.starts_with(L"..")) {
        return std::nullopt;  // outside this repository's working directory
    }
    // libgit2 path specs (both the "HEAD:path" revspec below and the
    // old_as_path/buffer_as_path hints git_diff_blob_to_buffer() uses for
    // its own binary-detection heuristics) always use '/' regardless of
    // host OS - generic_wstring() (not wstring(), which keeps native '\')
    // is what produces that.
    const std::string relativeUtf8 = wideToUtf8(relativeGeneric);

    const std::string revspec = "HEAD:" + relativeUtf8;
    git_object*        headObj = nullptr;
    // Fails (GIT_ENOTFOUND/GIT_EUNBORNBRANCH/etc.) for an untracked file or
    // a repository with no commits yet - both collapse to the same
    // std::nullopt contract this method's own header comment documents
    // ("the whole file is new, don't ask this class about it").
    if (::git_revparse_single(&headObj, m_repo.get(), revspec.c_str()) != 0) {
        return std::nullopt;
    }
    if (::git_object_type(headObj) != GIT_OBJECT_BLOB) {
        ::git_object_free(headObj);
        return std::nullopt;  // the path resolved to something other than a blob (e.g. a submodule gitlink)
    }
    // git_object and git_blob share the same underlying layout - this
    // reinterpret_cast is libgit2's own documented idiom once the type has
    // been confirmed via git_object_type() above, not an aliasing risk this
    // project introduces.
    auto* headBlob = reinterpret_cast<git_blob*>(headObj);

    const document::TextPos    length      = doc.length();
    const std::u16string        currentText = doc.snapshot()->extract(document::TextRange{.start = 0, .end = length});
    const std::string           currentUtf8 = util::toUtf8WithOffsets(currentText).utf8;

    git_diff_options options;
    ::git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
    // GIT_DIFF_OPTIONS_INIT's own default is 3 (git2/diff.h's own doc
    // comment on context_lines) - the right value for a human-readable
    // patch view, but wrong for onHunk()'s classification below: 3 lines
    // of unchanged context bundled into a hunk make old_lines/new_lines
    // both nonzero even for a pure insertion or deletion, misclassifying
    // it as Modified. A gutter-marker feature wants to know exactly which
    // lines actually changed, not a human-readable context window, so this
    // must be 0 - confirmed by writing this exact bug into the initial
    // implementation and having DiffAgainstHeadDetectsAddedRegion/
    // DetectsDeletedRegion/UsesInMemoryDocumentNotDiskContent all fail with
    // Modified instead of Added/Deleted before this line was added.
    options.context_lines = 0;

    std::vector<LineDiffRegion> regions;
    // file_cb/binary_cb/line_cb are nullptr - confirmed safe against the
    // vendored patch_generate.c source (each call site null-checks before
    // invoking, and passing at least one of binary_cb/hunk_cb/data_cb - here
    // hunk_cb - keeps libgit2 from short-circuiting content loading
    // entirely) before writing this, per CLAUDE.md rule 3.
    const int rc = ::git_diff_blob_to_buffer(headBlob, relativeUtf8.c_str(), currentUtf8.data(), currentUtf8.size(),
                                             relativeUtf8.c_str(), &options, nullptr, nullptr, &onHunk, nullptr,
                                             &regions);
    ::git_blob_free(headBlob);
    if (rc != 0) {
        return std::nullopt;
    }
    return regions;
}

}  // namespace neomifes::git
