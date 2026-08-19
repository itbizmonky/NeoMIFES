#pragma once

// buildJsonFoldRegions - flattens a jsontree::JsonNode tree (WI-15a) into a
// flat list of core::FoldRegion (Phase 7i), one entry per Object/Array node
// that spans more than one line. Header-only, pure, and free of Windows-SDK
// includes so it stays unit-testable, mirroring fold_bridge.h's
// buildFoldRegions() rationale - this lives under src/app/ rather than
// src/core/ because it depends on neomifes::jsontree, and core:: is
// deliberately kept free of that dependency (same "independent engines"
// principle FoldingModel's own header comment states).
//
// Iterative (explicit stack), for the same reason fold_bridge.h's
// buildFoldRegions() is: jsontree::JsonNode's nesting depth is bounded only
// by the source JSON's own structure (see json_tree_bridge.h's header
// comment on this same hazard). Unlike buildJsonTreeItems() this produces a
// flat list, not a mirrored tree, so there is no parent/child pointer
// stability concern here - just a straightforward stack-based DFS, same
// shape as buildFoldRegions().

#include <vector>

#include "neomifes/core/folding_model.h"
#include "neomifes/document/document.h"
#include "neomifes/jsontree/json_tree.h"

namespace neomifes::app {

// String/Number/Boolean/Null leaves never produce a region (nothing to
// fold). Regions that fit on a single line (endLine <= headerLine) are
// excluded, matching buildFoldRegions()'s own exclusion - there is nothing
// to hide by folding them.
[[nodiscard]] inline std::vector<core::FoldRegion> buildJsonFoldRegions(const jsontree::JsonNode&  root,
                                                                         const document::Document& document) {
    std::vector<core::FoldRegion>          result;
    std::vector<const jsontree::JsonNode*> stack;
    stack.push_back(&root);

    while (!stack.empty()) {
        const jsontree::JsonNode* node = stack.back();
        stack.pop_back();

        if (node->kind == jsontree::JsonNodeKind::Object || node->kind == jsontree::JsonNodeKind::Array) {
            const document::LineNumber headerLine = document.offsetToLine(node->startPos);
            const document::LineNumber endLine = document.offsetToLine(node->endPos > 0 ? node->endPos - 1 : 0);
            if (endLine > headerLine) {
                result.push_back(core::FoldRegion{.headerLine = headerLine, .endLineInclusive = endLine, .folded = false});
            }
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            stack.push_back(&*it);
        }
    }

    return result;
}

}  // namespace neomifes::app
