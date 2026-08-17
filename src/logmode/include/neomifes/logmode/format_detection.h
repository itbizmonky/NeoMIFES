#pragma once

// detectLogPatternRule - WI-14b format auto-detection. Tries each candidate
// rule (builtInLogPatterns()'s 4 rules by default) against a bounded sample
// of the document's leading lines and returns whichever rule matched the
// most of them. Synchronous (sampleLines bounds the cost regardless of
// document size), so this runs on the calling thread - no LogIndexWorker
// involvement, unlike LogModel::build() over a whole document.
//
// WI-14d: `candidates` lets a caller widen the search beyond the 4 built-in
// rules (e.g. "Log: Enable (Auto-Detect)" combines builtInLogPatterns()
// with the user's own loadUserLogPatternsFromDirectory() results,
// log_pattern_file.h) without this function needing to know anything about
// %APPDATA% or user-editable pattern files itself - it only ever sees a
// flat list of rules to try.

#include <cstddef>
#include <optional>
#include <span>

#include "neomifes/logmode/log_pattern.h"

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::logmode {

// Examines the first min(sampleLines, doc.lineCount()) lines and returns the
// candidate rule that matched the largest fraction of them, or std::nullopt
// if no rule reaches the minimum confidence threshold (an unrelated text
// file - e.g. ordinary source code - should not be misdetected as some log
// format just because a handful of lines happen to match). `candidates`
// defaults to builtInLogPatterns() - existing callers that only ever meant
// "try the built-in rules" are unaffected by this parameter's addition.
[[nodiscard]] std::optional<LogPatternRule> detectLogPatternRule(
    const document::Document& doc, std::size_t sampleLines = 100,
    std::span<const LogPatternRule> candidates = builtInLogPatterns());

}  // namespace neomifes::logmode
