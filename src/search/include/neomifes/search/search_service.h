#pragma once

// SearchService - literal/regex text search (Phase 5a).
//
// Scope (see docs/design/detailed_design.md sec.7 and the Phase 5a plan):
//   - Synchronous only. async (std::future, per the original detailed_design.md
//     sketch) is deferred until UI wiring (Phase 5b+) actually needs
//     non-blocking search - introducing it now would be speculative.
//   - Single-line matches only: a pattern that would match across a '\n' is
//     not found. Mirrors how Ctrl+arrow word movement (Phase 4b6b) started
//     single-line before being generalized (Phase 4b7b); cross-line search
//     is an explicit Phase 5b+ candidate, not attempted here.
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
    // Finds every non-overlapping match of `query` in `doc`, scanning line
    // by line in document order. Returns an empty vector for an empty
    // pattern or a regex pattern that fails to compile (an interactively
    // typed, not-yet-complete regex is an expected condition once Find UI
    // wiring lands in Phase 5b+, not an error worth throwing for).
    //
    // static: this class currently holds no instance state (a future
    // worker-pool-backed async findAll/grep, per detailed_design.md sec.7's
    // original sketch, would change that - not introduced now, YAGNI).
    [[nodiscard]] static std::vector<Match> findAll(const document::Document& doc,
                                                     const Query&              query);
};

}  // namespace neomifes::search
