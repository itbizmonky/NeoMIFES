#include "neomifes/search/search_service.h"

#include <re2/re2.h>

#include <algorithm>
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

// Converts one RE2 submatch to a UTF-16 TextRange, or `fallback` for a
// non-participating capture group / an empty document (RE2 reports
// sub.data() as nullptr in both cases - see findAllInBuffer's handling of
// submatch[0] for the same signal). Used for capture groups only (Phase
// 5b2) - submatch[0]'s own byteStart/byteEnd are computed inline in
// findAllInBuffer because they additionally drive the zero-width-match
// search-position advance below, which this helper does not need to know
// about.
[[nodiscard]] document::TextRange submatchToRange(const absl::string_view&    sub,
                                                    const util::Utf8Conversion& conv,
                                                    document::TextPos bufferStart,
                                                    document::TextPos fallback) {
    if (conv.utf8.empty() || sub.data() == nullptr) {
        return document::TextRange{.start = fallback, .end = fallback};
    }
    const auto         byteStart = static_cast<std::size_t>(sub.data() - conv.utf8.data());
    const std::size_t byteEnd   = byteStart + sub.size();
    return document::TextRange{.start = bufferStart + conv.byteToUtf16[byteStart],
                               .end   = bufferStart + conv.byteToUtf16[byteEnd]};
}

