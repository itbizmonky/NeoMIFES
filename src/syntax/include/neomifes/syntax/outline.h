#pragma once

// neomifes::syntax::extractOutline() - headless extraction of a symbol tree
// (functions/classes/structs/namespaces) from source text, built on
// tree-sitter (Phase 7f). Independent of syntax.h's parseCpp()/parsePython()/
// parse() - a second, separate parse (see this header's rationale in the
// Phase 7f plan's Context section: outline extraction is low-frequency,
// unlike per-edit token coloring, so tree-sharing is not worth the added
// complexity without a benchmark showing it matters, CLAUDE.md rule 10).
//
// No Document/RenderPipeline/UI dependency - outline_pane.h (WC_TREEVIEW) and
// main.cpp wiring are deliberately deferred to a later sub-phase (see
// master_roadmap.md sec.7 "実装後の確定事項").

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/document/text_pos.h"
#include "neomifes/syntax/syntax.h"

namespace neomifes::syntax {

// Deliberately NOT syntax::TokenKind (see this header's Phase 7f plan
// Context section) - TokenKind classifies individual leaf tokens for text
// coloring, while this classifies compound definition nodes for the outline
// tree. Reusing TokenKind would mean adding Function/Class/Namespace to it,
// which Phase 7a explicitly deferred because leaf-level classification alone
// cannot distinguish a function *call* from a function *definition* - that
// problem does not exist here, since extractOutline() only ever visits
// definition nodes, but conflating the two enums would still couple two
// unrelated concerns.
enum class SymbolKind : std::uint8_t { Function, Class, Struct, Namespace };

struct OutlineNode {
    std::u16string           name;
    document::TextPos        pos;              // start of the `name` identifier itself
    document::TextRange      containingRange;  // full span of the definition (for future Breadcrumb reverse lookup)
    SymbolKind                symbolKind;
    std::vector<OutlineNode> children;         // nested definitions (member functions, inner classes, closures, ...)
};

// Parses `text` as `language` and returns the top-level symbol definitions,
// each recursively populated with its own nested definitions. Empty text, or
// text containing no recognized definitions, yields an empty vector - same
// "tree-sitter never fails to parse" contract as parseCpp()/parsePython()
// (syntax.h), so malformed input still returns whatever definitions could be
// recognized rather than an error.
[[nodiscard]] std::vector<OutlineNode> extractOutline(std::u16string_view text, Language language);

// Returns the chain of symbols containing `pos`, outermost first, using the
// tree already produced by extractOutline() (this is the "future Breadcrumb
// reverse lookup" OutlineNode::containingRange's doc comment refers to).
// containingRange is [start, end) - same half-open convention as
// document::TextRange elsewhere. Empty result if `pos` falls outside every
// top-level node. Iterative (a plain descend-one-level-at-a-time loop, not a
// self-call) - this file's src/.clang-tidy sets WarningsAsErrors: '*', and
// misc-no-recursion flags any self-recursive function regardless of provable
// depth bounds, the same check that made outline.cpp's walkForOutline()
// iterative in Phase 7f. The tree itself is still shallow (symbol-definition
// nesting only, not raw tree-sitter AST depth), so this is a lint-driven
// implementation choice, not evidence the recursive form would have been
// unsafe.
[[nodiscard]] std::vector<const OutlineNode*> findBreadcrumbPath(document::TextPos pos,
                                                                  const std::vector<OutlineNode>& nodes);

}  // namespace neomifes::syntax
