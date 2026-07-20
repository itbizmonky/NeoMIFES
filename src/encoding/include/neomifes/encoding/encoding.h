#pragma once

// Encoding Engine core (Phase 6a) - decode/encode/BOM-detection for the
// Unicode transformation formats. Shift-JIS/EUC-JP/ISO-2022-JP land in
// Phase 6b (their enumerators are added then, not reserved here - no
// enumerator without an implementation, CLAUDE.md rule 3).
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
    // ShiftJis / EucJp / Iso2022Jp added in Phase 6b.
};

enum class DecodeError {
    InvalidSequence,    // malformed/overlong/out-of-range code point, an
                        // unpaired surrogate, or a *Bom encoding requested
                        // against bytes that don't actually start with that BOM
    TruncatedSequence,  // a multi-byte/multi-unit sequence cut off at the end
                        // of the input
};

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
// `encoding` is a *Bom variant.
[[nodiscard]] std::vector<std::byte> encode(std::u16string_view text, Encoding encoding);

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

}  // namespace neomifes::encoding
