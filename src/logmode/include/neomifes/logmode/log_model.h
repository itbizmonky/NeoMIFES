#pragma once

// LogModel - headless log-line pattern matching (WI-14a, Phase 10.1 core).
// build() matches one LogPatternRule against every line of a
// document::Document, synchronously, on the calling thread. No background
// worker, no EditorSession integration, no UI - see build_plan.md's WI-14a
// entry for the full sub-WI breakdown (WI-14b adds async indexing and
// piece-streaming; WI-14c adds the UI; WI-14d adds multi-line grouping and
// user-editable pattern files).

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "neomifes/document/text_pos.h"  // document::LineNumber
#include "neomifes/logmode/log_pattern.h"
#include "neomifes/logmode/timestamp_parser.h"

namespace neomifes::document {
class Document;
class BufferSnapshot;
}  // namespace neomifes::document

namespace neomifes::logmode {

enum class LogPatternError { InvalidRegex };

// Deliberately compact: message/traceId/spanId text is NOT duplicated here.
// A LogModel over a multi-million-line document must not hold several
// std::u16string copies per line - see this WI's 設計方針5 (build_plan.md).
// Callers needing a line's full text call document::Document::lineText(line)
// themselves (the same "don't cache what's cheap to recompute" reasoning
// EditorSession::language() already applies).
struct LogLine {
    document::LineNumber     line = 0;
    std::optional<Timestamp> timestamp;
    LogLevel                 level = LogLevel::Unknown;
    bool                     matched = false;
};

class LogModel {
public:
    // Matches `rule` against every line of `doc` (line 0..doc.lineCount()-1),
    // synchronously - no worker thread yet (WI-14b). Every document line
    // produces exactly one LogLine; a line the rule's regex does not match
    // gets matched=false rather than being dropped (see
    // LinesSizeAlwaysEqualsDocumentLineCount in the test file for the
    // structural invariant this guarantees - this is also why multi-line
    // entries, e.g. a Java stack trace's continuation lines, show up as
    // consecutive matched=false lines rather than being grouped under the
    // preceding matched line; grouping is WI-14d scope).
    //
    // `assumedYear` is forwarded to parseTimestamp() for rules whose
    // timestampFormat has no "%Y" component (RFC 3164 syslog is the only
    // built-in rule this applies to - see timestamp_parser.h). Left
    // unspecified, such rules simply produce LogLine::timestamp==nullopt
    // rather than reading the wall clock - build() must stay deterministic
    // for its own sake as much as for its tests.
    //
    // Deliberately a static factory returning by value - NOT the roadmap
    // sketch's LogModel::attach(Document&, rule) mutate-in-place shape.
    // See this WI's Context/設計方針 (build_plan.md) for why: a Document*-
    // holding LogModel would raise a document-swap lifetime question this
    // headless stage doesn't need to answer yet, and
    // search::SearchService::findAll() already establishes the
    // "static, self-contained per call" precedent this class follows.
    [[nodiscard]] static std::expected<LogModel, LogPatternError> build(
        const document::Document& doc, const LogPatternRule& rule,
        std::optional<int> assumedYear = std::nullopt);

    // WI-14b: piece-streaming core - walks `snapshot.pieces()` exactly once
    // (BufferSnapshot::pieceView(), not extract() - see this WI's 設計方針1
    // for why: Document::lineText() re-takes a snapshot() and re-walks the
    // whole piece list from cursor 0 on EVERY line, an O(lines * pieces)
    // cost this overload eliminates in favor of a single O(document length)
    // linear pass with only one line's worth of text ever held in memory at
    // once - the "never materialize the whole document" principle WI-01
    // established for saveFile() also applies here for the 10GB/60s target
    // (master_roadmap.md sec.10.1). The Document-taking overload above is a
    // one-line delegation to this one (`return build(*doc.snapshot(), rule,
    // assumedYear);`) - its own public contract and every WI-14a test are
    // unaffected by this internal change.
    [[nodiscard]] static std::expected<LogModel, LogPatternError> build(
        const document::BufferSnapshot& snapshot, const LogPatternRule& rule,
        std::optional<int> assumedYear = std::nullopt);

    [[nodiscard]] std::span<const LogLine> lines() const noexcept { return m_lines; }

private:
    std::vector<LogLine> m_lines;
};

}  // namespace neomifes::logmode
