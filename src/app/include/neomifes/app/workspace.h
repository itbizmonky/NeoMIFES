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

    // Refuses (returns false, no-op) if `index` is out of range, is the
    // last remaining session, or is dirty (caller's job to confirm
    // discarding first, same convention as main.cpp's
    // confirmDiscardIfDirty()). On success, removes the session and keeps
    // activeIndex() pointing at a valid session.
    bool closeSession(std::size_t index) noexcept;

    void activate(std::size_t index) noexcept;

    [[nodiscard]] bool hasUnsavedChanges() const noexcept;

private:
    std::vector<std::unique_ptr<EditorSession>> m_sessions;
    std::size_t                                 m_activeIndex = 0;
};

}  // namespace neomifes::app
