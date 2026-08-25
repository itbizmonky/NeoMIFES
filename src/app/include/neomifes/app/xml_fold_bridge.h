#pragma once

// buildXmlFoldRegions - flattens an xmltree::XmlNode tree (WI-15f) into a
// flat list of core::FoldRegion (Phase 7i), one entry per non-self-closing
// Element node that spans more than one line - the XML counterpart of
// json_fold_bridge.h's buildJsonFoldRegions() (one entry per Object/Array).
// Header-only, pure, and free of Windows-SDK includes so it stays
// unit-testable, same rationale as json_fold_bridge.h - lives under
// src/app/ rather than src/core/ because it depends on neomifes::xmltree.
//
// Iterative (explicit stack), for the same reason json_fold_bridge.h's
// buildJsonFoldRegions() is: xmltree::XmlNode's nesting depth is bounded
// only by the source XML's own structure (see xml_tree_bridge.h's header
// comment on this same hazard). Produces a flat list, not a mirrored tree,
// so there is no parent/child pointer stability concern - a straightforward
// stack-based DFS, same shape as buildJsonFoldRegions().

#include <vector>

#include "neomifes/core/folding_model.h"
#include "neomifes/document/document.h"
#include "neomifes/xmltree/xml_tree.h"

namespace neomifes::app {

// Text/Cdata/Comment/ProcessingInstruction/EntityReference/Error leaves
// never produce a region (nothing to fold), the same "leaves never fold"
// rule buildJsonFoldRegions() applies to String/Number/Boolean/Null -
// accepted for v1 even though XML Comment/Cdata content can genuinely be
// long and multi-line (WI-15h's plan explicitly scopes this out; see that
// plan's non-scope section). A self-closing element is explicitly excluded
// regardless of its own line span (`<foo\n  a="1"/>` has no content to
// hide) - unlike Object/Array, which have no analogous "self-closing"
// concept, so this guard has no JSON-side counterpart. Regions that fit on
// a single line (endLine <= headerLine) are excluded, matching
// buildJsonFoldRegions()'s own exclusion.
[[nodiscard]] inline std::vector<core::FoldRegion> buildXmlFoldRegions(const xmltree::XmlNode& root,
                                                                         const document::Document& document) {
    std::vector<core::FoldRegion>       result;
    std::vector<const xmltree::XmlNode*> stack;
    stack.push_back(&root);

    while (!stack.empty()) {
        const xmltree::XmlNode* node = stack.back();
        stack.pop_back();

        if (node->kind == xmltree::XmlNodeKind::Element && !node->selfClosing) {
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
