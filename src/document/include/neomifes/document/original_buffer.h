#pragma once

// OriginalBuffer - read-only storage for a document's initial (on-disk)
// content.
//
// Two backing modes:
//   - In-memory (fromU16String): wraps an already-decoded std::u16string
//     directly. Used by tests, any programmatic document creation that
//     doesn't come from a file on disk, AND (Phase 6d) the eager-decode
//     path for encodings that don't get mmap + Lazy Decode (see below).
//   - Memory-mapped (openMemoryMapped): the real file-loading path for
//     encodings whose byte-to-character-boundary structure is simple enough
//     to scan without a character-set table: UTF-8, UTF-16 LE/BE, UTF-32
//     LE/BE (Phase 6d generalized this from a UTF-8-only implementation,
//     Phase 2b3). The file's raw bytes are mapped via platform::FileMapping
//     (OS-paged; does not itself consume process working set beyond touched
//     pages) and UTF-16 is decoded ON DEMAND, range by range, the first
//     time each range is requested. Decoded ranges are cached for the
//     lifetime of the OriginalBuffer and never evicted - see
//     docs/issues/lazy_decode_mmap.md for why a full LRU-with-eviction was
//     deliberately NOT implemented (returning a raw std::u16string_view
//     from view() means any evicted entry would leave outstanding views
//     dangling; avoiding that safely requires refcounted cache entries, a
//     bigger redesign deferred until real memory pressure at full-document-
//     scroll is actually measured).
//
//     Shift-JIS/EUC-JP/ISO-2022-JP deliberately do NOT get this treatment
//     (openMemoryMapped() never receives these Encoding values - callers
//     route them to fromU16String() + neomifes::encoding::decode() instead,
//     see file_loader.cpp's loadFile()). ISO-2022-JP in particular is
//     stateful (its escape sequences switch decode mode), so resuming a
//     decode from an arbitrary checkpoint would need the checkpoint to also
//     record which mode was active there - a materially harder problem
//     deferred until a real need for multi-GB legacy-Japanese files
//     appears (none of this project's personas have one; see the Phase 6d
//     plan's "design decision 1").
//
// Neither mode requires the caller to decode content up front just to learn
// the buffer's own length or newline count - both are computed once, at
// construction, via a single streaming pass over the RAW BYTES that never
// materialises the full decoded content. This is what lets opening a large
// file avoid doubling/tripling memory just to report its own size (Phase 2a
// materialised the whole file as UTF-16 in the constructor path; Phase 2b3
// removes that for UTF-8, Phase 6d for UTF-16/UTF-32).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "neomifes/encoding/encoding.h"
#include "neomifes/platform/file_mapping.h"

namespace neomifes::document {

enum class OriginalBufferError {
    NotFound,
    PermissionDenied,
    IoFailure,
    EmptyFile,
    InvalidEncoding,
};

class OriginalBuffer {
public:
    // In-memory factory: wraps `s` directly, no decoding, no mmap. Used by
    // tests and non-file document sources.
    [[nodiscard]] static std::shared_ptr<const OriginalBuffer>
        fromU16String(std::u16string s);

    // Real file factory: mmaps `path` and validates + scans its content
    // (interpreted as `encoding`) starting at byte offset `byteOffset`
    // (callers skip a BOM by passing its length - see
    // neomifes::encoding::detectBom()) once, to compute length /
    // newline-count / (for UTF-8 and UTF-32) a checkpoint index. Does not
    // decode or retain any UTF-16 content at this point.
    //
    // `encoding` must be one of Utf8, Utf16Le, Utf16Be, Utf32Le, Utf32Be -
    // the encodings whose byte-to-character-boundary structure is knowable
    // without a character-set table (see class comment). Passing
    // ShiftJis/EucJp/Iso2022Jp is a caller contract violation (there is no
    // legacy-codepage scan path here); callers route those through
    // fromU16String() + neomifes::encoding::decode() instead, see
    // file_loader.cpp's loadFile().
    [[nodiscard]] static std::variant<std::shared_ptr<const OriginalBuffer>, OriginalBufferError>
        openMemoryMapped(const std::filesystem::path& path, std::uint64_t byteOffset,
                          encoding::Encoding encoding);

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

    // Which scan/decode algorithm MemoryMapped mode uses - derived once
    // from the Encoding passed to openMemoryMapped() and stored so view()
    // can dispatch without re-deriving it. A separate (smaller) enum from
    // encoding::Encoding rather than storing the Encoding itself: this
    // class's own logic only ever branches on byte-structural shape
    // (variable-length-with-lead-byte vs fixed-2-byte vs fixed-4-byte), and
    // LE/BE variants of the same width share one algorithm (see
    // m_bigEndian).
    enum class ScanFamily : std::uint8_t { Utf8, Utf16, Utf32 };

    // Maps a Group-A Encoding (Utf8/Utf16Le/Utf16Be/Utf32Le/Utf32Be) to this
    // class's internal (family, bigEndian) representation. Returns nullopt
    // for any other Encoding (openMemoryMapped()'s caller-contract
    // violation case).
    [[nodiscard]] static std::optional<std::pair<ScanFamily, bool>>
        classifyEncoding(encoding::Encoding encoding) noexcept;

    // CU offset reached after decoding up to `byteOffset` bytes of content
    // (measured from the content start, i.e. past any BOM). Recorded only
    // at complete character boundaries, roughly every kCheckpointBytes
    // bytes (never exactly, since we never split a multi-unit sequence to
    // hit an exact byte count). Used by the Utf8 and Utf32 scan families;
    // Utf16 doesn't need it (see viewMemoryMappedUtf16()'s comment).
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

