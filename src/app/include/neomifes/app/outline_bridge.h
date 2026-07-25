#pragma once

// buildOutlineItems - converts a syntax::OutlineNode tree (Phase 7f) into a
// ui::OutlineItem tree (Phase 7g). Header-only, pure, and free of Windows-SDK
// includes so it stays unit-testable without a live HWND, mirroring
// app::formatGrepResultRow()'s rationale - this lives under src/app/ rather
// than src/ui/ because it depends on neomifes::syntax, and ui:: is
// deliberately kept free of that dependency (see outline_pane.h's class
// comment).

#include <vector>

#include "neomifes/syntax/outline.h"
#include "neomifes/ui/outline_pane.h"

namespace neomifes::app {

// Recursive: safe here even though outline.cpp's own AST walk
// (walkForOutline()) had to become iterative to avoid misc-no-recursion.
// The two walks operate on different trees - this one only ever recurses
// through OutlineNode::children, which extractOutline() has already
// collapsed down to symbol-definition nesting (namespace > class > method),
// not the raw, source-file-dependent AST depth that made the other walk
// unbounded.
[[nodiscard]] inline std::vector<ui::OutlineItem> buildOutlineItems(
    const std::vector<syntax::OutlineNode>& nodes) {
    std::vector<ui::OutlineItem> result;
    result.reserve(nodes.size());
    for (const auto& node : nodes) {
        result.push_back(ui::OutlineItem{
            .name      = node.name,
            .targetPos = node.pos,
            .children  = buildOutlineItems(node.children),
        });
    }
    return result;
}

}  // namespace neomifes::app
