#pragma once

// autosave (WI-11, build_plan.md §5 "自動保存/バックアップ/クラッシュ復旧") -
// the Win32/Document-aware policy layer behind %APPDATA%\NeoMIFES\autosave\
// <hash>.tmp. Lives in src/app/ (not src/core/ or src/document/) for the
// same reason document_open.h/file_dialogs.h/message_dialogs.h do: it
// combines core::AutosaveIndex, document::saveFile(), and
// EditorSession/Workspace - the "glue" layer that already depends on
// everything below it, matching CLAUDE.md's layered-dependency rule.
//
// Every function here is synchronous, UI-thread I/O (reuses
// document::saveFile()'s existing synchronous chunked-write machinery, same
// as Ctrl+S) - deliberately NOT offloaded to a background thread. A very
// large document's periodic autosave can therefore cause a brief,
// noticeable UI stall; async autosave is a known, accepted limitation,
// deferred as unbenchmarked need (CLAUDE.md rule 10) rather than built
// speculatively.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "neomifes/core/autosave_index.h"

namespace neomifes::app {

class EditorSession;

// Hex-encoded (16 lowercase hex digits, util::fnv1aHash64's full 64-bit
// range) hash of `documentPath`'s weakly_canonical() form - the filename
// stem every %APPDATA%\NeoMIFES\autosave\<this>.tmp uses. Normalizing via
// weakly_canonical (falling back to the unnormalized path on any
// filesystem error, same convention Workspace::openFile()'s own
// canonicalOrSelf() uses) ensures relative/absolute spellings of the same
// file always hash identically.
[[nodiscard]] std::u16string autosaveHashFor(const std::filesystem::path& documentPath);

// Full autosave file path for `documentPath`, given the already-resolved
// autosave directory (injected, not resolved internally here - same
// "inject the path" convention every %APPDATA%-backed class in this
// codebase follows, for headless testability against a temp dir).
[[nodiscard]] std::filesystem::path autosaveFilePathFor(const std::filesystem::path& autosaveDir,
                                                         const std::filesystem::path& documentPath);

// Writes `session`'s CURRENT content to its autosave slot, but ONLY if
// !session.isUntitled() (no real path to protect / recover to) AND
// session.isDirty() (nothing changed since the last save - or the last
// autosave that happened to match - means there is nothing new worth
// writing). Reuses document::saveFile() with keepBackup=false,
// markAsSaved=false (WI-11's own extension, file_saver.h) - the write
// never creates a durable .bak and never clears the document's dirty
// state against its REAL path (only the side autosave copy was written).
// On success, records `session`'s (hash, path) pair into `index` and
// immediately persists `index` to `indexPath` - see core::AutosaveIndex's
// own header comment on why this can't be batched like
// core::SearchHistory/core::RecentFiles are. Never shows an error UI on
// failure (autosave failing silently is preferable to interrupting the
// user with a dialog about a background safety net - same "best effort"
// tone every other %APPDATA% write in this codebase already has); the
// index is simply left unchanged in that case.
void performAutoSave(EditorSession& session, const std::filesystem::path& autosaveDir,
                     core::AutosaveIndex& index, const std::filesystem::path& indexPath);

// Deletes `session`'s autosave file (if any) and its index entry (if any) -
// call after a successful REAL save (the autosave copy is now superseded)
// or an explicit "Don't Save" discard (the autosave copy must not survive
// to be wrongly offered as "crash recovery" for content the user just
// explicitly discarded). No-op for an untitled session (nothing to clean
// up - autosave never wrote anything for one). Best-effort: a failed
// deletion (e.g. the .tmp file was already gone) is silently ignored, same
// tone as saveFile()'s own best-effort backup cleanup.
void clearAutoSave(const EditorSession& session, const std::filesystem::path& autosaveDir,
                   core::AutosaveIndex& index, const std::filesystem::path& indexPath);

struct RecoverableAutoSave {
    std::filesystem::path autosaveTmpPath;
    std::filesystem::path originalPath;
    std::u16string         hash;
};

// Startup scan (main.cpp, Normal launch mode only, before the Workspace is
// constructed): every `index` entry whose <hash>.tmp file still exists in
// `autosaveDir` AND (originalPath no longer exists on disk, OR the .tmp
// file's last-write-time is strictly newer than originalPath's) - exactly
// the comparison build_plan.md's WI-11 spec describes ("対応する正規
// ファイルより新しい autosave があれば復旧を提案する"). An index entry
// whose .tmp file is already gone (e.g. manually deleted) is silently
// skipped, not reported and not force-cleaned here - scanning is read-only,
// cleanup only happens once a candidate has actually been shown to the
// user and handled either way (see main.cpp's recovery loop).
[[nodiscard]] std::vector<RecoverableAutoSave> scanForRecoverableAutoSaves(
    const std::filesystem::path& autosaveDir, const core::AutosaveIndex& index);

}  // namespace neomifes::app
