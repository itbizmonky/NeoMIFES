#pragma once

// RecentFiles - MRU list of recently opened/saved file paths (WI-11,
// build_plan.md §5 "最近開いたファイル"). Persisted as
// %APPDATA%\NeoMIFES\recent.json, shown in the File menu (see
// app::buildMenuBar()/app::refreshRecentFilesMenu()).
//
// Modeled directly on core::SearchHistory (search_history.h) - same
// loadFrom/saveTo + inject-the-path + graceful-degrade-on-corruption
// contract, same MRU record()-dedups-and-moves-to-front behavior. Batched
// save (once at clean exit, main.cpp - not written on every record() call)
// is intentional and differs from core::AutosaveIndex's own immediate-write
// requirement: losing the latest MRU reordering to a crash is a minor,
// acceptable UX inconvenience, not the correctness-critical "must survive
// to be found again" requirement AutosaveIndex has.

#include <cstddef>
#include <filesystem>
#include <vector>

namespace neomifes::core {

class RecentFiles {
public:
    // Loads from `path`. A missing file, unparsable JSON, or an unexpected
    // "version" field all fall back to an empty RecentFiles - losing the
    // recent-files list is acceptable data loss (no error-toast UI exists
    // in this codebase to surface it), same rationale as SearchHistory::
    // loadFrom().
    [[nodiscard]] static RecentFiles loadFrom(const std::filesystem::path& path);

    // Records `filePath` as the most-recent entry (MRU - inserted at the
    // front). An existing entry for the SAME FILE (weakly_canonical-
    // normalized comparison, so relative/absolute spellings of the same
    // path count as one entry - matches Workspace::openFile()'s own
    // dedup convention) is removed first, so re-opening/re-saving an
    // already-listed file moves it to front instead of duplicating. Once
    // more than kMaxEntries (20, build_plan.md) accumulate, the oldest is
    // dropped.
    void record(const std::filesystem::path& filePath);

    // MRU order - entries()[0] is the most recent.
    [[nodiscard]] const std::vector<std::filesystem::path>& entries() const noexcept { return m_entries; }

    // Writes the current entries to `path` as JSON. Best-effort: any
    // failure (parent directory missing, disk full, permission denied) is
    // silently ignored, same rationale as loadFrom()'s graceful
    // degradation above.
    void saveTo(const std::filesystem::path& path) const;

private:
    static constexpr std::size_t kMaxEntries = 20;

    std::vector<std::filesystem::path> m_entries;
};

}  // namespace neomifes::core
