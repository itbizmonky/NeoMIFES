#pragma once

// OriginalBuffer - read-only UTF-16 storage for the initial file contents.
//
// Phase 2a MVP: the entire file is decoded once at load time and held in a
// std::u16string. This is correct but breaks the 20MB target for 10GB files.
//
// Phase 2b will replace the implementation with a memory-mapped byte buffer
// plus per-chunk lazy decode caches (see detailed_design.md sec.3.1 "Lazy
// Decode" and docs/issues/lazy_decode_mmap.md). The public API below is
// deliberately shaped so callers stay unchanged across that swap.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace neomifes::document {

class OriginalBuffer {
public:
    OriginalBuffer() = default;
    explicit OriginalBuffer(std::u16string decoded) noexcept
        : m_decoded(std::move(decoded)) {}

    // Returns a view over UTF-16 code units at [offset, offset+length).
    // In the eventual Lazy Decode implementation this will trigger on-demand
    // decoding of the containing chunk and pin the returned view for the
    // caller's lifetime.
    [[nodiscard]] std::u16string_view view(std::uint64_t offset,
                                           std::uint64_t length) const noexcept;

    [[nodiscard]] std::uint64_t size() const noexcept { return m_decoded.size(); }

    // Factory helper for tests / simple callers.
    [[nodiscard]] static std::shared_ptr<const OriginalBuffer>
        fromU16String(std::u16string s);

private:
    std::u16string m_decoded;
};

}  // namespace neomifes::document
