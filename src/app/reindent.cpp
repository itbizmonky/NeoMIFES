#include "neomifes/app/reindent.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "neomifes/core/cursor.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/syntax/syntax.h"

namespace neomifes::app {

namespace {

using document::LineNumber;
using document::TextPos;
using document::TextRange;

struct BraceEvent {
    TextPos position;
    bool    isOpen;
};

struct TokenClassification {
    std::vector<BraceEvent> braceEvents;
    std::vector<TextRange>  skipRanges;
};

// Splits a parsed token stream into '{'/'}' brace events and String/Comment
// "skip ranges" (see reindent.h's header comment for why). Braces are
// restricted to 1-character Punctuation tokens so multi-character anonymous
// leaves (e.g. JS/TS template-literal interpolation's "${") are never
// misread as a bare brace. Both output vectors are already ascending -
// syntax::parse() returns a flat, left-to-right, non-overlapping stream.
TokenClassification classifyTokens(const std::vector<syntax::Token>& tokens, const std::u16string& wholeText) {
    TokenClassification result;
    for (const auto& tok : tokens) {
        if (tok.kind == syntax::TokenKind::Punctuation && tok.range.length() == 1) {
            const char16_t ch = wholeText[tok.range.start];
            if (ch == u'{' || ch == u'}') {
                result.braceEvents.push_back(BraceEvent{.position = tok.range.start, .isOpen = (ch == u'{')});
            }
        } else if (tok.kind == syntax::TokenKind::String || tok.kind == syntax::TokenKind::Comment) {
            result.skipRanges.push_back(tok.range);
        }
    }
    return result;
}

// Tracks brace-nesting depth across a forward-only, per-line scan of
// `events` (must already be ascending). For each line, callers must call
// startsWithClose() then advanceThrough() exactly once, in that order, with
// a strictly increasing `lineEndExclusive` across calls.
class DepthTracker {
public:
    explicit DepthTracker(std::span<const BraceEvent> events) noexcept : m_events(events) {}

    // Whether the next not-yet-applied event is a close brace located
    // exactly at `contentStart` (the line's first non-whitespace column) -
    // i.e. this line's own displayed indent should dedent by one level
    // before advanceThrough() below folds that same event into the running
    // depth for subsequent lines.
    [[nodiscard]] bool startsWithClose(TextPos contentStart) const noexcept {
        return m_index < m_events.size() && m_events[m_index].position == contentStart &&
              !m_events[m_index].isOpen;
    }

    [[nodiscard]] int depth() const noexcept { return m_depth; }

    void advanceThrough(TextPos lineEndExclusive) noexcept {
        while (m_index < m_events.size() && m_events[m_index].position < lineEndExclusive) {
            m_depth = m_events[m_index].isOpen ? m_depth + 1 : std::max(m_depth - 1, 0);
            ++m_index;
        }
    }

private:
    std::span<const BraceEvent> m_events;
    std::size_t                 m_index = 0;
    int                         m_depth = 0;
};

// Tracks whether successive, increasing line-start positions fall inside a
// multi-line String/Comment token that began on an earlier line. `ranges`
// must already be ascending.
class SkipRangeTracker {
public:
    explicit SkipRangeTracker(std::span<const TextRange> ranges) noexcept : m_ranges(ranges) {}

    [[nodiscard]] bool insideMultilineToken(TextPos lineStart) noexcept {
        while (m_index < m_ranges.size() && m_ranges[m_index].end <= lineStart) {
            ++m_index;
        }
        return m_index < m_ranges.size() && m_ranges[m_index].start < lineStart &&
              m_ranges[m_index].end > lineStart;
    }

private:
    std::span<const TextRange> m_ranges;
    std::size_t                m_index = 0;
};

// Tracks whether successive, increasing line numbers fall inside one of
// `ranges` (must already be ascending and merged - reindentSelectedLineRanges()'s
// own contract). An empty `ranges` means every line is in scope (whole
// document).
class ScopeTracker {
public:
    explicit ScopeTracker(std::span<const ReindentLineRange> ranges) noexcept : m_ranges(ranges) {}

