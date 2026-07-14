#pragma once

// Core position / range types used throughout the Document Engine.
// All positions are UTF-16 code-unit offsets from the start of the document
// (surrogate pairs count as 2 units). Line numbers are 0-based.

#include <cstdint>

namespace neomifes::document {

using TextPos    = std::uint64_t;
using LineNumber = std::uint64_t;

struct TextRange {
    TextPos start = 0;
    TextPos end   = 0;  // exclusive

    [[nodiscard]] constexpr bool     empty()  const noexcept { return start == end; }
    [[nodiscard]] constexpr TextPos  length() const noexcept { return end - start; }

    friend constexpr bool operator==(const TextRange&, const TextRange&) = default;
};

}  // namespace neomifes::document
