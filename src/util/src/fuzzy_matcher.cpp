#include "neomifes/util/fuzzy_matcher.h"

#include <cstddef>

namespace neomifes::util {

namespace {

constexpr char16_t asciiToLower(char16_t c) noexcept {
    return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

constexpr bool isAsciiAlnum(char16_t c) noexcept {
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

// A match at `index` counts as a "word boundary" if it starts the string,
// follows a non-alphanumeric separator, or continues a lowercase-to-
// uppercase transition (e.g. matching the 'R' in "FindReplace").
constexpr bool isWordBoundary(std::u16string_view target, std::size_t index) noexcept {
    if (index == 0) {
        return true;
    }
    const char16_t prev = target[index - 1];
    if (!isAsciiAlnum(prev)) {
        return true;
    }
    const char16_t curr      = target[index];
    const bool     prevLower = prev >= u'a' && prev <= u'z';
    const bool     currUpper = curr >= u'A' && curr <= u'Z';
    return prevLower && currUpper;
}

}  // namespace

std::optional<int> fuzzyMatchScore(std::u16string_view query, std::u16string_view target) noexcept {
    if (query.empty()) {
        return 0;
    }

    constexpr int kMatchScore        = 1;
    constexpr int kConsecutiveBonus  = 3;
    constexpr int kWordBoundaryBonus = 5;

    int                         score = 0;
    std::size_t                 targetIndex = 0;
    std::optional<std::size_t> previousMatchIndex;

    for (const char16_t queryChar : query) {
        const char16_t queryLower = asciiToLower(queryChar);
        bool           found      = false;
        for (; targetIndex < target.size(); ++targetIndex) {
            if (asciiToLower(target[targetIndex]) != queryLower) {
                continue;
            }
            score += kMatchScore;
            if (previousMatchIndex.has_value() && targetIndex == *previousMatchIndex + 1) {
                score += kConsecutiveBonus;
            }
            if (isWordBoundary(target, targetIndex)) {
                score += kWordBoundaryBonus;
            }
            previousMatchIndex = targetIndex;
            ++targetIndex;
            found = true;
            break;
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return score;
}

}  // namespace neomifes::util
