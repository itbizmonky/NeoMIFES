#pragma once

// Encoding Engine core - decode/encode/BOM-detection. Phase 6a implemented
// the Unicode transformation formats; Phase 6b1 added Shift-JIS/EUC-JP.
// ISO-2022-JP is deferred to Phase 6b2 - its ESC-sequence framing and
// WC_ERR_INVALID_CHARS's documented incompatibility with the ISO-2022 code
// pages (50220/50221/50222) make it a different shape of problem from
// Shift-JIS/EUC-JP, and only Shift-JIS is explicitly required by this
// project's personas (master_roadmap.md's P1 SAP consultant persona).
//
// Deliberately independent of neomifes::document/core - this is a pure
// bytes<->UTF-16 codec module, headlessly testable with no Document/UI
// dependency, mirroring how search::GrepService (Phase 5c1) and
// util::parseTagJumpReference (Phase 5c4) were built standalone before any
// later phase wired them into main.cpp. document::loadUtf8File()/
// document::OriginalBuffer's mmap+lazy-decode integration is a separate,
// later sub-phase (6d+) - see the Phase 6a plan's scope notes for why
// integrating multi-encoding support into OriginalBuffer's existing
// UTF-8-specific streaming/checkpoint machinery is deliberately deferred.
//
// This is a standalone codec, not a reuse of
// neomifes::util::toUtf8WithOffsets() (Phase 5a): that function only
// encodes (UTF-16->UTF-8) and always builds a byte-offset<->UTF-16-offset
// table for RE2 search consumers - overhead this module doesn't need and a
// decode direction it doesn't have. This mirrors the project's existing
// precedent of independent UTF-8 implementations for different needs
// (document::OriginalBuffer's internal scanUtf8() is a third, separate one).
//
// Shift-JIS/EUC-JP (Phase 6b1) are not implemented as a hand-rolled JIS
// X 0208 mapping table: CLAUDE.md rule 3 (no speculative implementation)
// rules out transcribing a several-thousand-entry table from memory, since
// a transcription error would only surface on a real-world file. Instead
// they're thin wrappers around Win32's own NLS codepage conversion
// (neomifes::platform::convertToUtf16/convertFromUtf16, codepage_convert.h)
// - the authoritative, Microsoft-maintained source, and not a "new
// dependency" in the CLAUDE.md sense given this project is Win32-native
// throughout (basic_design.md's architecture already assumes Win32/
// Direct2D/DirectWrite).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neomifes::encoding {

enum class Encoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16LeBom,
    Utf16Be,
    Utf16BeBom,
    Utf32Le,
    Utf32LeBom,
    Utf32Be,
    Utf32BeBom,
    ShiftJis,
    EucJp,
    // Iso2022Jp added in Phase 6b2.
};

enum class DecodeError {
    InvalidSequence,    // malformed/overlong/out-of-range code point, an
                        // unpaired surrogate, a byte sequence not valid for
                        // the requested legacy code page, or a *Bom encoding
                        // requested against bytes that don't actually start
                        // with that BOM
    TruncatedSequence,  // a multi-byte/multi-unit sequence cut off at the end
                        // of the input
};

// A code point in `text` has no representation in the requested `encoding`
// (e.g. an emoji encoded as ShiftJis/EucJp - JIS X 0208 predates Unicode's
// astral plane) - or is only reachable via a lossy "best fit" approximation,
// which is rejected for the same reason as DecodeError::InvalidSequence
// (encode() does not silently substitute a different character).
enum class EncodeError { UnmappableCharacter };

// Decodes `bytes` as `encoding` into UTF-16 (this project's internal string
// type, CLAUDE.md sec.4). If `encoding` is a *Bom variant, the matching BOM
// is verified and skipped automatically - callers do not pre-strip it
// themselves (pass the same `bytes` a prior detectBom() call inspected,
// together with the Encoding it returned). No lenient "replace and
// continue" mode - the first invalid/truncated sequence fails the whole
// decode, mirroring ui::parseGotoLineInput()/util::parseTagJumpReference()'s
// existing "reject ambiguous input" convention rather than guessing.
[[nodiscard]] std::variant<std::u16string, DecodeError> decode(std::span<const std::byte> bytes,
                                                                Encoding                    encoding);

// Encodes `text` as `encoding`, prepending the matching BOM bytes first if
// `encoding` is a *Bom variant. The Unicode transformation formats (Phase
// 6a) can represent any UTF-16 string and so never actually produce
// EncodeError, but the return type is uniformly fallible because
// ShiftJis/EucJp (Phase 6b1) are not total functions - see EncodeError.
[[nodiscard]] std::variant<std::vector<std::byte>, EncodeError> encode(std::u16string_view text,
                                                                        Encoding             encoding);

struct BomDetection {
    Encoding    encoding;    // the matching *Bom Encoding value
    std::size_t bomLength;   // bytes to skip before the actual content starts

    friend constexpr bool operator==(const BomDetection&, const BomDetection&) = default;
};

// Inspects up to the first 4 bytes of `bytes` for a recognized BOM (UTF-8,
// UTF-16 LE/BE, UTF-32 LE/BE). Returns nullopt if none matches - the caller
// then falls back to a different detection strategy (Phase 6c) or a default
// encoding. The returned Encoding can be passed straight into decode()
// together with the SAME (BOM-included) `bytes` - decode() does its own
// skip, so callers never need to compute a byte offset themselves.
[[nodiscard]] std::optional<BomDetection> detectBom(std::span<const std::byte> bytes) noexcept;

// Best-effort encoding detection for BOM-less input (Phase 6c1): tries
// detectBom() first, then UTF-8 validity, then disambiguates Shift-JIS vs
// EUC-JP by re-using decode()'s existing strict validation as the oracle
// (no separate byte-range parser - see encoding.cpp's detectEncoding() for
// why this is exact, not approximate, for the Shift-JIS/EUC-JP case).
// Returns nullopt when genuinely ambiguous (`head` decodes successfully as
// both Shift-JIS and EUC-JP - a real, reachable case, e.g. two bytes that
// are simultaneously valid half-width-katakana-pair Shift-JIS and a valid
// EUC-JP double-byte character) or when it matches neither - the caller
// falls back to a default encoding rather than being given a guess.
// ISO-2022-JP detection is deliberately not implemented (Phase 6b2 is
// deferred - see master_roadmap.md §6 for why), and the roadmap's N-gram
// confidence stage for genuinely ambiguous input is out of scope.
[[nodiscard]] std::optional<Encoding> detectEncoding(std::span<const std::byte> head) noexcept;

}  // namespace neomifes::encoding
