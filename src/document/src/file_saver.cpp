#include "neomifes/document/file_saver.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <span>
#include <system_error>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/platform/handle_guard.h"

namespace neomifes::document {

namespace {

namespace fs = std::filesystem;

// Line-boundary chunk size (unbenchmarked initial value, CLAUDE.md rule
// 10 - tune once a real large-file save has been measured).
constexpr std::uint64_t kLinesPerChunk = 4096;

// Hard cap (UTF-16 code units, ~2MiB) on any single extract()/encode()
// call, regardless of line boundaries. Exists because Document's line
// model counts only '\n' (PieceTable::countNewlines()) - a CR-only file
// has lineCount()==1 for its ENTIRE content, and a huge minified/single-
// line file is exactly one "line" - so line-boundary chunking alone can
// degenerate to one chunk spanning the whole document. This cap is the
// insurance policy for that path; ordinary multi-line documents never
// come close to it within a kLinesPerChunk window in practice, but the
// cap applies uniformly so no separate code path is needed either way.
constexpr std::uint64_t kMaxChunkCodeUnits = 1ULL << 20;

[[nodiscard]] constexpr bool isHighSurrogate(char16_t c) noexcept { return c >= 0xD800 && c <= 0xDBFF; }
[[nodiscard]] constexpr bool isLowSurrogate(char16_t c) noexcept { return c >= 0xDC00 && c <= 0xDFFF; }

// Writes `bytes` fully to `file`, looping since a single WriteFile() call
// is not guaranteed to consume the whole span for very large writes.
[[nodiscard]] bool writeAll(HANDLE file, std::span<const std::byte> bytes) noexcept {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const std::size_t remaining = bytes.size() - written;
        // Cap each individual WriteFile() call well under DWORD's range -
        // matches the same defensive chunking rationale as kMaxChunkCodeUnits.
        const auto toWrite = static_cast<DWORD>(std::min<std::size_t>(remaining, 1ULL << 24));
        DWORD actuallyWritten = 0;
        if (!::WriteFile(file, bytes.data() + written, toWrite, &actuallyWritten, nullptr) ||
            actuallyWritten == 0) {
            return false;
        }
        written += actuallyWritten;
    }
    return true;
}

// For a candidate sub-chunk end `end` strictly inside (pos, windowEnd) -
// i.e. NOT already a real line boundary - nudges it back by one code unit
// if the code unit pair straddling `end` is a surrogate pair or a CRLF
// pair, so the resulting chunk boundary never splits either. Only reached
// from the "single oversized line" path (see nextSubChunkEnd()) - ordinary
// line-boundary chunk ends are real '\n'-adjacent offsets and never need
// this adjustment.
[[nodiscard]] TextPos adjustSubChunkEnd(const BufferSnapshot& snap, TextPos pos, TextPos end) noexcept {
    const std::u16string boundary = snap.extract(TextRange{.start = end - 1, .end = end + 1});
    if (boundary.size() != 2) {
        return end;  // at document end or similar edge - nothing to straddle
    }
    const bool splitsSurrogatePair = isHighSurrogate(boundary[0]) && isLowSurrogate(boundary[1]);
    const bool splitsCrlf          = boundary[0] == u'\r' && boundary[1] == u'\n';
    if (!splitsSurrogatePair && !splitsCrlf) {
        return end;
    }
    const TextPos adjusted = end - 1;
    return adjusted > pos ? adjusted : pos + 1;  // guarantee forward progress either way
}

// Computes the end of the next extract() sub-chunk starting at `pos`
// within [pos, windowEnd), capped at kMaxChunkCodeUnits. Always returns a
// value in (pos, windowEnd] - the caller's loop is guaranteed to progress.
[[nodiscard]] TextPos nextSubChunkEnd(const BufferSnapshot& snap, TextPos pos,
                                      TextPos windowEnd) noexcept {
    TextPos end = std::min(pos + kMaxChunkCodeUnits, windowEnd);
    if (end < windowEnd) {
        end = adjustSubChunkEnd(snap, pos, end);
    }
    return end;
}

// Streams doc's whole content through `tempFile` in bounded chunks: a
// leading BOM (if requested) written once up front, then line-boundary
// windows (sub-chunked per nextSubChunkEnd() for oversized single lines),
// each line-ending-converted and encoded independently. Every chunk after
// the BOM uses the non-BOM Encoding variant (the BOM is never repeated).
[[nodiscard]] std::optional<SaveError> writeChunks(HANDLE tempFile, const Document& doc,
                                                    const BufferSnapshot& snap, encoding::Encoding enc,
                                                    encoding::LineEnding lineEnding,
                                                    bool                  writeBom) {
    const encoding::Encoding bodyEncoding = encoding::withBom(enc, false);

    if (writeBom) {
        const auto encoded = encoding::encode(u"", encoding::withBom(enc, true));
        const auto* bytes  = std::get_if<std::vector<std::byte>>(&encoded);
        if (bytes == nullptr) {
            return SaveError::EncodeFailed;
        }
        if (!writeAll(tempFile, *bytes)) {
            return SaveError::WriteFailed;
        }
    }

    const std::uint64_t totalLines  = std::max<std::uint64_t>(doc.lineCount(), 1);
    const TextPos        documentEnd = snap.length();

    for (std::uint64_t startLine = 0; startLine < totalLines; startLine += kLinesPerChunk) {
        const std::uint64_t endLineExclusive = std::min(startLine + kLinesPerChunk, totalLines);
        const TextPos windowStart = doc.lineToOffset(startLine);
        const TextPos windowEnd =
            (endLineExclusive < totalLines) ? doc.lineToOffset(endLineExclusive) : documentEnd;

        for (TextPos pos = windowStart; pos < windowEnd;) {
            const TextPos        subEnd = nextSubChunkEnd(snap, pos, windowEnd);
            // extractNoCache(), not extract(): across the whole save, this
            // loop's sub-chunks cover the ENTIRE document exactly once each
            // (encode-and-write, then discard). Using the caching extract()
            // here would permanently retain the whole file as decoded UTF-16
            // in OriginalBuffer just from saving it - see
            // docs/issues/decode_cache_unbounded_growth.md.
            const std::u16string text   = encoding::convertLineEndings(
                snap.extractNoCache(TextRange{.start = pos, .end = subEnd}), lineEnding);
            const auto  encoded = encoding::encode(text, bodyEncoding);
            const auto* bytes   = std::get_if<std::vector<std::byte>>(&encoded);
            if (bytes == nullptr) {
                return SaveError::EncodeFailed;
            }
            if (!writeAll(tempFile, *bytes)) {
                return SaveError::WriteFailed;
            }
            pos = subEnd;
        }
    }
    return std::nullopt;
}

// Atomically swaps `tempPath` into `path`'s place, backing up the previous
// content at `backupPath` first. Safety is verified by POST-FAILURE
// FILESYSTEM STATE, not by branching on GetLastError() - a live probe
// during design found ERROR_FILE_NOT_FOUND is returned both when `path`
// doesn't exist yet (the expected "new file" case) AND would be returned
// for a caller bug (missing replacement file), making the error code alone
// ambiguous; checking what actually happened on disk is unambiguous
// regardless of the exact Win32 error family, and was directly verified
// empirically (locked-backup-path probe: ReplaceFileW failed but `path`'s
// original content was provably untouched).
//
// `keepBackup` (WI-11): on success, instead of best-effort DELETING
// `backupPath` (the pre-save content ReplaceFileW itself moved there),
// best-effort RENAME it to `keptBackupPath` - turning ReplaceFileW's
// internal implementation detail into the durable, user-facing `.bak` file
// build_plan.md's WI-11 spec calls for. fs::rename on Windows resolves to
// MoveFileExW with MOVEFILE_REPLACE_EXISTING, so this safely overwrites
// any `.bak` left by a previous save (single-generation backup, not
// accumulated history - see saveFile()'s own header comment).
[[nodiscard]] std::optional<SaveError> replaceIntoPlace(const fs::path& path, const fs::path& tempPath,
                                                         const fs::path& backupPath,
                                                         const fs::path& keptBackupPath,
                                                         bool             keepBackup) noexcept {
    if (::ReplaceFileW(path.c_str(), tempPath.c_str(), backupPath.c_str(), 0, nullptr, nullptr)) {
        std::error_code ec;
        if (keepBackup) {
            fs::rename(backupPath, keptBackupPath, ec);  // best-effort; see this function's own comment
        } else {
            fs::remove(backupPath, ec);  // best-effort; a leftover .neomifes-bak is not data loss
        }
        return std::nullopt;
    }

    std::error_code existsEc;
    if (fs::exists(path, existsEc)) {
        // ReplaceFileW's internal rename-to-backup step never completed -
        // path still holds its original content untouched (verified via
        // probe). No further recovery needed or attempted.
        return SaveError::ReplaceFailed;
    }

    if (fs::exists(backupPath, existsEc)) {
        // Rare: path is gone but the pre-save content survived under the
        // backup name. Restore it so the user's file isn't missing.
        if (::MoveFileExW(backupPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            return SaveError::ReplaceFailed;  // save failed, but original is safe again at `path`
        }
        return SaveError::OriginalFileAtRisk;  // couldn't even restore - surface loudly
    }

    // No backup either - path genuinely didn't exist before this call
    // (brand-new file / Save As to a fresh path). ReplaceFileW cannot
    // create a target that doesn't already exist, so fall back to a plain
    // move (verified via probe).
    if (::MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return std::nullopt;
    }
    return SaveError::ReplaceFailed;  // tempPath still holds the new content, nothing lost
}

}  // namespace

std::optional<SaveError> saveFile(Document& doc, const fs::path& path, encoding::Encoding enc,
                                  encoding::LineEnding lineEnding, bool writeBom, bool keepBackup,
                                  bool markAsSaved) {
    const auto snap = doc.snapshot();

    fs::path tempPath = path;
    tempPath += L".neomifes-tmp";
    fs::path backupPath = path;
    backupPath += L".neomifes-bak";
    fs::path keptBackupPath = path;
    keptBackupPath += L".bak";

    platform::FileHandle tempFile{::CreateFileW(tempPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                                CREATE_ALWAYS,  // overwrite any stale temp from a crashed save
                                                FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!tempFile) {
        return SaveError::CannotCreateTempFile;
    }

    if (const auto err = writeChunks(tempFile.get(), doc, *snap, enc, lineEnding, writeBom)) {
        tempFile.reset();  // must close before removing - opened without FILE_SHARE_DELETE
        std::error_code ec;
        fs::remove(tempPath, ec);  // best-effort cleanup of the partial temp file
        return err;
    }

    if (!::FlushFileBuffers(tempFile.get())) {
        tempFile.reset();
        std::error_code ec;
        fs::remove(tempPath, ec);
        return SaveError::WriteFailed;
    }
    tempFile.reset();  // close before ReplaceFileW/MoveFileExW touch tempPath

    if (const auto err = replaceIntoPlace(path, tempPath, backupPath, keptBackupPath, keepBackup)) {
        return err;
    }
    if (markAsSaved) {
        doc.markSaved();
    }
    return std::nullopt;
}

}  // namespace neomifes::document
