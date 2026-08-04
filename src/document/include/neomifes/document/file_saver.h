#pragma once

// file_saver - streams a Document's current content to disk (WI-01,
// build_plan.md §5). Mirrors file_loader.h's "free function orchestrates
// I/O + error handling, mutates the passed Document on success" shape
// (see document_open.cpp's openDocumentAt() for the identical pattern on
// the load side) rather than adding a Document::save() member.
//
// Deliberately does NOT touch OriginalBuffer's mmap or rebuild the Piece
// Table in any way - verified via a live probe (Win32 P/Invoke) that
// ReplaceFileW() succeeds against a target file that is still open
// elsewhere via CreateFileW(FILE_SHARE_READ|WRITE|DELETE) +
// MapViewOfFile (exactly OriginalBuffer's own mmap mode, see
// original_buffer.h and file_mapping.cpp's own FILE_SHARE_DELETE
// rationale) - the old mapped view keeps returning its old content
// (orphaned but valid) after the swap, and a fresh open of the same path
// sees the new content. So the Document's existing in-memory
// representation (whatever mix of OriginalBuffer-backed and
// AddBuffer-backed pieces it has) is left completely untouched by a
// save; only markSaved() is called on success.

#include <cstdint>
#include <filesystem>
#include <optional>

#include "neomifes/encoding/encoding.h"

namespace neomifes::document {

class Document;

enum class SaveError {
    CannotCreateTempFile,
    WriteFailed,
    EncodeFailed,
    ReplaceFailed,
    // Extremely rare: ReplaceFileW() failed AND `path` no longer exists AND
    // the original content (which ReplaceFileW's own backup mechanism had
    // moved to `path + ".neomifes-bak"`) could not be moved back into
    // place either. If this is ever returned, the caller must tell the
    // user to manually check `path + ".neomifes-bak"` for their original
    // content - both `doc` (in memory) and that backup file still hold it,
    // it is just not at `path` anymore.
    OriginalFileAtRisk,
};

// Streams doc's current content to `path`, encoded as `enc` with every line
// terminator rewritten to `lineEnding`'s convention (see
// encoding::convertLineEndings()), and a leading BOM if `writeBom` and `enc`
// has a BOM variant. Never materializes the whole document as one string -
// content is read and written in bounded chunks (see file_saver.cpp) so
// peak transient memory does not scale with document size.
//
// On success, calls doc.markSaved() and returns std::nullopt. On failure,
// `doc` is never mutated and `path` is left with its original content -
// see file_saver.cpp's replaceIntoPlace() for exactly how this is verified
// (via post-failure filesystem existence checks, not by trusting a
// specific Win32 error code - GetLastError() values in this family proved
// unreliable to discriminate against during design, see the WI-01 plan).
// Works equally for "Save" (path already exists) and "Save As" (path does
// not exist yet, or exists and should be overwritten) - no special-casing
// needed, this function does not care which case it is.
[[nodiscard]] std::optional<SaveError> saveFile(Document& doc, const std::filesystem::path& path,
                                                 encoding::Encoding    enc,
                                                 encoding::LineEnding lineEnding, bool writeBom);

}  // namespace neomifes::document
