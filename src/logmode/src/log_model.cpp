#include "neomifes/logmode/log_model.h"

#include <re2/re2.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/util/utf8_convert.h"

namespace neomifes::logmode {

namespace {

// Compiles `rule.pattern` as RE2 (mirrors search::SearchService's compile(),
// search_service.cpp) - nullptr on any compile failure (a hand-edited or
// future user-supplied rule with a typo'd regex is an expected condition
// this WI's LogPatternError::InvalidRegex surfaces, not an exception).
[[nodiscard]] std::unique_ptr<re2::RE2> compileRule(const LogPatternRule& rule) {
    const util::Utf8Conversion patternConv = util::toUtf8WithOffsets(rule.pattern);
    re2::RE2::Options          options;
    options.set_log_errors(false);  // an invalid rule is a data problem, not worth logging
    auto re = std::make_unique<re2::RE2>(patternConv.utf8, options);
    if (!re->ok()) {
        return nullptr;
    }
    return re;
}

// Resolves the RE2 submatch indices for this rule's "timestamp" and
// "level" named capture groups, once per compiled pattern (not once per
// line) - RE2::NamedCapturingGroups() returns name->index; a rule missing
// either group (e.g. RFC 5424/3164 syslog have no textual "level" - PRI
// encodes severity numerically, not as text; the Apache/Nginx access-log
// rule has neither field this WI decodes) simply leaves that index unset.
struct FieldIndices {
    std::optional<int> timestampIdx;
    std::optional<int> levelIdx;
};

[[nodiscard]] FieldIndices resolveFieldIndices(const re2::RE2& re) {
    FieldIndices indices;
    for (const auto& [name, index] : re.NamedCapturingGroups()) {
        if (name == "timestamp") {
            indices.timestampIdx = index;
        } else if (name == "level") {
            indices.levelIdx = index;
        }
    }
    return indices;
}

// Matches `re` against a single already-\r-trimmed line, returning the
// UTF-16 substring captured by 1-based submatch `idx`, or nullopt if that
// group is absent from the rule, did not participate in this particular
// match (RE2 reports sub.data()==nullptr for a non-participating optional
// group), or the line is empty. Byte->UTF-16 offset mapping mirrors
// search_service.cpp's submatchToRange() (Phase 5b2) - same underlying
// util::Utf8Conversion contract.
[[nodiscard]] std::optional<std::u16string> captureGroupText(std::optional<int>              idx,
                                                               const std::vector<absl::string_view>& submatches,
                                                               const util::Utf8Conversion&      conv,
                                                               std::u16string_view              lineTrimmed) {
    if (!idx.has_value() || *idx <= 0 || static_cast<std::size_t>(*idx) >= submatches.size()) {
        return std::nullopt;
    }
    const absl::string_view& sub = submatches[static_cast<std::size_t>(*idx)];
    if (sub.data() == nullptr || conv.utf8.empty()) {
        return std::nullopt;
    }
    const auto        byteStart = static_cast<std::size_t>(sub.data() - conv.utf8.data());
    const std::size_t byteEnd   = byteStart + sub.size();
    const auto         utf16Start = conv.byteToUtf16[byteStart];
    const auto         utf16End   = conv.byteToUtf16[byteEnd];
    return std::u16string(lineTrimmed.substr(utf16Start, utf16End - utf16Start));
}

// Matches `re`/`indices` against document line `lineNumber`'s text,
// producing one LogLine (matched=false, default level/timestamp, if the
// regex doesn't match - never throws, never skips a line).
[[nodiscard]] LogLine matchLine(const re2::RE2& re, const FieldIndices& indices,
                                 std::u16string_view timestampFormat, std::optional<int> assumedYear,
                                 document::LineNumber lineNumber, std::u16string_view rawLineText) {
    LogLine result;
    result.line = lineNumber;

    // Document::lineText() keeps a trailing '\r' as line content (WI-12
    // confirmed convention: only '\n' is the line separator) - trim it once
    // here so it can never leak into a captured field.
    std::u16string_view trimmed = rawLineText;
    if (!trimmed.empty() && trimmed.back() == u'\r') {
        trimmed = trimmed.substr(0, trimmed.size() - 1);
    }

    const util::Utf8Conversion conv = util::toUtf8WithOffsets(trimmed);

    const int                      numGroups = re.NumberOfCapturingGroups();
    std::vector<absl::string_view> submatches(static_cast<std::size_t>(numGroups) + 1);
    const bool matched = re.Match(conv.utf8, 0, conv.utf8.size(), re2::RE2::UNANCHORED, submatches.data(),
                                   static_cast<int>(submatches.size()));
    if (!matched) {
        return result;
    }
    result.matched = true;

    if (const auto text = captureGroupText(indices.timestampIdx, submatches, conv, trimmed)) {
        result.timestamp = parseTimestamp(*text, timestampFormat, assumedYear);
    }
    if (const auto text = captureGroupText(indices.levelIdx, submatches, conv, trimmed)) {
        result.level = parseLevel(*text);
    }
    return result;
}

}  // namespace

std::expected<LogModel, LogPatternError> LogModel::build(const document::BufferSnapshot& snapshot,
                                                           const LogPatternRule&           rule,
                                                           std::optional<int>              assumedYear) {
    const std::unique_ptr<re2::RE2> re = compileRule(rule);
    if (re == nullptr) {
        return std::unexpected(LogPatternError::InvalidRegex);
    }
    const FieldIndices indices = resolveFieldIndices(*re);

    LogModel model;
    model.m_lines.reserve(snapshot.lineCount());

    // Single linear pass over the piece list (mirrors LineIndex::build()'s
    // pieceTextStreamed()-based walk, line_index.cpp) - `currentLine`
    // accumulates one line's content at a time, correctly spanning piece
    // AND chunk boundaries (a line's text may straddle two pieces after
    // edits, or two streamed chunks within one large piece), and is
    // cleared after every '\n'. Never holds more than one line's worth of
    // text, unlike Document::lineText()'s per-line snapshot()+extract()
    // cost. pieceTextStreamed() rather than pieceTextNoCache(): the latter
    // still materializes an entire piece as one std::u16string before this
    // loop can start walking it - fine for a small edited piece, but for a
    // freshly opened multi-GB file (one single huge Original piece) that
    // transiently reserves ~2x the file's own size in one allocation just
    // to scan through and discard it, the same problem LineIndex::build()
    // had (measured: a 10GB file's one-shot decode took over 50s and
    // approached the system's physical memory ceiling before being
    // streamed instead). See docs/issues/decode_cache_unbounded_growth.md.
    std::u16string        currentLine;
    document::LineNumber  lineNumber = 0;
    for (const document::Piece& piece : snapshot.pieces()) {
        [[maybe_unused]] const bool streamedOk =
            snapshot.pieceTextStreamed(piece, [&](std::u16string_view chunk) {
                for (const char16_t ch : chunk) {
                    if (ch == u'\n') {
                        model.m_lines.push_back(matchLine(*re, indices, rule.timestampFormat, assumedYear,
                                                          lineNumber, currentLine));
                        currentLine.clear();
                        ++lineNumber;
                    } else {
                        currentLine.push_back(ch);
                    }
                }
            });
        // Same "best-effort, no crash" contract as before this streaming
        // rewrite - a page error mid-piece simply leaves the remainder of
        // this piece (and any following pieces) unscanned.
    }
    // Final line, whether or not it ends in '\n' (BufferSnapshot::lineCount()
    // == newlineCount()+1 always counts it, matching the empty-document ->
    // one-unmatched-line convention this WI's tests already pin down).
    model.m_lines.push_back(matchLine(*re, indices, rule.timestampFormat, assumedYear, lineNumber, currentLine));
    return model;
}

std::expected<LogModel, LogPatternError> LogModel::build(const document::Document& doc,
                                                           const LogPatternRule&     rule,
                                                           std::optional<int>        assumedYear) {
    return build(*doc.snapshot(), rule, assumedYear);
}

}  // namespace neomifes::logmode
