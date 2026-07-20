#include "neomifes/util/glob_match.h"

#include <cstddef>
#include <limits>

namespace neomifes::util {

namespace {

constexpr char16_t asciiToLower(char16_t c) noexcept {
    return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

constexpr bool foldedEqual(char16_t a, char16_t b) noexcept {
    return asciiToLower(a) == asciiToLower(b);
}

}  // namespace

bool globMatch(std::u16string_view pattern, std::u16string_view text) noexcept {
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

    std::size_t p        = 0;
    std::size_t t         = 0;
    std::size_t starIndex = kNone;  // position in `pattern` of the most recent '*'
    std::size_t matchIndex = 0;     // position in `text` that '*' last tried to cover up to

    // Standard iterative wildcard matcher: on a literal/'?' match, advance
    // both pointers; on '*', record where it is and optimistically match
    // zero characters; on a mismatch, backtrack to the most recent '*' (if
    // any) and have it cover one more character of `text` than last time.
    // No recursion, so no stack-depth risk from a pathological pattern.
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == u'?' || foldedEqual(pattern[p], text[t]))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == u'*') {
            starIndex  = p;
            matchIndex = t;
            ++p;
        } else if (starIndex != kNone) {
            p = starIndex + 1;
            ++matchIndex;
            t = matchIndex;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == u'*') {
        ++p;
    }
    return p == pattern.size();
}

}  // namespace neomifes::util
