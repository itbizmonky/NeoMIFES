#pragma once

// detectCsvDelimiter() - heuristic delimiter auto-detection (WI-16a),
// mirroring neomifes::logmode::detectLogPatternRule()'s sampling structure
// (format_detection.h) but with a different scoring rule - see this
// header's build_plan.md WI-16a entry for why "does the candidate appear
// at all" (detectLogPatternRule()'s own scoring) is not the right question
// for a delimiter: the candidates set includes characters (',' in
// particular) common enough in ordinary prose that mere presence is not
// discriminating. What IS discriminating is consistency: a genuine
// delimiter appears the same number of times on most sampled lines (one
// less than the column count), so this scores each candidate by how many
// sampled lines share its most common non-zero per-line occurrence count.

#include <array>
#include <optional>
#include <span>

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::csvmode {

inline constexpr std::array<char16_t, 4> kCsvDelimiterCandidates = {u',', u'\t', u';', u'|'};

// Samples up to `sampleLines` lines (fewer if the document is shorter) and
// returns whichever `candidates` entry shows the most consistent non-zero
// per-line occurrence count, or std::nullopt if no candidate clears the
// consistency threshold (an empty document, a document with no
// `candidates` character at all, or ordinary prose with no stable
// delimiter pattern). Never throws.
[[nodiscard]] std::optional<char16_t> detectCsvDelimiter(
    const document::Document& doc, std::size_t sampleLines = 100,
    std::span<const char16_t> candidates = kCsvDelimiterCandidates);

}  // namespace neomifes::csvmode
