#pragma once

// Workspace - the set of open EditorSessions plus which one is active
// (WI-04, tab-switching wired up in WI-05). One window == one Workspace.
// EditorSession itself can't be moved/copied (see editor_session.h's header
// comment), so sessions live behind unique_ptr for a stable address across
// std::vector reallocation.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "neomifes/app/editor_session.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/git/git_repository.h"  // git::GitStatusEntry

namespace neomifes::git {
class GitStatusWorker;
}  // namespace neomifes::git

namespace neomifes::app {

class Workspace {
public:
    explicit Workspace(document::Document                          initialDocument  = document::Document{},
                        DocumentFileState                            initialFileState = {},
                        const std::optional<std::filesystem::path>& initialPath      = std::nullopt);

    Workspace(const Workspace&)            = delete;
    Workspace& operator=(const Workspace&) = delete;
    Workspace(Workspace&&)                 = delete;
    Workspace& operator=(Workspace&&)      = delete;
    ~Workspace() = default;

    [[nodiscard]] EditorSession&       active() noexcept { return *m_sessions[m_activeIndex]; }
    [[nodiscard]] const EditorSession& active() const noexcept { return *m_sessions[m_activeIndex]; }
    [[nodiscard]] std::size_t activeIndex() const noexcept { return m_activeIndex; }
    [[nodiscard]] std::size_t sessionCount() const noexcept { return m_sessions.size(); }
    [[nodiscard]] EditorSession&       sessionAt(std::size_t index) { return *m_sessions.at(index); }
    [[nodiscard]] const EditorSession& sessionAt(std::size_t index) const {
        return *m_sessions.at(index);
    }

    // `path` already open in some session (weakly_canonical-normalized
    // comparison, so relative/absolute spellings of the same file match) -
    // activates that tab (unchanged) and returns its index. Otherwise opens
    // a new EditorSession for `path`, appends it, activates it, and returns
    // its index. Returns the LoadError on failure and leaves the Workspace
    // completely untouched (EditorSession::openFile()'s own
    // no-partial-mutation-on-failure contract, preserved here) - WI-05:
    // widened from optional<size_t> to variant<size_t, LoadError> so
    // Ctrl+O's showOpenErrorDialog() can keep reporting the specific
    // failure reason, matching this codebase's established
    // variant<Success, LoadError> convention (document_open.h's
    // openDocumentAt()) rather than introducing std::expected as a second
    // "success or failure" vocabulary for the same error type.
    [[nodiscard]] std::variant<std::size_t, document::LoadError> openFile(
        const std::filesystem::path& path);

    // Appends a new blank/untitled EditorSession and activates it. Never
    // fails (no I/O) - the Ctrl+N/new-tab counterpart to openFile().
    [[nodiscard]] std::size_t openBlank();

    // WI-11: appends an ALREADY-CONSTRUCTED session and activates it - the
    // crash-recovery counterpart to openFile()/openBlank() above, for a
    // session whose Document didn't come from a normal disk-backed
    // openFile() call (main.cpp builds it directly from an autosave
    // snapshot via document::loadFile() + Document::markDirty(), see
    // app::scanForRecoverableAutoSaves()). Never fails - same "append,
    // activate, done" shape as openBlank().
    std::size_t adoptSession(std::unique_ptr<EditorSession> session) noexcept;

    // Refuses (returns false, no-op) if `index` is out of range, is the
    // last remaining session, or is dirty (caller's job to confirm
    // discarding first, same convention as main.cpp's
    // confirmDiscardIfDirty()). On success, removes the session and keeps
    // activeIndex() pointing at a valid session.
    bool closeSession(std::size_t index) noexcept;

    void activate(std::size_t index) noexcept;

    [[nodiscard]] bool hasUnsavedChanges() const noexcept;

    // WI-17e: repo-wide Git status, deliberately stored on Workspace rather
    // than per-EditorSession the way gitDiff()/csvModel()/jsonTree() are.
    // Git status is a property of the REPOSITORY, not of any one open
    // document - two tabs open on the same repo would otherwise redundantly
    // re-fetch and could show inconsistent same-repo results depending on
    // fetch timing. GitPane is itself a single window-wide instance (not
    // one per tab), matching this placement.
    //
    // Same std::nullopt-collapses-two-cases ambiguity as gitDiff(): "never
    // requested" and "requested, but not inside a Git repository" both read
    // as std::nullopt (GitStatusWorker always posts a response either way -
    // see git_status_worker.h's own header comment).
    [[nodiscard]] const std::optional<std::vector<git::GitStatusEntry>>& gitStatus() const noexcept {
        return m_gitStatus;
    }
    [[nodiscard]] bool gitStatusInFlight() const noexcept { return m_gitStatusInFlight; }

    // Fires an async GitStatusWorker request scoped to the ACTIVE session's
    // path, using `this` (the Workspace, not the session) as the opaque
    // sessionToken - matches this feature's Workspace-wide cache, not a
    // per-session one. If the active session is Untitled (no path),
    // actively CLEARS m_gitStatus to std::nullopt instead of leaving it
    // untouched - unlike EditorSession::beginGitDiffIndexing()'s simple
    // no-op. That no-op is safe there only because gitDiff() is a per-
    // session cache with no other tab's data to confuse it with; here, a
    // stale m_gitStatus left over from a previously active session's
    // repository would otherwise keep showing after switching to an
    // Untitled tab, which has no repository of its own to report.
    void beginGitStatusIndexing(git::GitStatusWorker& worker);

    // Called by the kMsgGitStatusReady receiver once the worker's response
    // arrives. `result` is std::nullopt whenever the requested path wasn't
    // inside a Git repository - GitStatusWorker always posts a response,
    // precisely so this method is reachable and gitStatusInFlight() never
    // gets stuck at true.
    void applyGitStatusResult(std::optional<std::vector<git::GitStatusEntry>> result) noexcept {
        m_gitStatus         = std::move(result);
        m_gitStatusInFlight = false;
    }

private:
    std::vector<std::unique_ptr<EditorSession>> m_sessions;
    std::size_t                                 m_activeIndex = 0;
    std::optional<std::vector<git::GitStatusEntry>> m_gitStatus;
    bool                                              m_gitStatusInFlight = false;
};

}  // namespace neomifes::app
