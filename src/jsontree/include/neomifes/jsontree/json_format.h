#pragma once

// formatJsonNode() - renders a JsonNode (WI-15a) tree back into indented
// JSON source text (WI-15d, Phase 10.3 continuation). Pure function, no
// EditorSession/Command dependency - the caller (normal_mode_wiring.cpp)
// decides how to apply the result (an undoable core::ReplaceRangeCommand)
// and this module never needs to know that.

#include <string>

#include "neomifes/jsontree/json_tree.h"

namespace neomifes::jsontree {

// Renders `root` (typically parseJsonTree()'s own result) as JSON text with
// `indentWidth` spaces per nesting level. Leaf nodes (String/Number/
// Boolean/Null) are emitted from their own node.text VERBATIM - not
// re-serialized through a decoded value - so a Number keeps "1.50" as
// "1.50" and a String keeps its original escape choices, exactly like
// json_tree.h's own leaf-text design already avoids for parseJsonTree()'s
// consumers. An Object member's key (node.key, ALREADY DECODED - see
// JsonNode::key's own doc comment) is the one piece that DOES get
// re-encoded here, since json_tree.h deliberately does not retain a key's
// raw quoted/escaped source spelling.
//
// Safe as a straightforward recursive implementation (unlike json_tree.cpp's
// own buildTree(), which is deliberately iterative): every JsonNode this
// function can ever be handed came from parseJsonTree(), which already caps
// nesting at kMaxJsonNestingDepth (200) before a tree is ever built - two
// orders of magnitude below the ~2000-level depth that actually caused a
// real stack overflow (docs/issues/json_tree_worker_deep_nesting_stack_overflow.md).
[[nodiscard]] std::u16string formatJsonNode(const JsonNode& root, int indentWidth = 2);

}  // namespace neomifes::jsontree
