#pragma once

// SearchHistory - shared MRU list of recent search-pattern queries (Phase
// 5c5, master_roadmap.md §5.5). Shared by Find bar and the Grep dialog only
// - NOT the command palette, whose queries are command names (fuzzy-matched
// against a fixed registry), a semantically different kind of "history" than
// a search pattern (mixing them would surface e.g. "undo" as a suggestion
// while text-searching); confirmed with the user before implementation.
//
// Headless (no Win32/JSON-library type in this header - see search_history.cpp
// for the nlohmann::json usage, kept private to the .cpp so no consumer of
// this header needs to link/include it) and Win32-path-independent: this
// class never resolves "%APPDATA%\NeoMIFES" itself, callers inject the path
// (see neomifes::platform::resolveAppDataDir(), app_data_dir.h) - the same
// "inject the path, don't resolve it internally" split file_loader.h already
// uses between FileLoader and OriginalBuffer, chosen for the same reason:
// tests pass an arbitrary temp path instead of the real roaming AppData
// directory.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neomifes::core {

class SearchHistory {
public:
    // Loads from `path`. A missing file, unparsable JSON, or an unexpected
    // "version" field all fall back to an empty SearchHistory rather than a
    // fatal error - losing search history is an acceptable data loss (no
    // error-toast UI exists in this codebase to surface it to the user
    // anyway, matching document::loadFile()'s own "best-effort, degrade
    // gracefully" precedent for a different kind of file).
    [[nodiscard]] static SearchHistory loadFrom(const std::filesystem::path& path);

    // Records `query` as the most recent entry (MRU - inserted at the
    // front). An existing duplicate is removed first (a re-run query moves
    // to the front instead of appearing twice). Once more than kMaxEntries
    // (50, roadmap §5.5) accumulate, the oldest is dropped. Empty queries
    // are ignored (never worth remembering "searched for nothing").
    void record(std::u16string_view query);

    // MRU order - entries()[0] is the most recent.
    [[nodiscard]] const std::vector<std::u16string>& entries() const noexcept { return m_entries; }

    // Ctrl+Up ("older"): if `currentText` matches an existing entry, returns
    // the next-older one (clamped at the oldest - no wraparound, matching
    // GrepBar::moveSelection()'s established "Up at the top stays put"
    // convention in this codebase). If `currentText` doesn't match any entry
    // (the user was mid-typing something new, or the edit was empty),
    // returns the most recent entry instead - so pressing Ctrl+Up always
    // starts browsing from the top regardless of what was being typed.
    // nullopt if history is empty. Deliberately stateless (derives the
    // answer from `currentText` alone, not a remembered browse position) so
    // callers (ui::FindBar/ui::GrepBar) need no re-entrancy-guarded state of
    // their own - see this header's file comment.
    [[nodiscard]] std::optional<std::u16string_view> older(std::u16string_view currentText) const noexcept;

    // Ctrl+Down ("newer") - symmetric to older(). nullopt if `currentText`
    // is already the most recent entry, doesn't match any entry, or history
    // is empty (nothing "newer" to show in any of those cases).
    [[nodiscard]] std::optional<std::u16string_view> newer(std::u16string_view currentText) const noexcept;

    // Writes the current entries to `path` as JSON. Best-effort: any failure
    // (parent directory missing, disk full, permission denied) is silently
    // ignored, same rationale as loadFrom()'s graceful-degradation above.
    void saveTo(const std::filesystem::path& path) const;

private:
    static constexpr std::size_t kMaxEntries = 50;

    std::vector<std::u16string> m_entries;
};

}  // namespace neomifes::core
