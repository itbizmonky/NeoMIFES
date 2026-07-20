#include "neomifes/core/indentation_conversion.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>  // std::cmp_equal, std::move

#include "neomifes/document/buffer_snapshot.h"

namespace neomifes::core {

namespace {

using document::LineNumber;
using document::TextPos;
using document::TextRange;

std::u16string convertLeadingWhitespace(std::u16string_view leading,
                                        IndentationConversionTarget target, int tabWidth) {
    if (target == IndentationConversionTarget::TabsToSpaces) {
        std::u16string result;
        result.reserve(leading.size() * static_cast<std::size_t>(tabWidth));
        for (const char16_t ch : leading) {
            if (ch == u'\t') {
                result.append(static_cast<std::size_t>(tabWidth), u' ');
            } else {
                result.push_back(ch);
            }
        }
        return result;
    }
    // SpacesToTabs: walk the run, counting consecutive plain spaces; every
    // tabWidth of them collapses to one tab. Any remainder (< tabWidth,
    // including a run interrupted by an embedded tab) is left as spaces
    // rather than guessed at - an embedded tab already realigns to a tab
    // stop, so it is copied through unchanged and resets the space count.
    std::u16string result;
    std::size_t    spaceRun = 0;
    for (const char16_t ch : leading) {
        if (ch == u' ') {
            ++spaceRun;
            if (std::cmp_equal(spaceRun, tabWidth)) {
                result.push_back(u'\t');
                spaceRun = 0;
            }
            continue;
        }
        result.append(spaceRun, u' ');
        spaceRun = 0;
        result.push_back(ch);  // ch == u'\t'
    }
    result.append(spaceRun, u' ');
    return result;
}

}  // namespace

std::vector<PerCursorEdit> computeIndentationConversionEdits(
    IndentationConversionTarget target, int tabWidth, const document::Document& doc) {
    std::vector<PerCursorEdit> edits;
    const auto snapshot  = doc.snapshot();
    const auto lineCount = doc.lineCount();
    for (LineNumber line = 0; line < lineCount; ++line) {
        const TextPos lineStart = doc.lineToOffset(line);
        const TextPos lineEndExclusive =
            (line + 1 < lineCount) ? doc.lineToOffset(line + 1) : doc.length();
        const std::u16string lineText =
            snapshot->extract(TextRange{.start = lineStart, .end = lineEndExclusive});
        std::u16string_view lineSpan(lineText);
        const auto newlinePos = lineSpan.find(u'\n');
        if (newlinePos != std::u16string_view::npos) {
            lineSpan = lineSpan.substr(0, newlinePos);
        }

        std::size_t leadingLen = 0;
        while (leadingLen < lineSpan.size() &&
              (lineSpan[leadingLen] == u' ' || lineSpan[leadingLen] == u'\t')) {
            ++leadingLen;
        }
        if (leadingLen == 0) {
            continue;
        }

        const std::u16string_view leading = lineSpan.substr(0, leadingLen);
        std::u16string             converted = convertLeadingWhitespace(leading, target, tabWidth);
        if (converted == leading) {
            continue;
        }
        edits.push_back(PerCursorEdit{
            .range        = TextRange{.start = lineStart, .end = lineStart + leadingLen},
            .insertedText = std::move(converted)});
    }
    return edits;
}

}  // namespace neomifes::core
