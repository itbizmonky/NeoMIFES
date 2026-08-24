#include "neomifes/xmltree/xml_tree.h"

#include <tree_sitter/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_xml(void);

namespace neomifes::xmltree {

namespace {

using TSParserPtr = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using TSTreePtr   = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;

[[nodiscard]] document::TextPos toTextPos(std::uint32_t byteOffset) noexcept {
    // tree-sitter byte offsets are always even for TSInputEncodingUTF16LE
    // input (2 bytes per UTF-16 code unit) - same fact syntax_internal.h's
    // appendLeafToken() already relies on (ADR-014).
    return static_cast<document::TextPos>(byteOffset / 2);
}

[[nodiscard]] std::u16string sliceNode(TSNode node, const std::u16string& buffer) {
    const document::TextPos start = toTextPos(ts_node_start_byte(node));
    const document::TextPos end   = toTextPos(ts_node_end_byte(node));
    return buffer.substr(start, end - start);
}

// Fills `out` (an Attribute node's own Name + AttValue - see vendored
// node-types.json: Attribute's children are exactly {Name, AttValue}, both
// positional, no field names). AttValue is NOT walked further - the probe
// confirmed it has no child representing its literal quoted text (only an
// optional "content" field of _Reference (EntityRef/CharRef) children when
// the value contains an entity/character reference) - so the value is taken
// as AttValue's own raw span (quotes included), matching this file's
// "delimiters included" convention rather than reassembling text from
// children.
[[nodiscard]] XmlAttribute buildAttribute(TSNode attrNode, const std::u16string& buffer) {
    XmlAttribute attr;
    attr.startPos = toTextPos(ts_node_start_byte(attrNode));
    attr.endPos   = toTextPos(ts_node_end_byte(attrNode));

    const std::uint32_t count = ts_node_child_count(attrNode);
    for (std::uint32_t i = 0; i < count; ++i) {
        const TSNode           child = ts_node_child(attrNode, i);
        const std::string_view type  = ts_node_type(child);
        if (type == "Name") {
            attr.name = sliceNode(child, buffer);
        } else if (type == "AttValue") {
            attr.value = sliceNode(child, buffer);
        }
    }
    return attr;
}

// Fills `out`'s Element-level fields (kind/tagName/attributes/selfClosing/
// startPos/endPos) from `elementNode` - does NOT touch `out.children`.
// Returns the element's "content" child node if present, or a null TSNode
// (ts_node_is_null()) otherwise - "content" is optional in the grammar
// (`O($.content)`): a self-closing element (EmptyElemTag) never has one, and
// neither does an explicitly-closed-but-empty element (`<foo></foo>` is
// STag+ETag with no content node at all, not an empty content node - probe-
// confirmed). Not recursive - never walks into a nested "element".
[[nodiscard]] TSNode fillElementShallow(TSNode elementNode, const std::u16string& buffer, XmlNode& out) {
    out.kind      = XmlNodeKind::Element;
    out.startPos  = toTextPos(ts_node_start_byte(elementNode));
    out.endPos    = toTextPos(ts_node_end_byte(elementNode));

    TSNode contentNode{};  // zero-initialized - ts_node_is_null() true until assigned below

    // element's children are exactly {STag, EmptyElemTag, ETag, content},
    // all positional (node-types.json has no "fields" for `element`). ETag
    // (the closing tag's own Name) carries no information this struct
    // needs - element's own span already covers it.
    const std::uint32_t count = ts_node_child_count(elementNode);
    for (std::uint32_t i = 0; i < count; ++i) {
        const TSNode            child = ts_node_child(elementNode, i);
        const std::string_view  type  = ts_node_type(child);
        if (type == "STag" || type == "EmptyElemTag") {
            out.selfClosing = (type == "EmptyElemTag");
            // STag/EmptyElemTag's own children are {Name, Attribute*}
            // (node-types.json), Name always first and exactly once.
            const std::uint32_t tagChildCount = ts_node_child_count(child);
            for (std::uint32_t j = 0; j < tagChildCount; ++j) {
                const TSNode            tagChild = ts_node_child(child, j);
                const std::string_view  tagType  = ts_node_type(tagChild);
                if (tagType == "Name") {
                    out.tagName = sliceNode(tagChild, buffer);
                } else if (tagType == "Attribute") {
                    out.attributes.push_back(buildAttribute(tagChild, buffer));
                }
            }
        } else if (type == "content") {
            contentNode = child;
        }
    }
    return contentNode;
}

[[nodiscard]] XmlNodeKind contentChildKind(std::string_view type) noexcept {
    // content's children are exactly {CDSect, CharData, Comment, PI,
    // _Reference (CharRef|EntityRef), element} per node-types.json. Any
    // other type reaching here is tree-sitter error-recovery output
    // (ERROR/MISSING) - falls through to Error, never crashes.
    if (type == "CharData") {
        return XmlNodeKind::Text;
    }
    if (type == "CDSect") {
        return XmlNodeKind::Cdata;
    }
    if (type == "Comment") {
        return XmlNodeKind::Comment;
    }
    if (type == "PI") {
        return XmlNodeKind::ProcessingInstruction;
    }
    if (type == "CharRef" || type == "EntityRef") {
        return XmlNodeKind::EntityReference;
    }
    return XmlNodeKind::Error;
}

// One "currently being consumed" content-child-list frame in the explicit
// stack buildContentTree() walks below (misc-no-recursion - a well-formed
// but deeply nested document produces a genuinely deep TSNode tree, unlike
// the flat ERROR node malformed/pathological input collapses into per the
// WI-15f plan's probe results, so a recursive walk here would carry the same
// stack-overflow risk json_tree.cpp's buildTree() was written to avoid).
// `target` points at the XmlNode (always an Element) this frame appends
// into - stable across `stack`'s own reallocations for the same reason
// json_tree.cpp's PendingContainer::node is (see that file's comment): it is
// always a slot inside some OTHER frame's `target->children`, and by the
// time that slot's own children are being filled, the frame that created it
// has already had every sibling before it appended, so no further
// reallocation of ITS OWN children vector's earlier elements can occur.
struct ContentFrame {
    TSNode   contentNode;
    XmlNode* target;
    std::uint32_t childIndex = 0;
};

[[nodiscard]] XmlNode buildXmlTree(TSNode rootElementNode, const std::u16string& buffer) {
    XmlNode root;
    const TSNode rootContent = fillElementShallow(rootElementNode, buffer, root);

    std::vector<ContentFrame> stack;
    if (!ts_node_is_null(rootContent)) {
        stack.push_back(ContentFrame{.contentNode = rootContent, .target = &root});
    }

    while (!stack.empty()) {
        ContentFrame& top = stack.back();
        if (top.childIndex >= ts_node_child_count(top.contentNode)) {
            stack.pop_back();
            continue;
        }

        // Capture everything needed from `top` NOW - the "element" branch
        // below may push_back onto `stack`, which can reallocate and
        // invalidate `top` (a reference into it). See ContentFrame's own
        // comment and json_tree.cpp's consumeNextChild() for the same
        // idiom.
        const TSNode   child  = ts_node_child(top.contentNode, top.childIndex);
        XmlNode* const target = top.target;
        ++top.childIndex;  // `top` must not be used below this line.

        const std::string_view type = ts_node_type(child);
        if (type == "element") {
            XmlNode& childNode = target->children.emplace_back();
            const TSNode childContent = fillElementShallow(child, buffer, childNode);
            if (!ts_node_is_null(childContent)) {
                stack.push_back(ContentFrame{.contentNode = childContent, .target = &childNode});
            }
            continue;
        }

        XmlNode& leaf  = target->children.emplace_back();
        leaf.kind      = contentChildKind(type);
        leaf.startPos  = toTextPos(ts_node_start_byte(child));
        leaf.endPos    = toTextPos(ts_node_end_byte(child));
        leaf.text      = sliceNode(child, buffer);
    }

    return root;
}

[[nodiscard]] XmlNode makeErrorSentinel(TSNode node, const std::u16string& buffer) {
    XmlNode out;
    out.kind     = XmlNodeKind::Error;
    out.startPos = toTextPos(ts_node_start_byte(node));
    out.endPos   = toTextPos(ts_node_end_byte(node));
    out.text     = sliceNode(node, buffer);
    return out;
}

[[nodiscard]] std::u16string bufferFromSnapshot(const document::BufferSnapshot& snapshot) {
    std::u16string buffer;
    for (const auto& piece : snapshot.pieces()) {
        buffer.append(snapshot.pieceView(piece));
    }
    return buffer;
}

}  // namespace

XmlTree parseXmlTree(const document::BufferSnapshot& snapshot) {
    const std::u16string buffer = bufferFromSnapshot(snapshot);

    const TSParserPtr parser(ts_parser_new(), &ts_parser_delete);
    ts_parser_set_language(parser.get(), tree_sitter_xml());

    const char* bytes  = reinterpret_cast<const char*>(buffer.data());
    const auto  length = static_cast<std::uint32_t>(buffer.size() * sizeof(char16_t));
    const TSTreePtr tree(
        ts_parser_parse_string_encoding(parser.get(), nullptr, bytes, length, TSInputEncodingUTF16LE),
        &ts_tree_delete);
    const TSNode rootNode = ts_tree_root_node(tree.get());

    XmlTree result;
    result.hasErrors = ts_node_has_error(rootNode);

    // "root" is a required field on a genuine `document` node (per
    // node-types.json) - probe-confirmed null both for an empty document
    // and for input that collapses entirely into one top-level ERROR node
    // (e.g. a mismatched closing tag name), so this single null check
    // uniformly covers both "no content to parse" cases without needing a
    // separate ts_node_type(rootNode) == "ERROR" check.
    const TSNode docRoot = ts_node_child_by_field_name(rootNode, "root", 4);
    if (ts_node_is_null(docRoot)) {
        result.root      = makeErrorSentinel(rootNode, buffer);
        result.hasErrors = true;
        return result;
    }

    result.root = buildXmlTree(docRoot, buffer);
    return result;
}

XmlTree parseXmlTree(const document::Document& doc) {
    return parseXmlTree(*doc.snapshot());
}

}  // namespace neomifes::xmltree
