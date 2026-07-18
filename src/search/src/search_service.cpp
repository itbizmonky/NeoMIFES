#include "neomifes/search/search_service.h"

#include <re2/re2.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/util/utf8_convert.h"

namespace neomifes::search {

namespace {

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
// match, appending each as a document::TextRange (lineStart-relative byte
// offsets mapped back to UTF-16 via `conv.byteToUtf16`) to `out`. Handles
// empty lines (conv.utf8.empty()) as a special case: RE2 documents
// submatch[0].data() as indistinguishable from "no match" (always NULL) when
// the input text itself is empty, so byte-offset pointer arithmetic is
// skipped there in favour of the one deterministic answer (position 0).
void findAllInLine(const re2::RE2& re, const util::Utf8Conversion& conv,
                    document::TextPos lineStart, std::vector<Match>& out) {
    std::size_t searchPos = 0;
    while (searchPos <= conv.utf8.size()) {
        absl::string_view submatch;
        if (!re.Match(conv.utf8, searchPos, conv.utf8.size(), re2::RE2::UNANCHORED, &submatch, 1)) {
            break;
        }

        std::size_t byteStart = 0;
        std::size_t byteEnd   = 0;
        if (!conv.utf8.empty()) {
            byteStart = static_cast<std::size_t>(submatch.data() - conv.utf8.data());
            byteEnd   = byteStart + submatch.size();
        }

        out.push_back(Match{.range = document::TextRange{
                                 .start = lineStart + conv.byteToUtf16[byteStart],
                                 .end   = lineStart + conv.byteToUtf16[byteEnd],
                             }});

        if (conv.utf8.empty()) {
            break;  // only one possible position on an empty line
        }

        if (byteEnd > byteStart) {
            searchPos = byteEnd;
            continue;
        }

        // Zero-length match (e.g. pattern "a*" where the line has no 'a'):
        // advance past the *entire* codepoint at byteEnd, not just one
        // byte. Landing mid-UTF-8-sequence would let RE2 report spurious
        // additional zero-length matches at each continuation byte, which
        // conv.byteToUtf16 maps back to the same UTF-16 offset as the
        // legitimate match (every byte of one codepoint shares one entry) -
        // producing duplicate matches on any non-ASCII line.
        const std::uint32_t utf16AtByteEnd = conv.byteToUtf16[byteEnd];
        searchPos = byteEnd + 1;
        while (searchPos < conv.utf8.size() && conv.byteToUtf16[searchPos] == utf16AtByteEnd) {
            ++searchPos;
        }
    }
}

// Single forward pass over the snapshot's pieces, splitting on '\n' and
// running findAllInLine() on each line as it completes. Deliberately does
// NOT use BufferSnapshot::extract() per line - extract() re-walks the full
// piece list from cursor=0 on every call (its own doc comment says so),
// which would make this O(lines * pieces); pieceView() is O(1) per piece,
// the same primitive LineIndex::build() already uses to stay O(document
// length) instead of re-scanning already-passed pieces for every line.
void scanDocument(const re2::RE2& re, const document::BufferSnapshot& snapshot, std::vector<Match>& matches) {
    document::TextPos lineStart = 0;
    document::TextPos cursor    = 0;
    std::u16string     lineBuffer;

    for (const auto& piece : snapshot.pieces()) {
        const std::u16string_view v = snapshot.pieceView(piece);
        std::size_t runStart = 0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] == u'\n') {
                lineBuffer.append(v.substr(runStart, i - runStart));
                findAllInLine(re, util::toUtf8WithOffsets(lineBuffer), lineStart, matches);
                lineBuffer.clear();
                lineStart = cursor + i + 1;
                runStart  = i + 1;
            }
        }
        lineBuffer.append(v.substr(runStart));
        cursor += piece.length;
    }
    // Final line: no trailing '\n' consumed it (or it's empty, if the
    // document ends with '\n' - BufferSnapshot::lineCount()'s documented
    // "documents without a trailing newline still have a final line"
    // contract applies symmetrically here too).
    findAllInLine(re, util::toUtf8WithOffsets(lineBuffer), lineStart, matches);
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

    scanDocument(*re, *doc.snapshot(), matches);
    return matches;
}

}  // namespace neomifes::search
