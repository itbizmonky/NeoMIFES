#include "neomifes/document/original_buffer.h"

#include <algorithm>
#include <cassert>

namespace neomifes::document {

// ---------------------------------------------------------------------------
// UTF-8 decode primitive shared by the initial scan and on-demand view()
// decode. Decodes starting at bytes[startByte]; if `out` is non-null,
// decoded code units are appended to it, otherwise they are counted but
// discarded (used for the "skip forward to a target CU offset" phase of
// view()). Stops before producing more than `maxCodeUnits` code units (never
// overshoots, even across a 2-CU surrogate pair) or at end of `bytes`,
// whichever comes first. Returns the number of bytes consumed.
//
// Precondition: `startByte` does not fall inside a multi-byte UTF-8
// sequence. This holds by construction: checkpoints are only ever recorded
// right after a complete code point finishes (see scanUtf8), and view()
// always resumes decoding from either byte 0 or a checkpoint.
// ---------------------------------------------------------------------------

namespace {

// A tight, already-reviewed bit-level UTF-8 decode loop; splitting it would
// scatter the surrogate-pair/overshoot invariants documented above across
// functions rather than reduce actual complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::uint64_t decodeUtf8Run(std::span<const std::byte> bytes,
                            std::uint64_t startByte,
                            std::uint64_t maxCodeUnits,
                            std::u16string* out,
                            std::uint64_t& codeUnitsProduced) noexcept {
    codeUnitsProduced = 0;
    std::uint64_t i = startByte;

    while (i < bytes.size()) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        std::uint32_t cp    = 0;
        std::size_t   extra = 0;

        if (b0 < 0x80) {
            cp = b0; extra = 0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F; extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F; extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07; extra = 3;
        } else {
            break;  // invalid lead byte
        }
        if (i + extra >= bytes.size()) {
            break;  // truncated sequence at EOF
        }

        bool valid = true;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto bk = static_cast<unsigned char>(bytes[i + k]);
            if ((bk & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (bk & 0x3F);
        }
        if (!valid) {
            break;
        }

        const std::uint32_t cuThisChar = (cp > 0xFFFF) ? 2U : 1U;
        if (codeUnitsProduced + cuThisChar > maxCodeUnits) {
            break;  // would overshoot the requested count - stop before it
        }

        if (out != nullptr) {
            if (cp <= 0xFFFF) {
                out->push_back(static_cast<char16_t>(cp));
            } else {
                const std::uint32_t adj = cp - 0x10000;
                out->push_back(static_cast<char16_t>(0xD800 + (adj >> 10)));
                out->push_back(static_cast<char16_t>(0xDC00 + (adj & 0x3FF)));
            }
        }
        codeUnitsProduced += cuThisChar;
        i += extra + 1;

        if (codeUnitsProduced == maxCodeUnits) {
            break;
        }
    }
    return i - startByte;
}

// SEH trampoline around decodeUtf8Run. Must have no local C++ objects with
// non-trivial destructors (MSVC restriction on mixing __try/__except with
// object unwinding) - `out` is a pointer the CALLER owns, not a local
// object here, so that's fine. See scanUtf8Safe's declaration comment (in
// the header) for the general rationale; this is the same mechanism
// applied to the on-demand decode path exercised by every view() call, not
// just the one-time initial scan.
std::uint64_t decodeUtf8RunSafe(std::span<const std::byte> bytes,
                                std::uint64_t startByte,
                                std::uint64_t maxCodeUnits,
                                std::u16string* out,
                                std::uint64_t& codeUnitsProduced,
                                bool& pageError) noexcept {
    pageError = false;
    __try {
        return decodeUtf8Run(bytes, startByte, maxCodeUnits, out, codeUnitsProduced);
    } __except (::GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        pageError = true;
        codeUnitsProduced = 0;
        return 0;
    }
}

// ---------------------------------------------------------------------------
// UTF-16 decode primitive (Phase 6d). Unlike UTF-8, every source code unit
// is exactly 2 bytes and maps to exactly one output CU (a surrogate pair is
// just two consecutive already-well-formed CUs) - no lead-byte parsing, no
// CU-count/byte-count divergence, so this has no "maxCodeUnits overshoot"
// concern the way decodeUtf8Run does. Reads exactly `byteCount` bytes
// starting at `startByte` (always even - callers only ever pass 2*N).
// ---------------------------------------------------------------------------

void decodeUtf16Run(std::span<const std::byte> bytes, bool bigEndian,
                    std::uint64_t startByte, std::uint64_t byteCount,
                    std::u16string& out) noexcept {
    for (std::uint64_t i = 0; i < byteCount; i += 2) {
        const auto b0 = static_cast<unsigned char>(bytes[startByte + i]);
        const auto b1 = static_cast<unsigned char>(bytes[startByte + i + 1]);
        out.push_back(bigEndian ? static_cast<char16_t>((b0 << 8) | b1)
                                 : static_cast<char16_t>((b1 << 8) | b0));
    }
}

// SEH trampoline around decodeUtf16Run - see decodeUtf8RunSafe's comment.
void decodeUtf16RunSafe(std::span<const std::byte> bytes, bool bigEndian,
                        std::uint64_t startByte, std::uint64_t byteCount,
                        std::u16string* out, bool& pageError) noexcept {
    pageError = false;
    __try {
        decodeUtf16Run(bytes, bigEndian, startByte, byteCount, *out);
    } __except (::GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        pageError = true;
    }
}

// ---------------------------------------------------------------------------
// UTF-32 decode primitive (Phase 6d). Every source unit is a fixed 4 bytes
// (no lead-byte parsing like UTF-8), but a non-BMP code point (>U+FFFF)
// still produces 2 output CUs from that one unit, so the "stop before
// overshooting maxCodeUnits" logic from decodeUtf8Run is still needed here.
// ---------------------------------------------------------------------------

std::uint64_t decodeUtf32Run(std::span<const std::byte> bytes, bool bigEndian,
                             std::uint64_t startByte, std::uint64_t maxCodeUnits,
                             std::u16string* out, std::uint64_t& codeUnitsProduced) noexcept {
    codeUnitsProduced = 0;
    std::uint64_t i = startByte;

    while (i + 4 <= bytes.size()) {
        const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i]));
        const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1]));
        const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 2]));
        const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 3]));
        const std::uint32_t cp = bigEndian
            ? (b0 << 24) | (b1 << 16) | (b2 << 8) | b3
            : (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;

        const std::uint32_t cuThisChar = (cp > 0xFFFF) ? 2U : 1U;
        if (codeUnitsProduced + cuThisChar > maxCodeUnits) {
            break;
        }

        if (out != nullptr) {
            if (cp <= 0xFFFF) {
                out->push_back(static_cast<char16_t>(cp));
            } else {
                const std::uint32_t adj = cp - 0x10000;
                out->push_back(static_cast<char16_t>(0xD800 + (adj >> 10)));
                out->push_back(static_cast<char16_t>(0xDC00 + (adj & 0x3FF)));
            }
        }
        codeUnitsProduced += cuThisChar;
        i += 4;

        if (codeUnitsProduced == maxCodeUnits) {
            break;
        }
    }
    return i - startByte;
}

