#pragma once

// fuzzyMatchScore - subsequence-based fuzzy matching for short, curated
// strings (Phase 5b3c, command palette). Header-only declaration + a small
// .cpp, mirroring utf8_convert.h's split. Deliberately simplified compared
// to editors' typical DP-optimal fuzzy scorers (e.g. VSCode's): greedy
// leftmost matching, no typo tolerance, no path-separator handling - the
// command palette's candidate list is O(10) entries of curated English
// text, not an arbitrary file/symbol list, so this is not a bottleneck and
// a more elaborate scorer would be premature.

#include <optional>
#include <string_view>

namespace neomifes::util {

// Every character of `query` must appear in `target`, in order (not
// necessarily contiguous), to match at all; returns std::nullopt otherwise.
// Case-insensitive over the ASCII range only (`target` is expected to be a
// curated English command title, not general Unicode text - revisit if a
// future caller needs full Unicode case-folding).
//
// Score is higher for a better match: a base point per matched character,
// plus a bonus for each character that continues a consecutive run from the
// previous matched character, plus a bonus for a character matched at a
// "word boundary" in `target` (index 0, or immediately following a
// non-alphanumeric character or a lowercase-to-uppercase transition).
//
// An empty `query` matches everything with score 0 (the "nothing typed yet,
// show everything" state used by command_palette_filter.h).
[[nodiscard]] std::optional<int> fuzzyMatchScore(std::u16string_view query,
                                                  std::u16string_view target) noexcept;

}  // namespace neomifes::util
