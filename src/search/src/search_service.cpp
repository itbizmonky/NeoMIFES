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
//
// Every pattern is prefixed with "(?m)": since Phase 5b1, findAll() scans
// the whole document as one buffer (not one buffer per line, as Phase 5a
// did), and RE2's ^/$ only anchor to the start/end of the *entire* buffer
// unless multi-line mode is requested inline (RE2 only honours this via the
// (?m) flag when posix_syntax is off, which is this project's mode - see
// re2.h's Options comment). (?m) keeps ^/$ meaning "start/end of line" as
// they did implicitly in Phase 5a, so existing line-anchored queries (e.g.
// "^$" for a blank line) keep working. A query wanting the true start/end
// of the whole document can use RE2's \A/\z instead.
[[nodiscard]] std::string buildPattern(const Query& query, const std::string& patternUtf8) {
    std::string pattern = query.regex ? patternUtf8 : re2::RE2::QuoteMeta(patternUtf8);
    if (query.wholeWord) {
        pattern = "\\b(?:" + pattern + ")\\b";
    }
    return "(?m)" + pattern;
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

// Scans an already-UTF-8-converted buffer for every non-overlapping match,
// appending each as a document::TextRange (bufferStart-relative byte offsets
// mapped back to UTF-16 via `conv.byteToUtf16`) to `out`. Since Phase 5b1,
// `conv` covers the whole document (not one line, as in Phase 5a), so a
// match's UTF-8 bytes - and therefore RE2's search window - may span
// multiple original lines; nothing below needs to know that. Handles an
// empty buffer (conv.utf8.empty(), i.e. an empty document) as a special
// case: RE2 documents submatch[0].data() as indistinguishable from "no
// match" (always NULL) when the input text itself is empty, so byte-offset
// pointer arithmetic is skipped there in favour of the one deterministic
// answer (position 0).
void findAllInBuffer(const re2::RE2& re, const util::Utf8Conversion& conv,
                      document::TextPos bufferStart, std::vector<Match>& out) {
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
                                 .start = bufferStart + conv.byteToUtf16[byteStart],
                                 .end   = bufferStart + conv.byteToUtf16[byteEnd],
                             }});

        if (conv.utf8.empty()) {
            break;  // only one possible position on an empty document
        }

        if (byteEnd > byteStart) {
            searchPos = byteEnd;
            continue;
        }

        // Zero-length match (e.g. pattern "a*" where there is no 'a'
        // nearby): advance past the *entire* codepoint at byteEnd, not just
        // one byte. Landing mid-UTF-8-sequence would let RE2 report
        // spurious additional zero-length matches at each continuation
        // byte, which conv.byteToUtf16 maps back to the same UTF-16 offset
        // as the legitimate match (every byte of one codepoint shares one
        // entry) - producing duplicate matches around any non-ASCII text.
        const std::uint32_t utf16AtByteEnd = conv.byteToUtf16[byteEnd];
        searchPos = byteEnd + 1;
        while (searchPos < conv.utf8.size() && conv.byteToUtf16[searchPos] == utf16AtByteEnd) {
            ++searchPos;
        }
    }
}

// Single forward pass over the snapshot's pieces, concatenating the whole
// document into one UTF-16 buffer before searching it as a unit (Phase 5b1:
// this is what makes matches able to span line boundaries - see
// search_service.h's scope comment for the memory-vs-document-size tradeoff
// this implies). Deliberately does NOT use BufferSnapshot::extract() -
// extract() re-walks the full piece list from cursor=0 on every call (its
// own doc comment says so); pieceView() is O(1) per piece, the same
// primitive LineIndex::build() already uses to stay O(document length).
void scanDocument(const re2::RE2& re, const document::BufferSnapshot& snapshot, std::vector<Match>& matches) {
    std::u16string buffer;
    for (const auto& piece : snapshot.pieces()) {
        buffer.append(snapshot.pieceView(piece));
    }
    findAllInBuffer(re, util::toUtf8WithOffsets(buffer), /*bufferStart=*/0, matches);
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
