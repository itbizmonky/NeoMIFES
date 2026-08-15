#pragma once

// fnv1aHash64 (WI-11) - a deterministic, filesystem-safe identifier
// generator for autosave file naming (%APPDATA%\NeoMIFES\autosave\<hex hash
// of the document's canonical path>.tmp, see app::autosaveHashFor()).
//
// NOT a security hash - collision resistance against an adversary is
// irrelevant here (this only needs to avoid accidental collisions across
// however many files one user has open). Deliberately NOT std::hash<T>:
// the C++ standard does not guarantee std::hash's output is stable ACROSS
// SEPARATE PROCESS RUNS, and that stability is load-bearing here - crash
// recovery must recompute the exact same hash for the same path in a LATER
// process invocation to find the autosave file a crashed earlier run wrote.
// FNV-1a is simple, well-known, and (as implemented below, with no per-run
// seed) trivially deterministic across runs.

#include <cstdint>
#include <string_view>

namespace neomifes::util {

// FNV-1a 64-bit over the raw UTF-16 code units of `text` (each char16_t
// contributes its low byte then its high byte, independent of host
// endianness - the byte order here is an internal implementation detail of
// this hash, not tied to any on-disk/wire format).
[[nodiscard]] constexpr std::uint64_t fnv1aHash64(std::u16string_view text) noexcept {
    constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime       = 0x100000001b3ULL;

    std::uint64_t hash = kOffsetBasis;
    for (const char16_t ch : text) {
        for (int shift = 0; shift < 16; shift += 8) {
            const auto byte = static_cast<std::uint8_t>((static_cast<std::uint16_t>(ch) >> shift) & 0xFFU);
            hash ^= byte;
            hash *= kPrime;
        }
    }
    return hash;
}

}  // namespace neomifes::util
