#include "neomifes/logmode/format_detection.h"

#include <re2/re2.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/util/utf8_convert.h"

namespace neomifes::logmode {

namespace {

// Below this fraction of sampled lines actually matching, a rule is
// considered a false-positive risk (e.g. a handful of coincidental matches
// in an unrelated source file) rather than a genuine detection - untuned
// initial value, revisit once real-world sample log files are available
// (see docs/issues/phase_10_1_v2_extended_patterns.md).
constexpr double kMinMatchRatio = 0.5;

// Trims a trailing '\r' the same way log_model.cpp's matchLine() does
// (Document::lineText() keeps it as line content for CRLF documents, WI-12
// convention) - detection must ignore it too, or every rule's match count
// would be skewed low on CRLF files.
[[nodiscard]] std::u16string_view trimTrailingCr(std::u16string_view line) noexcept {
    if (!line.empty() && line.back() == u'\r') {
        return line.substr(0, line.size() - 1);
    }
    return line;
}

struct CompiledRule {
    const LogPatternRule*     rule;
    std::unique_ptr<re2::RE2> re;
};

// Compiles every candidate rule's pattern once (mirrors log_model.cpp's
// compileRule(), minus the FieldIndices resolution detection doesn't need -
// PartialMatch() below never asks for submatches, only whether the pattern
// matches at all). Rules that fail to compile are silently skipped (cannot
// happen for the shipped built-in table today, but a hand-edited
// user-supplied rule reaching here - WI-14d - could plausibly fail here if
// loadLogPatternRuleFromFile()'s own compile check were ever bypassed, so
// this function still makes no assumption that every candidate compiles).
[[nodiscard]] std::vector<CompiledRule> compileRules(std::span<const LogPatternRule> candidates) {
    std::vector<CompiledRule> compiled;
    for (const LogPatternRule& rule : candidates) {
        const util::Utf8Conversion patternConv = util::toUtf8WithOffsets(rule.pattern);
        re2::RE2::Options          options;
        options.set_log_errors(false);
        auto re = std::make_unique<re2::RE2>(patternConv.utf8, options);
        if (re->ok()) {
            compiled.push_back(CompiledRule{.rule = &rule, .re = std::move(re)});
        }
    }
    return compiled;
}

}  // namespace

std::optional<LogPatternRule> detectLogPatternRule(const document::Document& doc, std::size_t sampleLines,
                                                     std::span<const LogPatternRule> candidates) {
    const std::uint64_t totalLines      = doc.lineCount();
    const std::uint64_t consideredLines = std::min<std::uint64_t>(sampleLines, totalLines);
    if (consideredLines == 0) {
        return std::nullopt;
    }

    const std::vector<CompiledRule> compiled = compileRules(candidates);
    std::vector<std::uint64_t>      matchCounts(compiled.size(), 0);

    for (document::LineNumber line = 0; line < consideredLines; ++line) {
        // Named local (not a temporary passed inline) so `trimmed`'s view
        // stays valid for the rest of this iteration.
        const std::u16string       lineText = doc.lineText(line);
        const std::u16string_view  trimmed  = trimTrailingCr(lineText);
        const util::Utf8Conversion conv     = util::toUtf8WithOffsets(trimmed);
        for (std::size_t i = 0; i < compiled.size(); ++i) {
            if (re2::RE2::PartialMatch(conv.utf8, *compiled[i].re)) {
                ++matchCounts[i];
            }
        }
    }

    std::size_t   bestIndex = compiled.size();
    std::uint64_t bestCount = 0;
    for (std::size_t i = 0; i < compiled.size(); ++i) {
        if (matchCounts[i] > bestCount) {
            bestCount = matchCounts[i];
            bestIndex = i;
        }
    }
    if (bestIndex == compiled.size()) {
        return std::nullopt;
    }
    const auto ratio = static_cast<double>(bestCount) / static_cast<double>(consideredLines);
    if (ratio < kMinMatchRatio) {
        return std::nullopt;
    }
    return *compiled[bestIndex].rule;
}

}  // namespace neomifes::logmode
