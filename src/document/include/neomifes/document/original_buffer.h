#pragma once

// OriginalBuffer - read-only storage for a document's initial (on-disk)
// content.
//
// Two backing modes:
//   - In-memory (fromU16String): wraps an already-decoded std::u16string
//     directly. Used by tests and any programmatic document creation that
//     doesn't come from a file on disk.
//   - Memory-mapped (openMemoryMapped): the real file-loading path. The
//     file's raw bytes are mapped via platform::FileMapping (OS-paged; does
//     not itself consume process working set beyond touched pages) and
//     UTF-16 is decoded ON DEMAND, range by range, the first time each
//     range is requested. Decoded ranges are cached for the lifetime of the
//     OriginalBuffer and never evicted - see docs/issues/lazy_decode_mmap.md
//     for why a full LRU-with-eviction was deliberately NOT implemented
//     (returning a raw std::u16string_view from view() means any evicted
//     entry would leave outstanding views dangling; avoiding that safely
//     requires refcounted cache entries, a bigger redesign deferred until
//     real memory pressure at full-document-scroll is actually measured).
//
// Neither mode requires the caller to decode content up front just to learn
// the buffer's own length or newline count - both are computed once, at
// construction, via a single streaming pass over the RAW BYTES that never
// materialises the full decoded content. This is what lets opening a large
// file avoid doubling/tripling memory just to report its own size (Phase 2a
// materialised the whole file as UTF-16 in the constructor path; Phase 2b3
// removes that).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "neomifes/platform/file_mapping.h"

namespace neomifes::document {

enum class OriginalBufferError {
    NotFound,
    PermissionDenied,
    IoFailure,
    EmptyFile,
    InvalidUtf8,
};

class OriginalBuffer {
public:
    // In-memory factory: wraps `s` directly, no decoding, no mmap. Used by
    // tests and non-file document sources.
    [[nodiscard]] static std::shared_ptr<const OriginalBuffer>
        fromU16String(std::u16string s);

    // Real file factory: mmaps `path` and validates + scans its UTF-8
    // content starting at byte offset `byteOffset` (callers skip a BOM by
    // passing 3) once, to compute length / newline-count / a checkpoint
    // index. Does not decode or retain any UTF-16 content at this point.
    [[nodiscard]] static std::variant<std::shared_ptr<const OriginalBuffer>, OriginalBufferError>
        openMemoryMapped(const std::filesystem::path& path, std::uint64_t byteOffset);

    // Returns a UTF-16 view of [offset, offset+length) in CU (UTF-16 code
    // unit) space. In MemoryMapped mode this decodes on first request for a
    // given (offset, length) pair; subsequent requests for the exact same
    // range are served from cache. The returned view stays valid for the
    // lifetime of this OriginalBuffer.
    //
    // Not noexcept: the MemoryMapped decode path allocates (a fresh
    // std::u16string per cache miss, a cache-map node); a genuine
    // std::bad_alloc is allowed to propagate rather than being swallowed by
    // a blanket catch (CLAUDE.md forbids unconditional catch(...)). Callers
    // that need a noexcept boundary should establish one explicitly rather
    // than relying on this function to provide it silently.
    [[nodiscard]] std::u16string_view view(std::uint64_t offset, std::uint64_t length) const;

    [[nodiscard]] std::uint64_t size() const noexcept { return m_totalCuLength; }
    [[nodiscard]] std::uint32_t newlineCount() const noexcept { return m_totalNewlines; }

private:
    OriginalBuffer() = default;

    enum class Kind : std::uint8_t { InMemory, MemoryMapped };

    // CU offset reached after decoding up to `byteOffset` bytes of UTF-8
    // content (measured from the content start, i.e. past any BOM).
    // Recorded only at complete code-point boundaries, roughly every
    // kCheckpointBytes bytes (never exactly, since we never split a
    // multi-byte sequence to hit an exact byte count).
    struct Checkpoint {
        std::uint64_t byteOffset;
        std::uint64_t cuOffset;
    };

    // Streaming UTF-8 validation + metrics pass over `bytes`. Computes total
    // CU length, total newline count, and a checkpoint index spaced
    // approximately every `checkpointBytes` bytes. Returns false on invalid
    // UTF-8 (unpaired surrogate, overlong/out-of-range code point, malformed
    // continuation byte, or a sequence truncated at EOF).
    [[nodiscard]] static bool scanUtf8(std::span<const std::byte> bytes,
                                       std::uint64_t& totalCu,
                                       std::uint32_t& totalNewlines,
                                       std::vector<Checkpoint>& checkpoints,
                                       std::uint64_t checkpointBytes);

    // Decodes the memory-mapped content for [offset, offset+length), using
    // the checkpoint index to avoid re-decoding from byte 0 every time, and
    // caches the result. Precondition: bounds already validated by view().
    [[nodiscard]] std::u16string_view viewMemoryMapped(std::uint64_t offset,
                                                        std::uint64_t length) const;

    Kind m_kind = Kind::InMemory;

    // -- InMemory backing --
    std::u16string m_inMemory;

    // -- MemoryMapped backing --
    platform::FileMapping m_mapping;
    std::uint64_t         m_byteOffset    = 0;  // where UTF-8 content starts (past any BOM)
    std::uint64_t         m_totalCuLength = 0;
    std::uint32_t         m_totalNewlines = 0;

    static constexpr std::uint64_t kCheckpointBytes = 64 * 1024;
    std::vector<Checkpoint> m_checkpoints;

    mutable std::mutex m_decodeCacheMutex;
    // Keyed by (offset, length); entries are never evicted (see class
    // comment). std::map avoids needing a custom hash for the pair key;
    // lookups are not on a hot path (edits/inserts go through AddBuffer,
    // which never touches this cache).
    mutable std::map<std::pair<std::uint64_t, std::uint64_t>, std::unique_ptr<std::u16string>>
        m_decodeCache;
};

}  // namespace neomifes::document
