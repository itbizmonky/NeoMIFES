#pragma once

// AutosaveIndex - hash-to-original-path lookup for crash recovery (WI-11,
// build_plan.md §5 "クラッシュ復旧"). Persisted as
// %APPDATA%\NeoMIFES\autosave\index.json.
//
// Why this exists: autosave files are named %APPDATA%\NeoMIFES\autosave\
// <hash>.tmp (see app::autosaveHashFor()), where hash is a one-way,
// non-reversible function of the document's real path (filesystem-safe,
// fixed-length - see util::fnv1aHash64()'s own header comment for why a
// hash was chosen at all). Scanning the autosave DIRECTORY alone at
// startup therefore can't answer "which real file does this .tmp belong
// to?" - this index is the (hash -> original path) side table that makes
// that lookup possible.
//
// Same loadFrom/saveTo + inject-the-path + graceful-degrade-on-corruption
// contract as core::Settings/core::SearchHistory/core::RecentFiles.
// UNLIKE those three, callers must call saveTo() IMMEDIATELY after every
// record()/remove() (see app::performAutoSave()/app::clearAutoSave()) -
// NOT batched once at clean exit. The entire point of this index is to
// survive a crash; if it were only written at a clean exit, a crash would
// mean the index never learned about the very autosave file it exists to
// help recover.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace neomifes::core {

struct AutosaveEntry {
    std::u16string        hash;          // hex-encoded, matches the .tmp file's stem
    std::filesystem::path originalPath;
};

class AutosaveIndex {
public:
    // Loads from `path`. A missing file, unparsable JSON, or an unexpected
    // "version" field all fall back to an empty AutosaveIndex - same
    // graceful-degradation rationale as the other persisted core:: classes
    // (losing the index just means any surviving .tmp files become
    // unreachable orphans, not a crash or data corruption).
    [[nodiscard]] static AutosaveIndex loadFrom(const std::filesystem::path& path);

    // Upserts: replaces any existing entry with the same `hash` (a hash is
    // 1:1 with a real path - the same path always hashes identically - so
    // re-recording the same document's autosave is naturally idempotent,
    // never an append-duplicate).
    void record(std::u16string_view hash, const std::filesystem::path& originalPath);

    // No-op if `hash` isn't present.
    void remove(std::u16string_view hash);

    [[nodiscard]] const std::vector<AutosaveEntry>& entries() const noexcept { return m_entries; }

    // Writes the current entries to `path` as JSON. Best-effort: any
    // failure (parent directory missing, disk full, permission denied) is
    // silently ignored, same rationale as loadFrom()'s graceful
    // degradation above.
    void saveTo(const std::filesystem::path& path) const;

private:
    std::vector<AutosaveEntry> m_entries;
};

}  // namespace neomifes::core