// Scans an already-UTF-8-converted buffer for every non-overlapping match,
// appending each as a document::TextRange (bufferStart-relative byte offsets
// mapped back to UTF-16 via `conv.byteToUtf16`) plus its capture groups
// (Phase 5b2) to `out`. Since Phase 5b1, `conv` covers the whole document
// (not one line, as in Phase 5a), so a match's UTF-8 bytes - and therefore
// RE2's search window - may span multiple original lines; nothing below
// needs to know that. Handles an empty buffer (conv.utf8.empty(), i.e. an
// empty document) as a special case: RE2 documents every submatch (whole
// match and every group) as indistinguishable from "no match"/"did not
// participate" (always NULL) when the input text itself is empty, so
// byte-offset pointer arithmetic is skipped there in favour of the one
// deterministic answer (position 0).
void findAllInBuffer(const re2::RE2& re, const util::Utf8Conversion& conv,
                      document::TextPos bufferStart, std::vector<Match>& out) {
    // Capture groups beyond $9 are never referenced by
    // search::expandReplacementTemplate() (Phase 5b2), so capping here
    // saves RE2 the cost of populating submatches nothing will read (RE2's
    // own docs note requesting fewer than all groups "runs much faster").
    const int                      numGroups = std::min(9, re.NumberOfCapturingGroups());
    std::vector<absl::string_view> submatches(static_cast<std::size_t>(numGroups) + 1);

    std::size_t searchPos = 0;
    while (searchPos <= conv.utf8.size()) {
        if (!re.Match(conv.utf8, searchPos, conv.utf8.size(), re2::RE2::UNANCHORED, submatches.data(),
                      static_cast<int>(submatches.size()))) {
            break;
        }
        const absl::string_view& submatch = submatches[0];

        std::size_t byteStart = 0;
        std::size_t byteEnd   = 0;
        if (!conv.utf8.empty()) {
            byteStart = static_cast<std::size_t>(submatch.data() - conv.utf8.data());
            byteEnd   = byteStart + submatch.size();
        }
        const document::TextPos matchStart = bufferStart + conv.byteToUtf16[byteStart];
        const document::TextPos matchEnd   = bufferStart + conv.byteToUtf16[byteEnd];

        std::vector<document::TextRange> groups;
        groups.reserve(static_cast<std::size_t>(numGroups));
        for (int g = 1; g <= numGroups; ++g) {
            groups.push_back(
                submatchToRange(submatches[static_cast<std::size_t>(g)], conv, bufferStart, matchStart));
        }

        out.push_back(Match{.range  = document::TextRange{.start = matchStart, .end = matchEnd},
                            .groups = std::move(groups)});

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

// search_crlf_line_ending.md: findAllInBuffer()'s pattern is compiled with
// "(?m)" (buildPattern() above), so RE2's own '^'/'$' anchor immediately
// after/before a '\n' - it has no concept of "\r\n" as a single unit, so a
// CRLF document's trailing '\r' sits between the matched content and the
// '\n' RE2 actually anchors to, and a pattern like "bar$" never matches
// "foo bar\r\n" even though "bar" is visually the end of that line. Stripped
// out below (only when at least one '\r' is present - see scanDocument()'s
// own early-out for the common LF-only case) before the buffer ever reaches
// RE2, then match positions are mapped back through boundaryToOriginal so
// callers still see offsets into the real, unmodified document.
//
// A lone '\r' NOT immediately followed by '\n' (old Mac line endings) is
// left untouched - this project's line-splitting convention
// (document::LineIndex, and core::selection_model.cpp's own
// lineContentEnd()) only ever recognizes '\n' as a line separator, so a lone
// '\r' is ordinary line content everywhere else in this codebase too; this
// fix stays consistent with that rather than inventing a second definition
// of "line ending" just for search.
struct CrStrippedText {
    std::u16string text;
    // boundaryToOriginal[k] is the smallest ORIGINAL-buffer position that
    // has produced exactly k characters of `text` so far - the ORIGINAL
    // position immediately after the k-th kept character, and BEFORE any
    // run of stripped '\r's that immediately follows it. Sized
    // text.size() + 1, same "sentinel entry for one-past-the-end"
    // convention util::toUtf8WithOffsets()'s byteToUtf16 already uses. This
    // "leftmost boundary" choice is what makes a match ending right before a
    // stripped '\r' (a '$' anchor - this struct's whole reason for existing)
    // resolve to the original position right before the '\r', not right
    // after it. The mirror case - a match STARTING with a literal '\n'
    // immediately after a stripped '\r' - is a narrow, unaffected edge case
    // this fix does not target (searches for literal '\r'/'\n' content are
    // not what search_crlf_line_ending.md is about; '^' anchors, which are
    // the common case, land one character later at the following kept
    // character and are unaffected by this ambiguity).
    std::vector<document::TextPos> boundaryToOriginal;
};

[[nodiscard]] CrStrippedText stripCrBeforeLf(std::u16string_view original) {
    CrStrippedText result;
    result.text.reserve(original.size());
    result.boundaryToOriginal.reserve(original.size() + 1);
    result.boundaryToOriginal.push_back(0);
    for (std::size_t i = 0; i < original.size(); ++i) {
        if (original[i] == u'\r' && i + 1 < original.size() && original[i + 1] == u'\n') {
            continue;
        }
        result.text.push_back(original[i]);
        result.boundaryToOriginal.push_back(static_cast<document::TextPos>(i) + 1);
    }
    return result;
}

// Single forward pass over the snapshot's pieces, concatenating the whole
// document into one UTF-16 buffer before searching it as a unit (Phase 5b1:
// this is what makes matches able to span line boundaries - see
// search_service.h's scope comment for the memory-vs-document-size tradeoff
// this implies). Deliberately does NOT use BufferSnapshot::extract() -
// extract() re-walks the full piece list from cursor=0 on every call (its
// own doc comment says so); pieceTextNoCache() is O(1) per piece, the same
// primitive LineIndex::build() already uses to stay O(document length) -
// and, unlike pieceView(), doesn't ALSO permanently retain this transient
// per-call `buffer` copy a second time inside OriginalBuffer's decode
// cache (this scan visits every piece once and moves on - see
// docs/issues/decode_cache_unbounded_growth.md).
//
// search_grep_multi_gb_performance_gap.md: pieceTextStreamed() was tried
// here too (matching LineIndex::build()'s own move off pieceTextNoCache()
// for the same "one huge Original piece" file shape), on the hypothesis
// that one-shot decode of a multi-GB piece would show the same non-linear
// cost LineIndex::build() hit at 10GB. Measured against a real 3GB file
// (standalone probe, 3 repeated runs): pieceTextStreamed() was NOT faster -
// consistently ~1-2s SLOWER (8.4-8.8s vs pieceTextNoCache()'s 6.6-7.8s),
// unlike LineIndex::build()'s case. Reverted per that measurement (CLAUDE.md
// rule 10: don't keep a change with no measured benefit) - this function
// needs the WHOLE concatenated buffer at the end regardless (RE2 must see
// the complete text to match across piece boundaries), so it does not share
// LineIndex::build()'s "look at one character, discard it" shape that made
// streaming a clear win there.
void scanDocument(const re2::RE2& re, const document::BufferSnapshot& snapshot, std::vector<Match>& matches) {
    std::u16string buffer;
    for (const auto& piece : snapshot.pieces()) {
        buffer.append(snapshot.pieceTextNoCache(piece));
    }
    // Common case (no '\r' at all, e.g. any LF-only document): skip
    // stripCrBeforeLf() entirely and take the exact pre-existing code path -
    // no extra buffer copy or position map for documents this fix doesn't
    // need to touch.
    if (buffer.find(u'\r') == std::u16string::npos) {
        findAllInBuffer(re, util::toUtf8WithOffsets(buffer), /*bufferStart=*/0, matches);
        return;
    }
    const CrStrippedText stripped = stripCrBeforeLf(buffer);
    const std::size_t    matchesBeforeThisScan = matches.size();
    findAllInBuffer(re, util::toUtf8WithOffsets(stripped.text), /*bufferStart=*/0, matches);
    // Remap every match this call just appended from stripped-buffer
    // coordinates back to the real document's - see stripCrBeforeLf()'s own
    // comment for why a match's start/end/group boundaries are always valid
    // indices into boundaryToOriginal (0..stripped.text.size() inclusive).
    for (std::size_t i = matchesBeforeThisScan; i < matches.size(); ++i) {
        Match& match  = matches[i];
        match.range.start = stripped.boundaryToOriginal[match.range.start];
        match.range.end   = stripped.boundaryToOriginal[match.range.end];
        for (document::TextRange& group : match.groups) {
            group.start = stripped.boundaryToOriginal[group.start];
            group.end   = stripped.boundaryToOriginal[group.end];
        }
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

    scanDocument(*re, *doc.snapshot(), matches);
    return matches;
}

}  // namespace neomifes::search
