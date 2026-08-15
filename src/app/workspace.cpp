#include "neomifes/app/workspace.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace neomifes::app {

namespace {

// Normalizes `p` for path-identity comparison (relative vs. absolute
// spellings of the same file should count as "already open"). Falls back
// to the original path on any filesystem error (e.g. a path that no longer
// exists) rather than throwing or failing the comparison outright - an
// unnormalized fallback still compares correctly against itself, just not
// against a differently-spelled duplicate, which is an acceptable
// degradation (same "best-effort, never throw on a stale path" convention
// this codebase uses elsewhere - see jumpToGotoTarget()'s clamping).
std::filesystem::path canonicalOrSelf(const std::filesystem::path& p) {
    std::error_code             ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    return ec ? p : canonical;
}

}  // namespace

Workspace::Workspace(document::Document initialDocument, DocumentFileState initialFileState,
                      const std::optional<std::filesystem::path>& initialPath) {
    m_sessions.push_back(
        std::make_unique<EditorSession>(std::move(initialDocument), initialFileState, initialPath));
}

std::variant<std::size_t, document::LoadError> Workspace::openFile(const std::filesystem::path& path) {
    const std::filesystem::path target = canonicalOrSelf(path);
    for (std::size_t i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i]->isUntitled()) {
            continue;
        }
        if (canonicalOrSelf(m_sessions[i]->path()) == target) {
            m_activeIndex = i;
            return i;
        }
    }
    auto session = std::make_unique<EditorSession>();
    if (const std::optional<document::LoadError> error = session->openFile(path)) {
        return *error;
    }
    m_sessions.push_back(std::move(session));
    m_activeIndex = m_sessions.size() - 1;
    return m_activeIndex;
}

std::size_t Workspace::openBlank() {
    m_sessions.push_back(std::make_unique<EditorSession>());
    m_activeIndex = m_sessions.size() - 1;
    return m_activeIndex;
}

std::size_t Workspace::adoptSession(std::unique_ptr<EditorSession> session) noexcept {
    m_sessions.push_back(std::move(session));
    m_activeIndex = m_sessions.size() - 1;
    return m_activeIndex;
}

bool Workspace::closeSession(std::size_t index) noexcept {
    if (index >= m_sessions.size() || m_sessions.size() <= 1) {
        return false;
    }
    if (m_sessions[index]->isDirty()) {
        return false;
    }
    m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_activeIndex >= m_sessions.size()) {
        m_activeIndex = m_sessions.size() - 1;
    } else if (index < m_activeIndex) {
        --m_activeIndex;
    }
    return true;
}

void Workspace::activate(std::size_t index) noexcept {
    if (index < m_sessions.size()) {
        m_activeIndex = index;
    }
}

bool Workspace::hasUnsavedChanges() const noexcept {
    return std::ranges::any_of(m_sessions,
                                [](const std::unique_ptr<EditorSession>& session) { return session->isDirty(); });
}

}  // namespace neomifes::app
