#pragma once

// AddBuffer - append-only UTF-16 storage for all inserted text.
// Every edit appends; nothing is ever mutated in place. Existing pieces that
// point into this buffer remain valid forever, which is what makes snapshots
// cheap (they only need to copy the piece list).

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace neomifes::document {

class AddBuffer {
public:
    // Appends `text` and returns the UTF-16 CU offset where it landed.
    // Never invalidates existing offsets returned from previous append() calls
    // (std::vector reserves capacity in chunks; pieces store offsets, not
    // pointers, so vector growth is safe).
    std::uint64_t append(std::u16string_view text);

    // Returns a view of the appended range. Behaviour is undefined if the range
    // was never actually appended.
    [[nodiscard]] std::u16string_view view(std::uint64_t offset,
                                           std::uint64_t length) const noexcept;

    [[nodiscard]] std::uint64_t size() const noexcept { return m_data.size(); }

    // Reserve to reduce reallocation cost when the caller expects many small edits.
    void reserve(std::size_t codeUnits) { m_data.reserve(codeUnits); }

private:
    std::vector<char16_t> m_data;
};

}  // namespace neomifes::document
