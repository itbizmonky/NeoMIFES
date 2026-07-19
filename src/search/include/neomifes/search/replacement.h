#pragma once

// expandReplacementTemplate - Find & Replace's replacement-template
// expansion, Phase 5b2.

#include <string>
#include <string_view>

#include "neomifes/search/search_service.h"

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::search {

// Expands `$0`/`$&` (whole match text), `$1`-`$9` (capture groups 1-9,
// non-participating -> empty string), and `$$` (literal '$') in
// `replacementTemplate`, extracting text from `doc` via `match`. Must be
// called before any edits are applied to `doc` - match/group ranges are
// only valid against the document SearchService::findAll() actually
// searched (Phase 5b2: this function does no cumulative-shift bookkeeping
// of its own; that happens separately inside core::ReplaceAllCommand::execute()
// for the *positions* it writes to, which is unrelated to the *text* this
// function produces).
//
// A `$N` referencing a group the match doesn't have (N > match.groups.size(),
// including any N for a literal-query match with 0 groups) is left as
// literal text ("$" followed by the digit), not an error - mirrors
// SearchService::findAll() treating an incomplete/invalid regex as an empty
// result rather than throwing. An unrecognized escape (e.g. "$x") or a
// trailing "$" at the very end of the template is likewise left literal.
[[nodiscard]] std::u16string expandReplacementTemplate(std::u16string_view       replacementTemplate,
                                                        const document::Document& doc,
                                                        const Match&               match);

}  // namespace neomifes::search
