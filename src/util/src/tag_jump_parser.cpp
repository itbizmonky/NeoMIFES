#include "neomifes/util/tag_jump_parser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>

namespace neomifes::util {

namespace {

// Same defensive digit-count bound as goto_line_parser.h's char[20] buffer
// (rejects on the 20th digit, i.e. allows up to 19).
constexpr std::size_t kMaxDigits = 19;

// Parses a run of ASCII digits starting at `pos` into `value`, advancing
// `pos` past them. False on no digits, too many digits, or overflow.
[[nodiscard]] bool parseDigits(std::u16string_view text, std::size_t& pos,
                               std::uint64_t& value) noexcept {
    std::array<char, kMaxDigits> buffer{};
    std::size_t                  count = 0;
    while (pos < text.size() && text[pos] >= u'0' && text[pos] <= u'9') {
        if (count >= kMaxDigits) {
            return false;
        }
        buffer.at(count) = static_cast<char>(text[pos]);
        ++count;
        ++pos;
    }
    if (count == 0) {
        return false;
    }
    const auto result = std::from_chars(buffer.data(), buffer.data() + count, value);
    return result.ec == std::errc{};
}

struct ParenGroup {
    std::uint64_t                line = 0;
    std::optional<std::uint64_t> column;
};

// Tries to parse "(line)" or "(line,column)" starting at lineText[parenIdx]
// (which must be '('). nullopt if the parenthesized content isn't a valid
// group (non-numeric, wrong separator, line/column of 0 - 1-based
// convention, "0" has no meaning).
[[nodiscard]] std::optional<ParenGroup> tryParseParenGroup(std::u16string_view lineText,
                                                           std::size_t parenIdx) noexcept {
    std::size_t   pos  = parenIdx + 1;
    std::uint64_t line = 0;
    if (!parseDigits(lineText, pos, line) || line == 0) {
        return std::nullopt;
    }
    if (pos < lineText.size() && lineText[pos] == u')') {
        return ParenGroup{.line = line, .column = std::nullopt};
    }
    if (pos < lineText.size() && lineText[pos] == u',') {
        ++pos;
        std::uint64_t column = 0;
        if (!parseDigits(lineText, pos, column) || column == 0) {
            return std::nullopt;
        }
        if (pos < lineText.size() && lineText[pos] == u')') {
            return ParenGroup{.line = line, .column = column};
        }
    }
    return std::nullopt;
}

// Characters Windows disallows in filenames, plus whitespace and the
// parenthesis delimiters themselves. Deliberately does NOT include ':' or
// '\'/'/' - a drive letter (C:\...) or UNC prefix (\\server\share\...) must
// survive the backward scan intact.
[[nodiscard]] constexpr bool isPathStopChar(char16_t c) noexcept {
    switch (c) {
        case u' ':
        case u'\t':
        case u'<':
        case u'>':
        case u'"':
        case u'|':
        case u'?':
        case u'*':
        case u'(':
        case u')':
            return true;
        default:
            return false;
    }
}

// Scans backward from parenIdx (exclusive) to find where the path substring
// starts - stops at start-of-string or the first stop character.
[[nodiscard]] std::size_t findPathStart(std::u16string_view lineText, std::size_t parenIdx) noexcept {
    std::size_t start = parenIdx;
    while (start > 0 && !isPathStopChar(lineText[start - 1])) {
        --start;
    }
    return start;
}

[[nodiscard]] constexpr bool isAsciiAlnum(char16_t c) noexcept {
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

// "Looks like a file path" heuristic: the filename component (text after the
// last '\' or '/', or the whole substring if none) must contain a '.'
// followed by 1-8 ASCII alphanumeric characters. Not a whitelist of known
// source-file extensions - see tag_jump_parser.h's header comment for why.
[[nodiscard]] bool looksLikeFilePath(std::u16string_view path) noexcept {
    if (path.empty()) {
        return false;
    }
    const auto                 lastSlash = path.find_last_of(u"\\/");
    const std::u16string_view filename =
        (lastSlash == std::u16string_view::npos) ? path : path.substr(lastSlash + 1);
    const auto lastDot = filename.find_last_of(u'.');
    if (lastDot == std::u16string_view::npos || lastDot + 1 >= filename.size()) {
        return false;
    }
    const std::u16string_view extension = filename.substr(lastDot + 1);
    if (extension.size() > 8) {
        return false;
    }
    return std::ranges::all_of(extension, isAsciiAlnum);
}

}  // namespace

std::optional<TagJumpReference> parseTagJumpReference(std::u16string_view lineText) noexcept {
    std::size_t searchFrom = 0;
    while (true) {
        const auto parenIdx = lineText.find(u'(', searchFrom);
        if (parenIdx == std::u16string_view::npos) {
            return std::nullopt;
        }
        const auto group = tryParseParenGroup(lineText, parenIdx);
        if (group) {
            const auto                 pathStart = findPathStart(lineText, parenIdx);
            const std::u16string_view pathSubstring =
                lineText.substr(pathStart, parenIdx - pathStart);
            if (looksLikeFilePath(pathSubstring)) {
                return TagJumpReference{
                    .path = std::u16string(pathSubstring), .line = group->line, .column = group->column};
            }
        }
        searchFrom = parenIdx + 1;
    }
}

}  // namespace neomifes::util
