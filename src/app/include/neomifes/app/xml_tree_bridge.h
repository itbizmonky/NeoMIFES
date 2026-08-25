#pragma once

// buildXmlTreeItems - converts an xmltree::XmlNode tree (WI-15f) into a
// ui::OutlineItem tree, the exact role json_tree_bridge.h's
// buildJsonTreeItems() plays for JSON - ui::JsonTreePane (WI-15c) was
// designed from the start to serve either format (see that class's own
// header comment: "the JSON/XML structure tree panel"), operating purely on
// ui::OutlineItem with zero JSON-specific types, so no new pane/item type is
// needed here (WI-15h). Header-only, pure, and free of Windows-SDK includes
// so it stays unit-testable without a live HWND, same rationale as
// json_tree_bridge.h.
//
// Iterative (explicit stack), for the identical reason json_tree_bridge.h's
// buildJsonTreeItems() is: xmltree::XmlNode's depth is bounded only by the
// source XML's own nesting - xml_tree.cpp's buildXmlTree() has no
// kMaxJsonNestingDepth equivalent (WI-15f found tree-sitter-xml itself
// survives deep nesting without an artificial cap, see that module's own
// header comment), so this walk must not add an unguarded recursion of its
// own either.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/ui/outline_pane.h"
#include "neomifes/util/wchar_cast.h"
#include "neomifes/xmltree/xml_tree.h"

namespace neomifes::app {

namespace detail_xml_tree_bridge {

inline constexpr std::size_t kXmlLabelPreviewMaxChars = 60;

// WC_TREEVIEW item text must stay one visual line. Unlike
// jsontree::JsonNode::text (which can never contain a raw, unescaped
// newline - JSON string literals forbid it), xmltree::XmlNode::text for
// Text/Comment/Cdata/ProcessingInstruction routinely spans multiple source
// lines, so every label built from raw node text is piped through this
// single normalization pass: collapse \r/\n/\t to a single space, then
// truncate to kXmlLabelPreviewMaxChars UTF-16 code units with a trailing
// ellipsis. json_tree_bridge.h's formatJsonTreeLabel() never needed this.
[[nodiscard]] inline std::u16string previewOneLine(std::u16string_view raw) {
    std::u16string collapsed;
    collapsed.reserve(raw.size());
    for (const char16_t ch : raw) {
        collapsed += (ch == u'\r' || ch == u'\n' || ch == u'\t') ? u' ' : ch;
    }
    if (collapsed.size() <= kXmlLabelPreviewMaxChars) {
        return collapsed;
    }
    collapsed.resize(kXmlLabelPreviewMaxChars);
    collapsed += u'…';  // "…"
    return collapsed;
}

[[nodiscard]] inline std::u16string digitsOf(std::size_t value) {
    return std::u16string(util::fromWstringView(std::to_wstring(value)));
}

// Element's own opening-tag spelling: "<tagName a=\"1\" b=\"2\">" (or
// ".../>" when selfClosing), matching this project's "raw source spelling,
// not re-derived" convention (XmlAttribute::value is already quote-
// delimited - see xml_tree.h's own field comment - so this reproduces the
// literal source text of each attribute unchanged). A trailing " {N}" child-
// count suffix is appended when the element has content children, the same
// "TVS_HASBUTTONS glyph already conveys expand/collapse, this only conveys
// how many children collapsing hides" convention formatJsonTreeLabel() uses
// for Object/Array.
[[nodiscard]] inline std::u16string formatElementLabel(const xmltree::XmlNode& node) {
    std::u16string label = u"<";
    label += node.tagName;
    for (const auto& attr : node.attributes) {
        label += u' ';
        label += attr.name;
        label += u'=';
        label += attr.value;
    }
    label += node.selfClosing ? u"/>" : u">";
    if (!node.children.empty()) {
        label += u" {";
        label += digitsOf(node.children.size());
        label += u"}";
    }
    return label;
}

// Text nodes between elements in pretty-printed XML are routinely pure
// whitespace (indentation/newlines) - previewOneLine() alone would render
// these as a blank-looking, seemingly-empty tree row. The node itself is
// still kept (not filtered out of the tree - it remains a real, clickable
// jump target and still counts toward its parent's {N} child count), but
// its label gets a placeholder instead of literal blank space.
[[nodiscard]] inline std::u16string formatTextLabel(std::u16string_view text) {
    const std::u16string preview = previewOneLine(text);
    for (const char16_t ch : preview) {
        if (ch != u' ') {
            return preview;
        }
    }
    return u"(whitespace)";
}

[[nodiscard]] inline std::u16string formatXmlTreeLabel(const xmltree::XmlNode& node) {
    switch (node.kind) {
        case xmltree::XmlNodeKind::Element:
            return formatElementLabel(node);
        case xmltree::XmlNodeKind::Text:
            return formatTextLabel(node.text);
        case xmltree::XmlNodeKind::Cdata:
        case xmltree::XmlNodeKind::Comment:
        case xmltree::XmlNodeKind::ProcessingInstruction:
        case xmltree::XmlNodeKind::EntityReference:
            return previewOneLine(node.text);
        case xmltree::XmlNodeKind::Error: {
            std::u16string label = u"[parse error] ";
            label += previewOneLine(node.text);
            return label;
        }
    }
    return previewOneLine(node.text);  // unreachable - every XmlNodeKind enumerator is handled above
}

// One "currently being expanded" source/destination pair in
// buildXmlTreeItems()'s explicit stack - same shape and same safety
// argument as json_tree_bridge.h's JsonTreeFrame (see that struct's own
// comment): `item` always points at a slot inside some OTHER frame's
// ui::OutlineItem::children, stable across `stack`'s own reallocations
// because the depth-first order below never reallocates a vector `item`
// itself lives in until after `item`'s own frame has been fully consumed.
struct XmlTreeFrame {
    const xmltree::XmlNode* source;
    ui::OutlineItem*        item;
    std::size_t              nextChildIndex = 0;
};

}  // namespace detail_xml_tree_bridge

[[nodiscard]] inline ui::OutlineItem buildXmlTreeItems(const xmltree::XmlNode& root) {
    using detail_xml_tree_bridge::formatXmlTreeLabel;
    using detail_xml_tree_bridge::XmlTreeFrame;

    ui::OutlineItem rootItem{.name = formatXmlTreeLabel(root), .targetPos = root.startPos, .children = {}};
    if (root.children.empty()) {
        return rootItem;
    }

    std::vector<XmlTreeFrame> stack;
    stack.push_back(XmlTreeFrame{.source = &root, .item = &rootItem});

    while (!stack.empty()) {
        XmlTreeFrame& top = stack.back();
        if (top.nextChildIndex >= top.source->children.size()) {
            stack.pop_back();
            continue;
        }

        const xmltree::XmlNode& childSource = top.source->children[top.nextChildIndex];
        ++top.nextChildIndex;

        ui::OutlineItem& childItem = top.item->children.emplace_back(ui::OutlineItem{
            .name = formatXmlTreeLabel(childSource), .targetPos = childSource.startPos, .children = {}});

        if (!childSource.children.empty()) {
            stack.push_back(XmlTreeFrame{.source = &childSource, .item = &childItem});
        }
    }

    return rootItem;
}

}  // namespace neomifes::app
