#pragma once

// SearchService - literal/regex text search (Phase 5a, cross-line matching
// added Phase 5b1).
//
// Scope (see docs/design/detailed_design.md sec.7 and the Phase 5b1 plan):
//   - Synchronous only. async (std::future, per the original detailed_design.md
//     sketch) is deferred until UI wiring (Phase 5b3+) actually needs
//     non-blocking search - introducing it now would be speculative.
//   - Matches may span line boundaries (Phase 5b1): findAll() scans the
//     whole document as a single buffer, so a pattern containing a literal
//     '\n' or a char class like [\s\S] can match across lines. '.' still
//     does not match '\n' by default (RE2's dot_nl option is left at its
//     default false, matching most editors' regex-search convention); a
//     query wanting that must write it explicitly (e.g. "[\s\S]").
//     '^'/'$' still mean "start/end of line", not "start/end of document"
//     (findAll() prefixes every compiled pattern with "(?m)" to preserve
//     this - see search_service.cpp's buildPattern()); use RE2's \A/\z for
//     the true document start/end.
//   - Memory cost scales with document size, not longest line (known
//     tradeoff of the whole-buffer scan strategy above - see
//     docs/issues/ for anything filed against this once it's profiled
//     against large real files). Piece-chunked/parallel scanning
//     (detailed_design.md sec.7.3) remains unimplemented.
//   - One regex engine, one code path, for both literal and pattern search
//     (RE2, ADR-002) - a literal query is compiled as an escaped RE2 pattern
//     rather than hand-rolled string search, so there is no second
//     algorithm to maintain until benchmarking shows RE2 itself is the
//     bottleneck (detailed_design.md sec.7.2's Boyer-Moore-Horspool+SIMD
//     path remains a documented future optimization, not implemented here).

#include <cstdint>
#include <string>
#include <vector>

#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::search {

struct Query {
    std::u16string pattern;
    bool caseSensitive = true;
    bool wholeWord     = false;
    bool regex         = false;
};

struct Match {
    document::TextRange range;
};

class SearchService {
public:
    // Finds every non-overlapping match of `query` in `doc`, in document
    // order; matches may span line boundaries (see the Scope comment
    // above). Returns an empty vector for an empty pattern or a regex
    // pattern that fails to compile (an interactively typed, not-yet-
    // complete regex is an expected condition once Find UI wiring lands in
    // Phase 5b3+, not an error worth throwing for).
    //
    // static: this class currently holds no instance state (a future
    // worker-pool-backed async findAll/grep, per detailed_design.md sec.7's
    // original sketch, would change that - not introduced now, YAGNI).
    [[nodiscard]] static std::vector<Match> findAll(const document::Document& doc,
                                                     const Query&              query);
};

}  // namespace neomifes::search
