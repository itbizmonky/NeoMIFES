#pragma once

// FileLoader - reads a file from disk into a Document.
//
// Phase 2a MVP: UTF-8 only (with optional BOM). The full Encoding Engine
// (UTF-16 LE/BE, Shift-JIS, EUC-JP, ISO-2022-JP, auto-detect) lands in Phase 6
// per CLAUDE.md sec.7. Loading is synchronous; async loading via a worker is a
// Phase 2b concern.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

namespace neomifes::document {

class Document;

enum class LoadError {
    NotFound,
    PermissionDenied,
    IoFailure,
    InvalidUtf8,
    TooLarge,
    Unknown,
};

struct LoadResult {
    std::unique_ptr<Document> document;
    bool                      hadBom     = false;
    std::uint64_t             byteLength = 0;
};

// Loads `path` as a UTF-8 file. Returns either a Document or a LoadError.
// `maxBytes` caps the accepted file size (default 512 MiB) so an accidental
// binary open cannot exhaust memory in the MVP path.
[[nodiscard]] std::variant<LoadResult, LoadError>
loadUtf8File(const std::filesystem::path& path,
             std::uint64_t                maxBytes = 512ULL * 1024ULL * 1024ULL);

}  // namespace neomifes::document
