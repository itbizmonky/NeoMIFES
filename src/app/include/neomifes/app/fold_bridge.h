#pragma once

// buildFoldRegions - flattens a syntax::OutlineNode tree (Phase 7f) into a
// flat list of core::FoldRegion (Phase 7i), one entry per symbol definition
// at every nesting level (not just top-level). Header-only, pure, and free
// of Windows-SDK includes so it stays unit-testable, mirroring
// outline_bridge.h's buildOutlineItems() rationale - this lives under
// src/app/ rather than src/core/ because it depends on neomifes::syntax,
// and core:: is deliberately kept free of that dependency (same "independent
// engines" principle FoldingModel's own header comment states).
//
// Iterative (explicit stack), not recursive - src/.clang-tidy's
// WarningsAsErrors: '*' makes misc-no-recursion a hard error for any
// self-recursive function under src/, regardless of whether the tree being
// walked is actually bounded-depth (Phase 7f's walkForOutline() and Phase
// 7h's findBreadcrumbPath() both hit this; this function is written
// iteratively from the start to avoid a third round of the same rewrite).

#include <vector>

#include "neomifes/core/folding_model.h"
#include "neomifes/document/document.h"
#include "neomifes/syntax/outline.h"

namespace neomifes::app {

// Symbols whose definition fits on a single line (endLineInclusive <=
// headerLine) are excluded - there is nothing to hide by folding them.
[[nodiscard]] inline std::vector<core::FoldRegion> buildFoldRegions(
    const std::vector<syntax::OutlineNode>& nodes, const document::Document& document) {
    std::vector<core::FoldRegion> result;
    std::vector<const syntax::OutlineNode*> stack;
    stack.reserve(nodes.size());
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        stack.push_back(&*it);
    }
    while (!stack.empty()) {
        const syntax::OutlineNode* node = stack.back();
        stack.pop_back();

        const document::LineNumber headerLine = document.offsetToLine(node->pos);
        const document::LineNumber endLine =
            document.offsetToLine(node->containingRange.end > 0 ? node->containingRange.end - 1 : 0);
        if (endLine > headerLine) {
            result.push_back(core::FoldRegion{
                .headerLine       = headerLine,
                .endLineInclusive = endLine,
                .folded           = false,
            });
        }
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            stack.push_back(&*it);
        }
    }
    return result;
}

}  // namespace neomifes::app
