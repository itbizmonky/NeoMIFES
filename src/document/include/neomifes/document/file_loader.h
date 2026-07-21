#pragma once

// FileLoader - reads a file from disk into a Document.
//
// Phase 2a MVP: UTF-8 only (with optional BOM). Phase 6a-6b2 built the full
// Encoding Engine (neomifes::encoding: UTF-8/16/32 families, Shift-JIS,
// EUC-JP, ISO-2022-JP, decode()/encode()/detectBom()/detectEncoding()).
// Phase 6d wires it into this module via loadFile() - see its doc comment.
// loadUtf8File() itself is UNCHANGED (still UTF-8-only, same error
// taxonomy): search::GrepService depends on its exact existing contract
// (see grep_service.cpp), and a directory-crawling grep deliberately stays
// on the simple, fast "assume UTF-8, skip anything that fails" path rather
// than paying auto-detection/legacy-decode cost per candidate file.
// Loading is synchronous; async loading via a worker is a later concern.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

#include "neomifes/encoding/encoding.h"

namespace neomifes::document {

class Document;

enum class LoadError {
    NotFound,
    PermissionDenied,
    IoFailure,
    InvalidUtf8,
    TooLarge,
    Unknown,
    InvalidEncoding,  // Phase 6d: loadFile()-only - content didn't decode
                       // cleanly as its detected (or UTF-8-fallback) encoding.
                       // Kept separate from InvalidUtf8 (loadUtf8File()'s
                       // own, unchanged, UTF-8-specific error) rather than
                       // reused, since reporting "InvalidUtf8" for a file
                       // that failed to decode as Shift-JIS would be
                       // actively misleading.
};

struct LoadResult {
    std::unique_ptr<Document> document;
    bool                      hadBom     = false;
    std::uint64_t             byteLength = 0;
    // The Encoding loadFile() detected/assumed (a *Bom variant if a BOM was
    // found). Always Encoding::Utf8 when produced by loadUtf8File(). Not
    // consumed anywhere yet (no encoding-indicator UI exists) - carried for
    // a future consumer, the same way hadBom/byteLength already were before
    // this phase.
    encoding::Encoding detectedEncoding = encoding::Encoding::Utf8;
};

// Loads `path` as a UTF-8 file. Returns either a Document or a LoadError.
// `maxBytes` caps the accepted file size (default 512 MiB) so an accidental
// binary open cannot exhaust memory in the MVP path.
[[nodiscard]] std::variant<LoadResult, LoadError>
loadUtf8File(const std::filesystem::path& path,
             std::uint64_t                maxBytes = 512ULL * 1024ULL * 1024ULL);

// Auto-detects `path`'s encoding (neomifes::encoding::detectBom() on the
// file's head, then detectEncoding() if no BOM matched, falling back to
// Utf8 if neither matches - the same default loadUtf8File() has always
// implicitly assumed) and loads it as that encoding. Unlike loadUtf8File(),
// this can open any of the 8 encodings neomifes::encoding supports.
//
// `maxBytes` defaults to 16 GiB (10GB target + headroom, mirroring
// tests/bench/document_load_bench.cpp's existing "pad past the smaller
// default" precedent) rather than loadUtf8File()'s 512 MiB - this is the
// entry point the "10GB mmap" target (master_roadmap.md §6) actually flows
// through; main.cpp's --open and app::openDocumentAt() (Phase 5c2) both
// call this with no override, so raising the default here is what makes
// that target reachable through the app at all, not just in a benchmark
// that passes its own larger cap.
//
// ISO-2022-JP auto-detection is NOT implemented (detectEncoding()'s
// existing scope, Phase 6c1/6b2's completion notes) - a plain ISO-2022-JP
// file's bytes are all < 0x80 (7-bit clean by construction, the whole point
// of ISO-2022 being safe over 7-bit transports), so it decodes
// successfully-but-wrongly as UTF-8 (mojibake: visible escape sequences,
// misinterpreted double-byte text) rather than failing. This is a known,
// accepted limitation until detectEncoding() gains ESC-sequence
// recognition (unscoped - see master_roadmap.md §6). Explicitly opening a
// file AS Iso2022Jp still works correctly (neomifes::encoding::decode()
// handles it); there is just no automatic trigger for it yet, matching how
// this codebase has no "open with encoding" menu at all (no menu bar
// exists).
[[nodiscard]] std::variant<LoadResult, LoadError>
loadFile(const std::filesystem::path& path,
         std::uint64_t                maxBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL);

}  // namespace neomifes::document
