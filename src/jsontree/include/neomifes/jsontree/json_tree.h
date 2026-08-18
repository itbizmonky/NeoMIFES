#pragma once

// JsonNode / parseJsonTree() - JSON document structure, headless (WI-15a,
// Phase 10.3 core). Mirrors neomifes::logmode's split (WI-14a shipped
// LogModel headless before WI-14c wired it to any UI) - no tree pane, no
// EditorSession integration, no folding bridge yet.
//
// Position tracking design (see build_plan.md's WI-15a plan for the full
// rationale): nlohmann::json_sax's callbacks carry no position information
// at all (verified against the vendored nlohmann/json v3.11.3 source and a
// standalone probe before this file was written - every json_sax<T> virtual
// takes only the decoded value, never a line/column/offset). parseJsonTree()
// therefore uses a two-pass approach instead: (1) nlohmann::ordered_json::
// parse() validates the document is well-formed JSON and produces a DOM
// (ordered_json, not the default json, because the default's object_t is a
// std::map and re-sorts keys alphabetically - this project's tree view must
// preserve the source's own key order); (2) a small internal scanner walks
// the SAME already-validated UTF-8 text in lockstep with the DOM to recover
// each node's source position, since it never has to handle malformed input
// itself.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "neomifes/document/text_pos.h"  // document::TextPos

namespace neomifes::document {
class Document;
class BufferSnapshot;
}  // namespace neomifes::document

namespace neomifes::jsontree {

enum class JsonNodeKind : std::uint8_t {
    Object,
    Array,
    String,
    Number,
    Boolean,
    Null,
};

struct JsonNode {
    JsonNodeKind kind = JsonNodeKind::Null;

    // Set iff this node is an Object member (the decoded property name -
    // quotes/escapes already resolved by nlohmann, e.g. u"a\"b" not
    // u"\"a\\\"b\""). Absent for Array elements and the document root -
    // mirrors LogLine::timestamp's optional<T> convention for "this concept
    // doesn't always apply".
    std::optional<std::u16string> key;

    // Leaf value's raw source text for String/Number/Boolean/Null (the
    // exact substring the document itself spells - NOT nlohmann's decoded
    // value, and NOT a re-serialization). This is what lets a Number node
    // keep "1.50" as "1.50" instead of round-tripping through a double and
    // losing the trailing zero, and is deliberately the SAME extraction
    // path (raw substring via startPos/endPos) for every leaf kind rather
    // than special-casing String to carry a decoded value - one path, one
    // set of edge cases to get right. Empty for Object/Array.
    std::u16string text;

    // UTF-16 code-unit offsets into the ORIGINAL document (the same
    // coordinate space as document::Document::offsetToLine() and
    // ui::OutlineItem::targetPos, so a future UI bridge needs no
    // conversion). For an Object member (key.has_value()), spans from the
    // key's opening quote through the value's own end - the whole "key:
    // value" run, matching roadmap's one-row-per-member tree mockup. For an
    // Array element or the document root, spans the value's own token range
    // only. endPos is exclusive.
    document::TextPos startPos = 0;
    document::TextPos endPos   = 0;

    // Non-empty only for Object/Array. Ordered exactly as encountered in
    // the source document (see this file's header comment on why
    // ordered_json, not the default json, is used to build this).
    std::vector<JsonNode> children;

    friend bool operator==(const JsonNode&, const JsonNode&) = default;
};

// Parses snapshot's full text as JSON and returns its structure rooted at
// the document's single top-level value (an object, array, or - per RFC
// 8259 - a bare scalar). Returns std::nullopt for anything that is not
// well-formed JSON, including an empty or whitespace-only document - never
// throws (matches log_pattern_file.cpp/format_detection.cpp's established
// fail-gracefully convention: callers never need to distinguish "empty",
// "malformed", or "not JSON at all", they all collapse to nullopt).
//
// This is the primary entry point (WI-15b) - takes a BufferSnapshot rather
// than a Document so a background thread (jsontree::JsonTreeWorker) can call
// it safely without touching the UI-thread-owned Document. Not a streaming
// optimization the way LogModel::build()'s BufferSnapshot overload is -
// nlohmann::ordered_json::parse() requires the whole document in one
// contiguous buffer regardless, so this overload has the same O(document
// length) shape the Document overload below always had. It exists purely so
// a caller already holding a snapshot (taken once on the UI thread) never
// needs a live Document reference to invoke it.
[[nodiscard]] std::optional<JsonNode> parseJsonTree(const document::BufferSnapshot& snapshot);

// Convenience overload for UI-thread callers that only have a Document at
// hand - snapshots it and delegates to the BufferSnapshot overload above.
[[nodiscard]] std::optional<JsonNode> parseJsonTree(const document::Document& doc);

}  // namespace neomifes::jsontree
