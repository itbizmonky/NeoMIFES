#pragma once

// XmlNode / parseXmlTree() - XML document structure, headless (WI-15f,
// Phase 10.3 core). Mirrors neomifes::jsontree's split (WI-15a shipped
// JsonNode headless before WI-15b wired it to an async worker, before WI-15c
// wired that to any UI) - no tree pane, no EditorSession integration, no
// async worker yet.
//
// Library choice (see build_plan.md's WI-15f plan for the full rationale):
// master_roadmap.md originally sketched pugixml for this role, but pugixml
// exposes NO per-node position API (only an error offset on parse failure),
// which would force reinventing json_tree.cpp's own PositionScanner
// machinery, in a MORE complex form (XML has more syntactic categories than
// JSON). tree-sitter-xml is already vendored (Phase 7r, for syntax
// highlighting) and, because this project's existing tree-sitter call sites
// (outline.cpp/incremental_parser.cpp) already feed it UTF-16LE text via
// ts_parser_parse_string_encoding(..., TSInputEncodingUTF16LE),
// ts_node_start_byte(node)/2 is directly this project's own
// document::TextPos - position tracking comes for free. Zero new dependency,
// zero new ADR (ADR-014 already covers tree-sitter's adoption and its
// error-tolerant parsing philosophy, which is the exact property this file
// relies on - see XmlTree's own comment below).
//
// Grammar shape (tree-sitter-xml v0.7.0) confirmed via BOTH static analysis
// of the vendored node-types.json AND a standalone runtime probe
// (ts_probe_xmltree, run before this file was written, CLAUDE.md rule 3)
// before this file was written - see xml_tree.cpp's own comments for the
// per-node-type detail this header's design rests on.

#include <cstdint>
#include <string>
#include <vector>

#include "neomifes/document/text_pos.h"  // document::TextPos

namespace neomifes::document {
class Document;
class BufferSnapshot;
}  // namespace neomifes::document

namespace neomifes::xmltree {

enum class XmlNodeKind : std::uint8_t {
    Element,
    Text,
    Cdata,
    Comment,
    ProcessingInstruction,
    EntityReference,
    // An opaque leaf standing in for anything tree-sitter's error recovery
    // produced (an ERROR/MISSING node) OR for a document whose root element
    // could not be resolved at all (e.g. an empty document, or a mismatched
    // closing tag that collapses the whole document into one top-level
    // ERROR node - see XmlTree::root's own comment). Never recursed into -
    // more detailed error-subtree modeling is deferred (see WI-15f plan's
    // 非スコープ section).
    Error,
};

struct XmlAttribute {
    std::u16string name = u"";  // raw source text, undecoded (no entity/reference resolution)
    // AttValue's own raw span INCLUDING the surrounding quotes (single or
    // double - both are legal XML). Not decoded: an entity reference inside
    // the value (e.g. "x&amp;y") is kept as-is, matching JsonNode::text's
    // "raw source substring, not a decoded value" convention.
    std::u16string value = u"";
    // Spans the whole `name="value"` run, matching JsonNode::startPos's
    // "object member spans key through value" convention.
    document::TextPos startPos = 0;
    document::TextPos endPos   = 0;  // exclusive

    friend bool operator==(const XmlAttribute&, const XmlAttribute&) = default;
};

struct XmlNode {
    XmlNodeKind kind = XmlNodeKind::Element;

    // Element only. Raw source text (no entity decoding).
    std::u16string tagName = u"";

    // Element only. In source order. Structurally disjoint from `children`
    // (tree-sitter-xml's own grammar never mixes an Attribute into an
    // element's content-child list) - see xml_tree.cpp.
    std::vector<XmlAttribute> attributes = {};

    // Element only. Distinguishes `<foo/>` from `<foo></foo>` - these are
    // structurally different node types in tree-sitter-xml's grammar
    // (EmptyElemTag vs STag+ETag), NOT recoverable from `children.empty()`
    // alone (an explicitly-closed empty element also has no children).
    bool selfClosing = false;

    // Non-Element kinds only. Raw source substring INCLUDING delimiters
    // (e.g. Comment spans "<!--...-->", Cdata spans "<![CDATA[...]]>",
    // ProcessingInstruction spans "<?target data?>", EntityReference spans
    // "&name;" or "&#NN;") - same "delimiters included" convention this
    // project already uses for JsonNode leaf text. For Error, the raw span
    // of whatever tree-sitter node this stands in for (possibly the entire
    // document, e.g. for a mismatched-closing-tag document - see XmlTree's
    // comment).
    std::u16string text = u"";

    // UTF-16 code-unit offsets into the original document (same coordinate
    // space as document::Document::offsetToLine()). endPos is exclusive.
    document::TextPos startPos = 0;
    document::TextPos endPos   = 0;

    // Element only. The element's content children in source order -
    // Text/Cdata/Comment/ProcessingInstruction/EntityReference/Element/
    // Error. Empty for a self-closing element AND for an explicitly-closed-
    // but-empty element (`<foo></foo>` has no `content` node in the grammar
    // at all - see xml_tree.cpp) - the two are distinguished by
    // `selfClosing`, not by this field.
    std::vector<XmlNode> children = {};

    friend bool operator==(const XmlNode&, const XmlNode&) = default;
};

struct XmlTree {
    // Always populated - unlike jsontree::parseJsonTree() (nlohmann is a
    // fail-fast parser, so std::optional<JsonNode> is the natural contract),
    // tree-sitter is a fundamentally error-tolerant parser (ADR-014): it
    // always produces SOME tree, even for malformed or empty input. Forcing
    // JSON's fail-fast contract onto XML would discard that structural
    // information. When the document's root element cannot be resolved
    // (confirmed via probe: an empty document, or input whose top-level
    // parse collapses entirely into one ERROR node - e.g. a mismatched
    // closing tag name), `root.kind == XmlNodeKind::Error`.
    XmlNode root;

    // ts_node_has_error() on the parse's root node, O(1) - true whenever any
    // part of the document (not just the top level) failed to parse
    // cleanly. Exposed for a future "XML: Validate" command (jsontree's
    // validateJson() precedent), not consumed by parseXmlTree() itself.
    bool hasErrors = false;
};

// Parses snapshot's full text as XML and returns its structure. Takes a
// BufferSnapshot (not just a Document) so a background thread
// (xmltree::XmlTreeWorker, WI-15g) can call it safely without touching the
// UI-thread-owned Document - same reasoning as jsontree::parseJsonTree()'s
// BufferSnapshot overload.
[[nodiscard]] XmlTree parseXmlTree(const document::BufferSnapshot& snapshot);

// Convenience overload for UI-thread callers that only have a Document at
// hand - snapshots it and delegates to the BufferSnapshot overload above.
[[nodiscard]] XmlTree parseXmlTree(const document::Document& doc);

}  // namespace neomifes::xmltree
