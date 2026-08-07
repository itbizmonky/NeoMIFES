#pragma once

// Workspace - the set of open EditorSessions plus which one is active
// (WI-04). One window == one Workspace. Only ever holds exactly one
// EditorSession today - tab UI (WI-05) is what will grow this to more than
// one - but this class is fully implemented and unit-tested now (not a
// stub) since main.cpp already needs SOME container to own the startup
// EditorSession with a stable address (see editor_session.h's header
// comment on why EditorSession itself can't be moved/copied).
//
// Existing keybindings (Ctrl+O etc.) intentionally do NOT route through
// Workspace::openFile() yet - they still call active().openFile(...)
// directly, unchanged from before this class existed (WI-04 is a pure
// refactor; this constructor/openFile() plumbing is unused by main.cpp
// until WI-05 wires tab creation to it).

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "neomifes/app/editor_session.h"
#include "neomifes/document/document.h"

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
    // its index. Returns nullopt on load failure and leaves the Workspace
    // completely untouched (EditorSession::openFile()'s own
    // no-partial-mutation-on-failure contract, preserved here).
    [[nodiscard]] std::optional<std::size_t> openFile(const std::filesystem::path& path);

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
