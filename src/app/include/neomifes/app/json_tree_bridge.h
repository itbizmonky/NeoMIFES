#pragma once

// buildJsonTreeItems - converts a jsontree::JsonNode tree (WI-15a) into a
// ui::OutlineItem tree (Phase 7g's JsonTreePane reuses OutlinePane's own
// item type - see json_tree_pane.h's class comment for why no JSON-specific
// item struct exists). Header-only, pure, and free of Windows-SDK includes
// so it stays unit-testable without a live HWND, mirroring
// outline_bridge.h's buildOutlineItems() rationale - this lives under
// src/app/ rather than src/ui/ because it depends on neomifes::jsontree, and
// ui:: is deliberately kept free of that dependency.
//
// Iterative (explicit stack), NOT modeled on outline_bridge.h's
// buildOutlineItems() (which recurses freely because syntax::OutlineNode's
// depth is bounded by symbol-definition nesting). jsontree::JsonNode's depth
// is bounded only by the source JSON's own nesting - exactly the hazard
// json_tree.cpp's kMaxJsonNestingDepth guard exists for on the parse side -
// so this walk must not add a second, unguarded recursion of its own even
// though today's guard keeps any JsonNode this function ever sees shallow.

#include <string>
#include <vector>

#include "neomifes/jsontree/json_tree.h"
#include "neomifes/ui/outline_pane.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

namespace detail_json_tree_bridge {

// "key: " prefix (or empty for array elements / the document root, which
// have no key - see JsonNode::key's own comment). Object/Array labels also
// carry a "{N}"/"[N]" child-count suffix (WC_TREEVIEW's own TVS_HASBUTTONS
// glyph already conveys expand/collapse state, so this only needs to convey
// how many children collapsing hides). Leaf labels use JsonNode::text as-is
// - already the exact raw source spelling (quotes included for String),
// matching this project's "one extraction path for every leaf kind" choice
// documented on json_tree.h's JsonNode::text field.
[[nodiscard]] inline std::u16string formatJsonTreeLabel(const jsontree::JsonNode& node) {
    std::u16string label;
    if (node.key.has_value()) {
        label += *node.key;
        label += u": ";
    }

    switch (node.kind) {
        case jsontree::JsonNodeKind::Object:
        case jsontree::JsonNodeKind::Array: {
            const bool            isObject = (node.kind == jsontree::JsonNodeKind::Object);
            const std::wstring    countWide = std::to_wstring(node.children.size());
            const std::u16string  countText(util::fromWstringView(countWide));
            label += (isObject ? u'{' : u'[');
            label += countText;
            label += (isObject ? u'}' : u']');
            return label;
        }
        case jsontree::JsonNodeKind::String:
        case jsontree::JsonNodeKind::Number:
        case jsontree::JsonNodeKind::Boolean:
        case jsontree::JsonNodeKind::Null:
            label += node.text;
            return label;
    }
    return label;  // unreachable - every JsonNodeKind enumerator is handled above
}

// One "currently being expanded" source/destination pair in
// buildJsonTreeItems()'s explicit stack. `item` points at the ui::OutlineItem
// this frame is filling in - always an element inside SOME OTHER
// ui::OutlineItem's `children` vector (or the root item owned by the
// caller), never inside `stack` itself, so it stays valid across `stack`'s
// own reallocations. Safe for the same reason json_tree.cpp's own
// PendingContainer is: by the time a parent frame appends `item`'s next
// SIBLING (the only other operation that could reallocate the vector `item`
// lives in), `item`'s own frame has already been fully consumed and popped,
// per the depth-first order the while-loop below enforces.
struct JsonTreeFrame {
    const jsontree::JsonNode* source;
    ui::OutlineItem*          item;
    std::size_t               nextChildIndex = 0;
};

}  // namespace detail_json_tree_bridge

[[nodiscard]] inline ui::OutlineItem buildJsonTreeItems(const jsontree::JsonNode& root) {
    using detail_json_tree_bridge::formatJsonTreeLabel;
    using detail_json_tree_bridge::JsonTreeFrame;

    ui::OutlineItem rootItem{.name = formatJsonTreeLabel(root), .targetPos = root.startPos, .children = {}};
    if (root.children.empty()) {
        return rootItem;
    }

    std::vector<JsonTreeFrame> stack;
    stack.push_back(JsonTreeFrame{.source = &root, .item = &rootItem});

    while (!stack.empty()) {
        JsonTreeFrame& top = stack.back();
        if (top.nextChildIndex >= top.source->children.size()) {
            stack.pop_back();
            continue;
        }

        const jsontree::JsonNode& childSource = top.source->children[top.nextChildIndex];
        ++top.nextChildIndex;

        ui::OutlineItem& childItem = top.item->children.emplace_back(
            ui::OutlineItem{.name = formatJsonTreeLabel(childSource), .targetPos = childSource.startPos, .children = {}});

        if (!childSource.children.empty()) {
            stack.push_back(JsonTreeFrame{.source = &childSource, .item = &childItem});
        }
    }

    return rootItem;
}

}  // namespace neomifes::app
