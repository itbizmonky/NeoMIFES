#pragma once

// reindent - "自動整形" (WI-25): re-indents already-existing text based on
// brace nesting depth, distinct from WI-12's "自動インデント" (which only
// continues the previous line's indentation when Enter is pressed while
// typing). Requirements doc sec.6 lists these as two separate items; this
// one was never implemented and fell out of master_roadmap.md's feature
// matrix entirely - see docs/history/TIMELINE.md's WI-25 entry.
//
// No general brace-nesting/structural API exists anywhere in this codebase
// (neomifes::syntax deliberately keeps tree-sitter internal - see syntax.h's
// header comment). This module instead reuses syntax::parse()'s flat token
// stream: TokenKind::Punctuation tokens whose 1-character text is '{'/'}'
// drive the depth count, while TokenKind::String/Comment token ranges are
// used to skip lines that fall inside a multi-line string literal or block
// comment (so their interior content is never touched). This gives
// per-language-correct results (braces inside a string/comment never affect
// depth) without needing raw tree-sitter node access.

#include <cstdint>
#include <span>
#include <vector>

#include "neomifes/core/edit_commands.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::core {
struct Cursor;
}  // namespace neomifes::core

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::syntax {
enum class Language;
}  // namespace neomifes::syntax

namespace neomifes::app {

// Whether `language` has brace-delimited block structure that a brace-depth
// reindent is meaningful for. Deliberately an explicit allowlist, not "let
// non-brace languages naturally no-op": Python's dict/set literals and
// Shell's `${var}`/brace-expansion/function bodies genuinely produce
// TokenKind::Punctuation '{'/'}' tokens even though neither language's
// PRIMARY block structure (colon+indentation; if/fi-while/do/done-case/esac
// keyword pairs) is brace-based - running the depth-based algorithm on them
// would produce confidently wrong results, not a harmless no-op.
[[nodiscard]] bool supportsReindent(syntax::Language language) noexcept;

struct ReindentLineRange {
    document::LineNumber start;
    document::LineNumber endInclusive;
};

// Turns `cursors` into the set of (merged, ascending, non-overlapping) line
// ranges reindenting should be scoped to. A cursor with no selection
// (position == anchor) contributes nothing. An empty result means "every
// cursor has no selection" - computeReindentEdits() treats that as "the
// whole document," matching json.format's existing "no selection concept,
// always whole document" precedent for the common case (no selection at
// all) while still letting an explicit selection narrow the scope.
[[nodiscard]] std::vector<ReindentLineRange> reindentSelectedLineRanges(
    const document::Document& doc, std::span<const core::Cursor> cursors);

// Computes the leading-whitespace edits needed to bring every in-scope line
// (see reindentSelectedLineRanges() above) to its brace-depth-implied
// indentation. `language` must satisfy supportsReindent() - callers are
// expected to have already checked (same "language resolved by caller"
// contract as extractCurrentOutline(), editor_input.h). `tabWidth` >= 1;
// `insertSpacesForTab` selects spaces vs. a literal tab per indent level
// (this is that setting's first real runtime consumer - core::Settings::
// insertSpacesForTab was otherwise dead code, only round-tripped through
// settings.json, see settings.h's own doc comment).
//
// A line is left completely untouched (no edit emitted, regardless of
// scope) if: it is blank (whitespace-only or empty - trimming trailing
// whitespace is a separate, unimplemented feature, not folded in here), or
// its start falls inside a TokenKind::String/Comment token that began on an
// earlier line (a continuation line of a multi-line string literal or block
// comment - reindenting it would corrupt content the user did not ask to
// change).
[[nodiscard]] std::vector<core::PerCursorEdit> computeReindentEdits(
    const document::Document& doc, syntax::Language language, int tabWidth, bool insertSpacesForTab,
    std::span<const core::Cursor> cursors);

}  // namespace neomifes::app
