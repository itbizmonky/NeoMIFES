#pragma once

// GrepService - synchronous multi-root/multi-file search, Phase 5c1.
//
// Scope:
//   - Synchronous only, returns a flat std::vector<GrepMatch> (not a
//     streaming std::function<void(GrepMatch)> callback, despite
//     master_roadmap.md sec.5.5's original sketch, and no worker-pool
//     threading) - mirrors SearchService::findAll()'s own documented
//     simplification (search_service.h). No threading exists anywhere in
//     this codebase yet; introducing it here would be the first, and
//     nothing downstream (there is no UI wiring in 5c1) needs it yet.
//     Revisit once a results-pane phase actually needs progressive results.
//   - No contextLines (surrounding lines of context): no consumer renders
//     them yet; adding the field now would be speculative (CLAUDE.md
//     rule 3). Add it to GrepQuery when a results-pane phase needs it.
//   - Reuses document::loadUtf8File() per candidate file and
//     search::SearchService::findAll() per loaded file, both unchanged -
//     zero modification to either as part of this phase.

#include <filesystem>
#include <string>
#include <vector>

#include "neomifes/document/text_pos.h"
#include "neomifes/search/search_service.h"

namespace neomifes::search {

// includeGlobs/excludeGlobs match against the candidate file's *filename*
// only (e.g. "foo.cpp", not "src/foo.cpp") via util::globMatch() -
// path-component globs, gitignore-style "**", and directory-scoped globs
// are all out of scope for 5c1 (no consumer needs them; see
// util/glob_match.h). An empty includeGlobs means "every filename passes
// the include filter" - the sensible default for "search everything under
// these roots" when the caller hasn't narrowed by file type. excludeGlobs
// is independent of includeGlobs and always applies (empty means "exclude
// nothing"); a filename matching both wins as excluded (see grep_service.cpp).
struct GrepQuery {
    std::vector<std::filesystem::path> roots;
    std::vector<std::u16string>        includeGlobs;
    std::vector<std::u16string>        excludeGlobs;
    Query                               query;
};

// One match, line-oriented for display (a future results-pane conventionally
// lists "file:line: <line text>").
//
// columnRange is relative to lineText's start (0 == its first UTF-16 code
// unit), NOT a document::TextPos into the source file's Document the way
// search::Match::range is - GrepService's per-file Document is transient
// (loaded, searched, then discarded), so an absolute document offset would
// be useless to a later consumer without also re-deriving the line's start
// offset; a line-relative range is self-contained and indexes directly into
// lineText. If a match spans multiple lines (SearchService supports this
// since Phase 5b1), columnRange.end is clamped to lineText.size() so it is
// always a valid sub-range of lineText - the match is still reported (at
// its start line), just without indicating it continues past this line;
// showing further lines is a "context lines" concern, deliberately
// deferred (see grep_service.h's file header).
//
// Deliberately omits search::Match::groups (capture groups): no 5c1
// consumer needs them (that would be a "replace within Grep results"
// feature, not in scope here). Add a groups field, converted to
// line-relative the same way columnRange is, if/when that lands.
struct GrepMatch {
    std::filesystem::path path;
    document::LineNumber   line = 0;  // 0-based
    document::TextRange    columnRange;
    std::u16string          lineText;  // trailing '\n' / '\r' stripped
};

class GrepService {
public:
    // Walks every root in query.roots, applies the include/exclude filename
    // filters, loads each surviving candidate via document::loadUtf8File(),
    // and runs SearchService::findAll() against it. A root that doesn't
    // exist, a file that fails to load (any LoadError, including
    // InvalidUtf8 - i.e. a binary file), or a directory-traversal error
    // partway through a root is skipped rather than aborting the whole
    // query - matches how grep/ripgrep skip unreadable/binary files and
    // keep going, and avoids one bad root blanking out another root's
    // results.
    //
    // static: same "no instance state yet" reasoning as
    // SearchService::findAll (search_service.h) - not introduced now, YAGNI.
    [[nodiscard]] static std::vector<GrepMatch> findAll(const GrepQuery& query);
};

}  // namespace neomifes::search
