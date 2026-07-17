#include "neomifes/search/search_service.h"

#include <re2/re2.h>

#include <memory>
#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/util/utf8_convert.h"

namespace neomifes::search {

namespace {

// Same "offset of last content code unit + 1" contract as the identically
// named helper in selection_model.cpp - duplicated rather than shared
// because it is three lines and core/search are sibling layers (both depend
// on document, neither depends on the other; see basic_design.md sec.2.1).
[[nodiscard]] document::TextPos lineContentEnd(const document::Document& doc,
                                                document::LineNumber      line) {
    if (line + 1 >= doc.lineCount()) {
        return doc.length();
    }
    return doc.lineToOffset(line + 1) - 1;
}

// Builds the RE2 pattern string for `query`: a literal query is escaped via
// QuoteMeta so it is matched verbatim rather than interpreted as regex
// syntax; wholeWord wraps either form in ASCII \b boundaries (RE2's \b is
// documented as ASCII-word-boundary only - it does not use this project's
// CJK-aware selectWordAt()/classify() char-class boundaries, which live in
// core:: and are not reachable from this sibling module. Known limitation,
// not attempted here).
[[nodiscard]] std::string buildPattern(const Query& query, const std::string& patternUtf8) {
    std::string pattern = query.regex ? patternUtf8 : re2::RE2::QuoteMeta(patternUtf8);
    if (query.wholeWord) {
        pattern = "\\b(?:" + pattern + ")\\b";
    }
    return pattern;
}

[[nodiscard]] std::unique_ptr<re2::RE2> compile(const Query& query) {
    const util::Utf8Conversion patternConv = util::toUtf8WithOffsets(query.pattern);

    re2::RE2::Options options;
    options.set_case_sensitive(query.caseSensitive);
    options.set_log_errors(false);  // invalid/incomplete regex is an expected interactive state, not worth logging

    auto re = std::make_unique<re2::RE2>(buildPattern(query, patternConv.utf8), options);
    if (!re->ok()) {
        return nullptr;
    }
    return re;
}

// Scans one line's already-UTF-8-converted text for every non-overlapping
// match, appending each as a document::TextRange (lineStart-relative
// byte offsets mapped back to UTF-16 via `conv.byteToUtf16`) to `out`.
void findAllInLine(const re2::RE2& re, const util::Utf8Conversion& conv,
                    document::TextPos lineStart, std::vector<Match>& out) {
    std::size_t searchPos = 0;
    while (searchPos <= conv.utf8.size()) {
        absl::string_view submatch;
        if (!re.Match(conv.utf8, searchPos, conv.utf8.size(), re2::RE2::UNANCHORED, &submatch, 1)) {
            break;
        }
        const auto byteStart = static_cast<std::size_t>(submatch.data() - conv.utf8.data());
        const std::size_t byteEnd = byteStart + submatch.size();

        out.push_back(Match{.range = document::TextRange{
                                 .start = lineStart + conv.byteToUtf16[byteStart],
                                 .end   = lineStart + conv.byteToUtf16[byteEnd],
                             }});

        // Zero-length matches (e.g. pattern "a*" where the line has no 'a')
        // would loop forever at the same searchPos otherwise.
        searchPos = (byteEnd > byteStart) ? byteEnd : byteEnd + 1;
    }
}

}  // namespace

std::vector<Match> SearchService::findAll(const document::Document& doc, const Query& query) {
    std::vector<Match> matches;
    if (query.pattern.empty()) {
        return matches;
    }

    const std::unique_ptr<re2::RE2> re = compile(query);
    if (re == nullptr) {
        return matches;
    }

    const auto snapshot = doc.snapshot();
    for (document::LineNumber line = 0; line < doc.lineCount(); ++line) {
        const document::TextPos lineStart = doc.lineToOffset(line);
        const document::TextPos lineEnd   = lineContentEnd(doc, line);
        if (lineStart >= lineEnd) {
            continue;  // empty line: RE2's empty-input submatch semantics make offset mapping unreliable (re2.h's Match() doc comment); scope excludes matches wholly on empty lines
        }

        const std::u16string lineText = snapshot->extract(document::TextRange{.start = lineStart, .end = lineEnd});
        const util::Utf8Conversion conv = util::toUtf8WithOffsets(lineText);

        findAllInLine(*re, conv, lineStart, matches);
    }

    return matches;
}

}  // namespace neomifes::search
