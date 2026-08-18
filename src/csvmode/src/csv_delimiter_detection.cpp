#include "neomifes/csvmode/csv_delimiter_detection.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/document/document.h"

namespace neomifes::csvmode {

namespace {

// Same untuned-initial-value caveat as logmode::kMinMatchRatio
// (format_detection.cpp) - revisit once real-world CSV samples are
// available.
constexpr double kMinConsistencyRatio = 0.5;

// Same trimming reasoning as format_detection.cpp's own trimTrailingCr():
// document::Document::lineText() keeps a trailing '\r' as line content
// (WI-12 convention, only '\n' is a line separator) - a candidate that
// happens to be tested against a CRLF file must not count '\r' as part of
// its own occurrence scan (irrelevant here since none of
// kCsvDelimiterCandidates is '\r', but trimming keeps line length
// consistent with what the CSV parser itself will actually see).
[[nodiscard]] std::u16string_view trimTrailingCr(std::u16string_view line) noexcept {
    if (!line.empty() && line.back() == u'\r') {
        return line.substr(0, line.size() - 1);
    }
    return line;
}

// How many of `sampledLines` share `delimiter`'s most common NON-ZERO
// per-line occurrence count. Lines with zero occurrences are excluded from
// the histogram entirely (not just scored low) - otherwise a delimiter
// that never appears anywhere would have every line "agree" on a count of
// zero and win by consistency alone, which is the opposite of what this
// function is meant to detect.
[[nodiscard]] std::size_t consistencyScore(char16_t delimiter, std::span<const std::u16string> sampledLines) {
    std::vector<std::pair<std::size_t, std::size_t>> countFrequencies;  // (occurrence count, line tally)
    for (const std::u16string& line : sampledLines) {
        const auto count = static_cast<std::size_t>(std::count(line.begin(), line.end(), delimiter));
        if (count == 0) {
            continue;
        }
        const auto existing = std::ranges::find_if(
            countFrequencies, [count](const std::pair<std::size_t, std::size_t>& entry) { return entry.first == count; });
        if (existing != countFrequencies.end()) {
            ++existing->second;
        } else {
            countFrequencies.emplace_back(count, 1U);
        }
    }
    std::size_t best = 0;
    for (const auto& [count, tally] : countFrequencies) {
        best = std::max(best, tally);
    }
    return best;
}

}  // namespace

std::optional<char16_t> detectCsvDelimiter(const document::Document& doc, std::size_t sampleLines,
                                            std::span<const char16_t> candidates) {
    const std::uint64_t totalLines      = doc.lineCount();
    const std::uint64_t consideredLines = std::min<std::uint64_t>(sampleLines, totalLines);
    if (consideredLines == 0 || candidates.empty()) {
        return std::nullopt;
    }

    std::vector<std::u16string> sampledLines;
    sampledLines.reserve(consideredLines);
    for (document::LineNumber line = 0; line < consideredLines; ++line) {
        sampledLines.emplace_back(trimTrailingCr(doc.lineText(line)));
    }

    std::size_t bestIndex = candidates.size();
    std::size_t bestScore = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::size_t score = consistencyScore(candidates[i], sampledLines);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }
    if (bestIndex == candidates.size()) {
        return std::nullopt;
    }
    const auto ratio = static_cast<double>(bestScore) / static_cast<double>(consideredLines);
    if (ratio < kMinConsistencyRatio) {
        return std::nullopt;
    }
    return candidates[bestIndex];
}

}  // namespace neomifes::csvmode