// SEH trampoline around decodeUtf32Run - see decodeUtf8RunSafe's comment.
std::uint64_t decodeUtf32RunSafe(std::span<const std::byte> bytes, bool bigEndian,
                                 std::uint64_t startByte, std::uint64_t maxCodeUnits,
                                 std::u16string* out, std::uint64_t& codeUnitsProduced,
                                 bool& pageError) noexcept {
    pageError = false;
    __try {
        return decodeUtf32Run(bytes, bigEndian, startByte, maxCodeUnits, out, codeUnitsProduced);
    } __except (::GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        pageError = true;
        codeUnitsProduced = 0;
        return 0;
    }
}

// Maps a platform::FileMapping open failure to this module's own error type
// - shared by openMemoryMapped()'s mmap step.
[[nodiscard]] OriginalBufferError mapFileMappingError(platform::FileMappingError err) noexcept {
    switch (err) {
        case platform::FileMappingError::NotFound:         return OriginalBufferError::NotFound;
        case platform::FileMappingError::PermissionDenied: return OriginalBufferError::PermissionDenied;
        case platform::FileMappingError::EmptyFile:        return OriginalBufferError::EmptyFile;
        case platform::FileMappingError::IoFailure:
        default:
            return OriginalBufferError::IoFailure;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// scanUtf8Safe - SEH wrapper. See declaration comment in the header.
// ---------------------------------------------------------------------------

bool OriginalBuffer::scanUtf8Safe(std::span<const std::byte> bytes,
                                  std::uint64_t& totalCu,
                                  std::uint32_t& totalNewlines,
                                  std::vector<Checkpoint>& checkpoints,
                                  std::uint64_t checkpointBytes,
                                  bool& pageError) {
    pageError = false;
    __try {
        return scanUtf8(bytes, totalCu, totalNewlines, checkpoints, checkpointBytes);
    } __except (::GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        pageError = true;
        return false;
    }
}

// ---------------------------------------------------------------------------
// scanUtf8 - one-time streaming pass building the checkpoint index.
// ---------------------------------------------------------------------------

bool OriginalBuffer::scanUtf8(std::span<const std::byte> bytes,
                              std::uint64_t& totalCu,
                              std::uint32_t& totalNewlines,
                              std::vector<Checkpoint>& checkpoints,
                              std::uint64_t checkpointBytes) {
    totalCu = 0;
    totalNewlines = 0;
    checkpoints.clear();
    checkpoints.push_back(Checkpoint{.byteOffset = 0, .cuOffset = 0});

    std::uint64_t i = 0;
    std::uint64_t lastCheckpointByte = 0;

    while (i < bytes.size()) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        std::uint32_t cp    = 0;
        std::size_t   extra = 0;

        if (b0 < 0x80) {
            cp = b0; extra = 0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F; extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F; extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07; extra = 3;
        } else {
            return false;
        }
        if (i + extra >= bytes.size()) {
            return false;  // truncated multi-byte sequence at EOF
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto bk = static_cast<unsigned char>(bytes[i + k]);
            if ((bk & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (bk & 0x3F);
        }
        // Reject lone surrogates and out-of-range code points, matching the
        // strictness of the Phase 2a decoder this replaces.
        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            return false;
        }

        totalCu += (cp > 0xFFFF) ? 2 : 1;
        if (cp == u'\n') {
            ++totalNewlines;
        }

        i += extra + 1;

        if (i - lastCheckpointByte >= checkpointBytes) {
            checkpoints.push_back(Checkpoint{.byteOffset = i, .cuOffset = totalCu});
            lastCheckpointByte = i;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// scanUtf16Safe / scanUtf16 (Phase 6d).
// ---------------------------------------------------------------------------

bool OriginalBuffer::scanUtf16Safe(std::span<const std::byte> bytes, bool bigEndian,
                                   std::uint64_t& totalCu, std::uint32_t& totalNewlines,
                                   bool& pageError) {
    pageError = false;
    __try {
        return scanUtf16(bytes, bigEndian, totalCu, totalNewlines);
    } __except (::GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        pageError = true;
        return false;
    }
}

bool OriginalBuffer::scanUtf16(std::span<const std::byte> bytes, bool bigEndian,
                               std::uint64_t& totalCu, std::uint32_t& totalNewlines) {
    totalCu = 0;
    totalNewlines = 0;
    if (bytes.size() % 2 != 0) {
        return false;
    }

    bool pendingHighSurrogate = false;
    for (std::uint64_t i = 0; i < bytes.size(); i += 2) {
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
        const char16_t unit = bigEndian ? static_cast<char16_t>((b0 << 8) | b1)
                                        : static_cast<char16_t>((b1 << 8) | b0);
        const bool isHigh = unit >= 0xD800 && unit <= 0xDBFF;
        const bool isLow  = unit >= 0xDC00 && unit <= 0xDFFF;

        if (pendingHighSurrogate) {
            if (!isLow) {
                return false;  // unpaired high surrogate
            }
            pendingHighSurrogate = false;
        } else {
            if (isLow) {
                return false;  // unpaired low surrogate
            }
            pendingHighSurrogate = isHigh;
        }

        if (unit == u'\n') {
            ++totalNewlines;
        }
        ++totalCu;
    }
    return !pendingHighSurrogate;  // truncated at EOF mid-pair
}

// ---------------------------------------------------------------------------
// scanUtf32Safe / scanUtf32 (Phase 6d).
// ---------------------------------------------------------------------------

bool OriginalBuffer::scanUtf32Safe(std::span<const std::byte> bytes, bool bigEndian,
                                   std::uint64_t& totalCu, std::uint32_t& totalNewlines,
                                   std::vector<Checkpoint>& checkpoints,
                                   std::uint64_t checkpointBytes, bool& pageError) {
    pageError = false;
    __try {
        return scanUtf32(bytes, bigEndian, totalCu, totalNewlines, checkpoints, checkpointBytes);
    } __except (::GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        pageError = true;
        return false;
    }
}

bool OriginalBuffer::scanUtf32(std::span<const std::byte> bytes, bool bigEndian,
                               std::uint64_t& totalCu, std::uint32_t& totalNewlines,
                               std::vector<Checkpoint>& checkpoints, std::uint64_t checkpointBytes) {
    totalCu = 0;
    totalNewlines = 0;
    checkpoints.clear();
    checkpoints.push_back(Checkpoint{.byteOffset = 0, .cuOffset = 0});

    if (bytes.size() % 4 != 0) {
        return false;
    }

    std::uint64_t lastCheckpointByte = 0;
    for (std::uint64_t i = 0; i < bytes.size(); i += 4) {
        const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i]));
        const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1]));
        const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 2]));
        const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 3]));
        const std::uint32_t cp = bigEndian
            ? (b0 << 24) | (b1 << 16) | (b2 << 8) | b3
            : (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;

        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            return false;
        }

        totalCu += (cp > 0xFFFF) ? 2 : 1;
        if (cp == u'\n') {
            ++totalNewlines;
        }

        const std::uint64_t byteAfter = i + 4;
        if (byteAfter - lastCheckpointByte >= checkpointBytes) {
            checkpoints.push_back(Checkpoint{.byteOffset = byteAfter, .cuOffset = totalCu});
            lastCheckpointByte = byteAfter;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// classifyEncoding (Phase 6d) - see declaration comment in the header.
// ---------------------------------------------------------------------------

std::optional<std::pair<OriginalBuffer::ScanFamily, bool>>
OriginalBuffer::classifyEncoding(encoding::Encoding encoding) noexcept {
    switch (encoding) {
        case encoding::Encoding::Utf8:    return std::pair{ScanFamily::Utf8, false};
        case encoding::Encoding::Utf16Le: return std::pair{ScanFamily::Utf16, false};
        case encoding::Encoding::Utf16Be: return std::pair{ScanFamily::Utf16, true};
        case encoding::Encoding::Utf32Le: return std::pair{ScanFamily::Utf32, false};
        case encoding::Encoding::Utf32Be: return std::pair{ScanFamily::Utf32, true};
        default:
            return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

std::shared_ptr<const OriginalBuffer> OriginalBuffer::fromU16String(std::u16string s) {
    auto buf = std::shared_ptr<OriginalBuffer>(new OriginalBuffer());
    buf->m_kind           = Kind::InMemory;
    buf->m_totalCuLength  = s.size();
    for (const char16_t c : s) {
        if (c == u'\n') {
            ++buf->m_totalNewlines;
        }
    }
    buf->m_inMemory = std::move(s);
    return buf;
}

std::variant<std::shared_ptr<const OriginalBuffer>, OriginalBufferError>
OriginalBuffer::openMemoryMapped(const std::filesystem::path& path, std::uint64_t byteOffset,
                                 encoding::Encoding encoding) {
    const auto dispatch = classifyEncoding(encoding);
    if (!dispatch) {
        // Caller contract violation - see this function's header comment
        // (ShiftJis/EucJp/Iso2022Jp never reach here; loadFile() routes
        // them to fromU16String() + neomifes::encoding::decode() instead).
        return OriginalBufferError::InvalidEncoding;
    }
    const auto [scanFamily, bigEndian] = *dispatch;

    auto mapped = platform::FileMapping::open(path);
    if (std::holds_alternative<platform::FileMappingError>(mapped)) {
        return mapFileMappingError(std::get<platform::FileMappingError>(mapped));
    }
    auto& mapping = std::get<platform::FileMapping>(mapped);
    if (byteOffset > mapping.size()) {
        return OriginalBufferError::IoFailure;
    }
    const auto contentBytes = mapping.data().subspan(byteOffset);

    std::uint64_t totalCu = 0;
    std::uint32_t totalNewlines = 0;
    std::vector<Checkpoint> checkpoints;
    bool pageError = false;
    bool scanOk = false;
    switch (scanFamily) {
        case ScanFamily::Utf8:
            scanOk = scanUtf8Safe(contentBytes, totalCu, totalNewlines, checkpoints,
                                  kCheckpointBytes, pageError);
            break;
        case ScanFamily::Utf16:
            scanOk = scanUtf16Safe(contentBytes, bigEndian, totalCu, totalNewlines, pageError);
            break;
        case ScanFamily::Utf32:
            scanOk = scanUtf32Safe(contentBytes, bigEndian, totalCu, totalNewlines, checkpoints,
                                   kCheckpointBytes, pageError);
            break;
    }
    if (pageError) {
        // Hardware page-in failure while scanning - most likely a network
        // drive that dropped mid-read. Surface as a plain IO failure rather
        // than crashing; the caller (FileLoader) already treats this the
        // same as any other unreadable file.
        return OriginalBufferError::IoFailure;
    }
    if (!scanOk) {
        return OriginalBufferError::InvalidEncoding;
    }

    auto buf = std::shared_ptr<OriginalBuffer>(new OriginalBuffer());
    buf->m_kind          = Kind::MemoryMapped;
    buf->m_scanFamily    = scanFamily;
    buf->m_bigEndian     = bigEndian;
    buf->m_mapping       = std::move(mapping);
    buf->m_byteOffset    = byteOffset;
    buf->m_totalCuLength = totalCu;
    buf->m_totalNewlines = totalNewlines;
    buf->m_checkpoints   = std::move(checkpoints);
    return std::shared_ptr<const OriginalBuffer>(std::move(buf));
}

// ---------------------------------------------------------------------------
// view()
// ---------------------------------------------------------------------------

std::u16string_view OriginalBuffer::view(std::uint64_t offset, std::uint64_t length) const {
    if (offset > m_totalCuLength || length > m_totalCuLength - offset || length == 0) {
        return {};
    }
    if (m_kind == Kind::InMemory) {
        return {m_inMemory.data() + offset, static_cast<std::size_t>(length)};
    }
    return viewMemoryMapped(offset, length);
}

std::u16string_view OriginalBuffer::viewMemoryMapped(std::uint64_t offset, std::uint64_t length) const {
    switch (m_scanFamily) {
        case ScanFamily::Utf8:  return viewMemoryMappedUtf8(offset, length);
        case ScanFamily::Utf16: return viewMemoryMappedUtf16(offset, length);
        case ScanFamily::Utf32: return viewMemoryMappedUtf32(offset, length);
    }
    return {};  // unreachable, all ScanFamily enumerators handled above
}

std::u16string_view OriginalBuffer::viewMemoryMappedUtf8(std::uint64_t offset,
                                                          std::uint64_t length) const {
    const auto key = std::make_pair(offset, length);
    {
        const std::lock_guard<std::mutex> lock(m_decodeCacheMutex);
        const auto it = m_decodeCache.find(key);
        if (it != m_decodeCache.end()) {
            return {it->second->data(), it->second->size()};
        }
    }

    // Find the nearest checkpoint at or before `offset` (checkpoints[0] =
    // {0,0} guarantees this is never empty-range). std::ranges::upper_bound's
    // indirect_strict_weak_order constraint would require the comparator to
    // also accept (Checkpoint, Checkpoint) and (Checkpoint, uint64_t), not
    // just the (uint64_t, Checkpoint) direction actually used here; satisfying
    // that would mean adding unused overloads purely to appease the concept.
    // NOLINTNEXTLINE(modernize-use-ranges)
    const auto cpIt = std::upper_bound(
        m_checkpoints.begin(), m_checkpoints.end(), offset,
        [](std::uint64_t cu, const Checkpoint& c) noexcept { return cu < c.cuOffset; });
    const Checkpoint& checkpoint = *(cpIt - 1);

    const auto bytes = m_mapping.data().subspan(m_byteOffset);

    // Skip phase: advance from the checkpoint to the byte position for `offset`.
    std::uint64_t skippedCu = 0;
    bool pageError = false;
    const std::uint64_t skipBytes = decodeUtf8RunSafe(
        bytes, checkpoint.byteOffset, offset - checkpoint.cuOffset, nullptr, skippedCu, pageError);
    if (pageError) {
        // Network drive dropped (or similar) mid-read. Fail gracefully with
        // an empty view rather than crash - matches this function's
        // existing "out of range -> empty view" contract, so callers don't
        // need a new error channel for what is, from their perspective,
        // just "couldn't get this content."
        return {};
    }
    // The skip must reach exactly `offset` - it can only fall short if the
    // checkpoint index or `offset` itself is inconsistent with the content
    // (both are derived from the same scanUtf8 pass, so this should never
    // fire; kept as a cheap Debug-build tripwire for checkpoint arithmetic
    // bugs, which are otherwise hard to catch without a local compiler).
    assert(skippedCu == offset - checkpoint.cuOffset);
    const std::uint64_t decodeStartByte = checkpoint.byteOffset + skipBytes;

    // Decode phase: produce up to `length` CUs into a fresh, cached buffer.
    auto decoded = std::make_unique<std::u16string>();
    decoded->reserve(static_cast<std::size_t>(length));
    std::uint64_t produced = 0;
    decodeUtf8RunSafe(bytes, decodeStartByte, length, decoded.get(), produced, pageError);
    if (pageError) {
        return {};
    }

    const std::lock_guard<std::mutex> lock(m_decodeCacheMutex);
    const auto [insertedIt, inserted] = m_decodeCache.try_emplace(key, std::move(decoded));
    return {insertedIt->second->data(), insertedIt->second->size()};
}

std::u16string_view OriginalBuffer::viewMemoryMappedUtf16(std::uint64_t offset,
                                                           std::uint64_t length) const {
    const auto key = std::make_pair(offset, length);
    {
        const std::lock_guard<std::mutex> lock(m_decodeCacheMutex);
        const auto it = m_decodeCache.find(key);
        if (it != m_decodeCache.end()) {
            return {it->second->data(), it->second->size()};
        }
    }

    // No checkpoint lookup: a UTF-16 source's CU offset is always exactly
    // byteOffset/2 (see scanUtf16's comment), so the byte range for
    // [offset, offset+length) is computable directly.
    const auto bytes = m_mapping.data().subspan(m_byteOffset);
    auto decoded = std::make_unique<std::u16string>();
    decoded->reserve(static_cast<std::size_t>(length));

    bool pageError = false;
    decodeUtf16RunSafe(bytes, m_bigEndian, 2 * offset, 2 * length, decoded.get(), pageError);
    if (pageError) {
        return {};
    }

    const std::lock_guard<std::mutex> lock(m_decodeCacheMutex);
    const auto [insertedIt, inserted] = m_decodeCache.try_emplace(key, std::move(decoded));
    return {insertedIt->second->data(), insertedIt->second->size()};
}

std::u16string_view OriginalBuffer::viewMemoryMappedUtf32(std::uint64_t offset,
                                                           std::uint64_t length) const {
    const auto key = std::make_pair(offset, length);
    {
        const std::lock_guard<std::mutex> lock(m_decodeCacheMutex);
        const auto it = m_decodeCache.find(key);
        if (it != m_decodeCache.end()) {
            return {it->second->data(), it->second->size()};
        }
    }

    // NOLINTNEXTLINE(modernize-use-ranges) - see viewMemoryMappedUtf8's identical comment.
    const auto cpIt = std::upper_bound(
        m_checkpoints.begin(), m_checkpoints.end(), offset,
        [](std::uint64_t cu, const Checkpoint& c) noexcept { return cu < c.cuOffset; });
    const Checkpoint& checkpoint = *(cpIt - 1);

    const auto bytes = m_mapping.data().subspan(m_byteOffset);

    std::uint64_t skippedCu = 0;
    bool pageError = false;
    const std::uint64_t skipBytes = decodeUtf32RunSafe(
        bytes, m_bigEndian, checkpoint.byteOffset, offset - checkpoint.cuOffset, nullptr, skippedCu, pageError);
    if (pageError) {
        return {};
    }
    assert(skippedCu == offset - checkpoint.cuOffset);
    const std::uint64_t decodeStartByte = checkpoint.byteOffset + skipBytes;

    auto decoded = std::make_unique<std::u16string>();
    decoded->reserve(static_cast<std::size_t>(length));
    std::uint64_t produced = 0;
    decodeUtf32RunSafe(bytes, m_bigEndian, decodeStartByte, length, decoded.get(), produced, pageError);
    if (pageError) {
        return {};
    }

    const std::lock_guard<std::mutex> lock(m_decodeCacheMutex);
    const auto [insertedIt, inserted] = m_decodeCache.try_emplace(key, std::move(decoded));
    return {insertedIt->second->data(), insertedIt->second->size()};
}

}  // namespace neomifes::document