    // SEH (__try/__except) trampoline around scanUtf8, catching a hardware
    // EXCEPTION_IN_PAGE_ERROR - raised when the OS can't page in mapped
    // content, the case that matters here being a network drive
    // disconnecting while a file from it is still mapped - and reporting it
    // via `pageError` instead of crashing the process. Declared as a private
    // static member (rather than a free function) purely so it can name
    // `Checkpoint` in its signature; MSVC's "no object unwinding in a
    // function using __try" restriction is about this function's OWN locals
    // (there are none needing destruction), not about its being a member.
    [[nodiscard]] static bool scanUtf8Safe(std::span<const std::byte> bytes,
                                           std::uint64_t& totalCu,
                                           std::uint32_t& totalNewlines,
                                           std::vector<Checkpoint>& checkpoints,
                                           std::uint64_t checkpointBytes,
                                           bool& pageError);

    // Streaming UTF-16 validation + metrics pass over `bytes` (`bigEndian`
    // selects byte order). Computes total CU count (always bytes.size()/2 -
    // every source code unit is exactly one output CU, unlike UTF-8/32) and
    // newline count. Returns false on an odd byte count or an unpaired
    // surrogate. No checkpoint index: a UTF-16 source's CU offset is always
    // exactly byteOffset/2, so viewMemoryMappedUtf16() computes the byte
    // range for any requested CU range directly with no scan-and-resume.
    [[nodiscard]] static bool scanUtf16(std::span<const std::byte> bytes, bool bigEndian,
                                        std::uint64_t& totalCu, std::uint32_t& totalNewlines);

    // SEH trampoline around scanUtf16 - see scanUtf8Safe's comment.
    [[nodiscard]] static bool scanUtf16Safe(std::span<const std::byte> bytes, bool bigEndian,
                                            std::uint64_t& totalCu, std::uint32_t& totalNewlines,
                                            bool& pageError);

    // Streaming UTF-32 validation + metrics pass over `bytes` (`bigEndian`
    // selects byte order), building a checkpoint index like scanUtf8 -
    // needed because a non-BMP code point (>U+FFFF) produces 2 output CUs
    // from a single 4-byte unit, so CU offset diverges from byteOffset/4
    // once any appear. Simpler than scanUtf8 otherwise: every unit is a
    // fixed 4 bytes, no variable-length lead-byte parsing. Returns false on
    // a byte count not a multiple of 4, a surrogate-range value, or a code
    // point beyond U+10FFFF.
    [[nodiscard]] static bool scanUtf32(std::span<const std::byte> bytes, bool bigEndian,
                                        std::uint64_t& totalCu, std::uint32_t& totalNewlines,
                                        std::vector<Checkpoint>& checkpoints,
                                        std::uint64_t checkpointBytes);

    // SEH trampoline around scanUtf32 - see scanUtf8Safe's comment.
    [[nodiscard]] static bool scanUtf32Safe(std::span<const std::byte> bytes, bool bigEndian,
                                            std::uint64_t& totalCu, std::uint32_t& totalNewlines,
                                            std::vector<Checkpoint>& checkpoints,
                                            std::uint64_t checkpointBytes, bool& pageError);

    // Dispatches to the per-family view implementation below based on
    // m_scanFamily. Precondition: bounds already validated by view().
    [[nodiscard]] std::u16string_view viewMemoryMapped(std::uint64_t offset,
                                                        std::uint64_t length) const;

    // Decodes the memory-mapped UTF-8 content for [offset, offset+length),
    // using the checkpoint index to avoid re-decoding from byte 0 every
    // time, and caches the result.
    [[nodiscard]] std::u16string_view viewMemoryMappedUtf8(std::uint64_t offset,
                                                            std::uint64_t length) const;

    // Decodes the memory-mapped UTF-16 content for [offset, offset+length).
    // No checkpoint lookup needed (see scanUtf16's comment) - the byte
    // range is computed directly as [m_byteOffset + 2*offset,
    // m_byteOffset + 2*(offset+length)) and byte-swapped if m_bigEndian.
    // Still cached (m_decodeCache), same as the other families, since
    // view() always returns a reference into a cache-owned std::u16string.
    [[nodiscard]] std::u16string_view viewMemoryMappedUtf16(std::uint64_t offset,
                                                             std::uint64_t length) const;

    // Decodes the memory-mapped UTF-32 content for [offset, offset+length),
    // using the checkpoint index the same way viewMemoryMappedUtf8() does.
    [[nodiscard]] std::u16string_view viewMemoryMappedUtf32(std::uint64_t offset,
                                                             std::uint64_t length) const;

    Kind       m_kind       = Kind::InMemory;
    ScanFamily m_scanFamily = ScanFamily::Utf8;
    bool       m_bigEndian  = false;

    // -- InMemory backing --
    std::u16string m_inMemory;

    // -- MemoryMapped backing --
    platform::FileMapping m_mapping;
    std::uint64_t         m_byteOffset    = 0;  // where content starts (past any BOM)
    std::uint64_t         m_totalCuLength = 0;
    std::uint32_t         m_totalNewlines = 0;

    static constexpr std::uint64_t kCheckpointBytes = 64 * 1024;
    std::vector<Checkpoint> m_checkpoints;  // unused (empty) when m_scanFamily == Utf16

    mutable std::mutex m_decodeCacheMutex;
    // Keyed by (offset, length); entries are never evicted (see class
    // comment). std::map avoids needing a custom hash for the pair key;
    // lookups are not on a hot path (edits/inserts go through AddBuffer,
    // which never touches this cache).
    mutable std::map<std::pair<std::uint64_t, std::uint64_t>, std::unique_ptr<std::u16string>>
        m_decodeCache;
};

}  // namespace neomifes::document