    [[nodiscard]] bool contains(LineNumber line) noexcept {
        if (m_ranges.empty()) {
            return true;
        }
        while (m_index < m_ranges.size() && line > m_ranges[m_index].endInclusive) {
            ++m_index;
        }
        return m_index < m_ranges.size() && line >= m_ranges[m_index].start &&
              line <= m_ranges[m_index].endInclusive;
    }

private:
    std::span<const ReindentLineRange> m_ranges;
    std::size_t                        m_index = 0;
};

struct LineBounds {
    TextPos     lineStart;
    TextPos     lineEndExclusive;
    std::size_t leadingLen;
    bool        isBlank;
};

LineBounds computeLineBounds(const document::Document& doc, const std::u16string& wholeText, LineNumber line,
                             LineNumber lineCount) {
    const TextPos lineStart = doc.lineToOffset(line);
    const TextPos lineEndExclusive = (line + 1 < lineCount) ? doc.lineToOffset(line + 1) : doc.length();
    TextPos       contentEnd       = lineEndExclusive;
    if (contentEnd > lineStart && wholeText[contentEnd - 1] == u'\n') {
        --contentEnd;
    }
    std::size_t leadingLen = 0;
    while (lineStart + leadingLen < contentEnd &&
          (wholeText[lineStart + leadingLen] == u' ' || wholeText[lineStart + leadingLen] == u'\t')) {
        ++leadingLen;
    }
    return LineBounds{.lineStart         = lineStart,
                      .lineEndExclusive = lineEndExclusive,
                      .leadingLen       = leadingLen,
                      .isBlank          = (lineStart + leadingLen == contentEnd)};
}

std::u16string indentUnitFor(int tabWidth, bool insertSpacesForTab) {
    return insertSpacesForTab ? std::u16string(static_cast<std::size_t>(tabWidth), u' ')
                              : std::u16string(1, u'\t');
}

}  // namespace

bool supportsReindent(syntax::Language language) noexcept {
    switch (language) {
        case syntax::Language::Cpp:
        case syntax::Language::C:
        case syntax::Language::JavaScript:
        case syntax::Language::Java:
        case syntax::Language::Go:
        case syntax::Language::Rust:
        case syntax::Language::TypeScript:
        case syntax::Language::Tsx:
        case syntax::Language::Php:
        case syntax::Language::Json:
        case syntax::Language::Css:
        case syntax::Language::PowerShell:
            return true;
        case syntax::Language::Python:
        case syntax::Language::Html:
        case syntax::Language::Xml:
        case syntax::Language::Shell:
        case syntax::Language::Yaml:
        case syntax::Language::Toml:
        case syntax::Language::Markdown:
        case syntax::Language::Ini:
        case syntax::Language::Batch:
        case syntax::Language::Sql:
            return false;
    }
    return false;  // unreachable (all Language enumerators handled above)
}

std::vector<ReindentLineRange> reindentSelectedLineRanges(const document::Document&      doc,
                                                           std::span<const core::Cursor> cursors) {
    std::vector<ReindentLineRange> ranges;
    for (const auto& cursor : cursors) {
        if (!cursor.hasSelection()) {
            continue;
        }
        const TextPos     lo        = std::min(cursor.position, cursor.anchor);
        const TextPos     hi        = std::max(cursor.position, cursor.anchor);
        const LineNumber startLine = doc.offsetToLine(lo);
        LineNumber        endLine   = doc.offsetToLine(hi);
        // If `hi` lands exactly at endLine's own start (column 0) and the
        // selection spans more than one line, exclude endLine - nothing of
        // it is actually selected (e.g. Shift+Down to column 0 of the next
        // line), matching common editors' line-selection UX.
        if (endLine > startLine && doc.lineToOffset(endLine) == hi) {
            --endLine;
        }
        ranges.push_back(ReindentLineRange{.start = startLine, .endInclusive = endLine});
    }
    if (ranges.empty()) {
        return ranges;
    }

    std::ranges::sort(ranges, [](const ReindentLineRange& a, const ReindentLineRange& b) {
        return a.start < b.start;
    });
    std::vector<ReindentLineRange> merged;
    merged.push_back(ranges.front());
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        ReindentLineRange&       last = merged.back();
        const ReindentLineRange& cur  = ranges[i];
        if (cur.start <= last.endInclusive + 1) {
            last.endInclusive = std::max(last.endInclusive, cur.endInclusive);
        } else {
            merged.push_back(cur);
        }
    }
    return merged;
}

std::vector<core::PerCursorEdit> computeReindentEdits(const document::Document& doc, syntax::Language language,
                                                       int tabWidth, bool insertSpacesForTab,
                                                       std::span<const core::Cursor> cursors) {
    std::vector<core::PerCursorEdit> edits;
    const auto lineCount = doc.lineCount();
    if (lineCount == 0) {
        return edits;
    }

    const std::vector<ReindentLineRange> scopedRanges = reindentSelectedLineRanges(doc, cursors);

    const auto           snapshot  = doc.snapshot();
    const std::u16string wholeText = snapshot->extractNoCache(TextRange{.start = 0, .end = doc.length()});
    const TokenClassification classified = classifyTokens(syntax::parse(wholeText, language), wholeText);

    const std::u16string indentUnit = indentUnitFor(tabWidth, insertSpacesForTab);

    DepthTracker     depthTracker(classified.braceEvents);
    SkipRangeTracker skipTracker(classified.skipRanges);
    ScopeTracker     scopeTracker(scopedRanges);

    for (LineNumber line = 0; line < lineCount; ++line) {
        const LineBounds bounds      = computeLineBounds(doc, wholeText, line, lineCount);
        const TextPos     contentStart = bounds.lineStart + bounds.leadingLen;

        const bool insideMultilineToken = skipTracker.insideMultilineToken(bounds.lineStart);
        const bool inScope             = scopeTracker.contains(line);
        const bool startsWithClose     = !bounds.isBlank && depthTracker.startsWithClose(contentStart);
        const int  displayDepth = std::max(depthTracker.depth() - (startsWithClose ? 1 : 0), 0);

        if (!bounds.isBlank && !insideMultilineToken && inScope) {
            const std::u16string_view currentLeading(wholeText.data() + bounds.lineStart, bounds.leadingLen);
            std::u16string desired;
            desired.reserve(static_cast<std::size_t>(displayDepth) * indentUnit.size());
            for (int i = 0; i < displayDepth; ++i) {
                desired += indentUnit;
            }
            if (currentLeading != std::u16string_view(desired)) {
                edits.push_back(core::PerCursorEdit{
                    .range        = TextRange{.start = bounds.lineStart, .end = contentStart},
                    .insertedText = std::move(desired)});
            }
        }

        depthTracker.advanceThrough(bounds.lineEndExclusive);
    }

    return edits;
}

}  // namespace neomifes::app
