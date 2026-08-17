#pragma once

// detectLogPatternRule - WI-14b format auto-detection. Tries each of
// builtInLogPatterns()'s 4 rules against a bounded sample of the document's
// leading lines and returns whichever rule matched the most of them.
// Synchronous (sampleLines bounds the cost regardless of document size), so
// this runs on the calling thread - no LogIndexWorker involvement, unlike
// LogModel::build() over a whole document.

#include <cstddef>
#include <optional>

#include "neomifes/logmode/log_pattern.h"

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::logmode {

// Examines the first min(sampleLines, doc.lineCount()) lines and returns the
// built-in rule that matched the largest fraction of them, or std::nullopt
// if no rule reaches the minimum confidence threshold (an unrelated text
// file - e.g. ordinary source code - should not be misdetected as some log
// format just because a handful of lines happen to match).
[[nodiscard]] std::optional<LogPatternRule> detectLogPatternRule(const document::Document& doc,
                                                                   std::size_t sampleLines = 100);

}  // namespace neomifes::logmode
