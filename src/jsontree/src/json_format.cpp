#include "neomifes/jsontree/json_format.h"

#include <cstddef>
#include <string_view>

namespace neomifes::jsontree {

namespace {

void appendIndent(std::u16string& out, int level, int indentWidth) {
    out.append(static_cast<std::size_t>(level) * static_cast<std::size_t>(indentWidth), u' ');
}

// RFC 8259 §7's minimal escaping requirement: only '"', '\', and control
// characters (0x00-0x1F) must be escaped inside a JSON string - every other
// Unicode character (including non-ASCII) is valid literal content and is
// passed through unchanged, no "\uXXXX" re-encoding attempted. Only used
// for Object member keys here (see json_format.h's own comment on why leaf
// VALUES never need this - their node.text is already raw, valid JSON
// source text, quotes and escapes included).
[[nodiscard]] std::u16string escapeJsonString(std::u16string_view decoded) {
    constexpr char16_t kHexDigits[] = u"0123456789abcdef";

    std::u16string out;
    out.reserve(decoded.size() + 2);
    out.push_back(u'"');
    for (const char16_t ch : decoded) {
        switch (ch) {
            case u'"':
                out += u"\\\"";
                break;
            case u'\\':
                out += u"\\\\";
                break;
            case u'\b':
                out += u"\\b";
                break;
            case u'\f':
                out += u"\\f";
                break;
            case u'\n':
                out += u"\\n";
                break;
            case u'\r':
                out += u"\\r";
                break;
            case u'\t':
                out += u"\\t";
                break;
            default:
                if (ch < 0x20) {
                    out += u"\\u00";
                    out.push_back(kHexDigits[(ch >> 4) & 0xF]);
                    out.push_back(kHexDigits[ch & 0xF]);
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    out.push_back(u'"');
    return out;
}

void formatValue(const JsonNode& node, std::u16string& out, int level, int indentWidth);

// Object/Array share this shape: `openCh`/`closeCh` on their own, each
// child on its own indented line, a trailing comma on every child but the
// last. An Object child additionally gets its (re-escaped) key + ": "
// prefix - an Array child does not (JsonNode::key is unset for Array
// elements, per json_tree.h's own contract).
void formatChildren(const JsonNode& node, std::u16string& out, int level, int indentWidth, char16_t openCh,
                    char16_t closeCh) {
    out.push_back(openCh);
    if (node.children.empty()) {
        out.push_back(closeCh);
        return;
    }
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        out.push_back(u'\n');
        appendIndent(out, level + 1, indentWidth);
        const JsonNode& child = node.children[i];
        if (child.key.has_value()) {
            out += escapeJsonString(*child.key);
            out += u": ";
        }
        formatValue(child, out, level + 1, indentWidth);
        if (i + 1 < node.children.size()) {
            out.push_back(u',');
        }
    }
    out.push_back(u'\n');
    appendIndent(out, level, indentWidth);
    out.push_back(closeCh);
}

void formatValue(const JsonNode& node, std::u16string& out, int level, int indentWidth) {
    switch (node.kind) {
        case JsonNodeKind::Object:
            formatChildren(node, out, level, indentWidth, u'{', u'}');
            break;
        case JsonNodeKind::Array:
            formatChildren(node, out, level, indentWidth, u'[', u']');
            break;
        case JsonNodeKind::String:
        case JsonNodeKind::Number:
        case JsonNodeKind::Boolean:
        case JsonNodeKind::Null:
            // Raw source text, quotes/escapes already exactly as the
            // document itself spelled them (see json_tree.h's own
            // JsonNode::text comment) - emitted verbatim, never
            // re-serialized.
            out += node.text;
            break;
    }
}

}  // namespace

std::u16string formatJsonNode(const JsonNode& root, int indentWidth) {
    std::u16string out;
    formatValue(root, out, 0, indentWidth);
    return out;
}

}  // namespace neomifes::jsontree
