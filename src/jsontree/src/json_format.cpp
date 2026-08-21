#include "neomifes/jsontree/json_format.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

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
    constexpr std::array<char16_t, 16> kHexDigits{u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7',
                                                   u'8', u'9', u'a', u'b', u'c', u'd', u'e', u'f'};

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
                    out.push_back(kHexDigits.at((ch >> 4) & 0xF));
                    out.push_back(kHexDigits.at(ch & 0xF));
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    out.push_back(u'"');
    return out;
}

[[nodiscard]] char16_t openDelimiter(JsonNodeKind kind) noexcept {
    return kind == JsonNodeKind::Object ? u'{' : u'[';
}

[[nodiscard]] char16_t closeDelimiter(JsonNodeKind kind) noexcept {
    return kind == JsonNodeKind::Object ? u'}' : u']';
}

// One Object/Array node currently being rendered: `node` points at it
// (stable regardless of `stack`'s own reallocations - it is an address
// into the JsonNode TREE the caller owns, never into `stack` itself, same
// non-aliasing argument json_tree.cpp's own PendingContainer::node
// comment makes), `childIndex` is the next child of `node` still to emit.
struct PendingContainer {
    const JsonNode* node;
    std::size_t     childIndex = 0;
    int              level;
};

}  // namespace

// Iterative, not recursive (mirrors json_tree.cpp's own buildTree() -
// same misc-no-recursion policy this project applies project-wide, not a
// stack-safety necessity here specifically: every JsonNode this function
// can be handed already came from parseJsonTree(), which caps nesting at
// kMaxJsonNestingDepth=200 - see json_format.h's own comment on why 200
// levels of recursion would in fact have been perfectly safe). Renders a
// child immediately if it's a scalar (no stack frame needed) and pushes a
// new PendingContainer only for a non-empty Object/Array child - an empty
// one is closed inline the same way the initial root is.
std::u16string formatJsonNode(const JsonNode& root, int indentWidth) {
    if (root.kind != JsonNodeKind::Object && root.kind != JsonNodeKind::Array) {
        return root.text;  // bare scalar root (RFC 8259 permits this) - nothing to indent
    }

    std::u16string out;
    out.push_back(openDelimiter(root.kind));
    if (root.children.empty()) {
        out.push_back(closeDelimiter(root.kind));
        return out;
    }

    std::vector<PendingContainer> stack;
    stack.push_back(PendingContainer{.node = &root, .level = 0});

    while (!stack.empty()) {
        // Capture everything this iteration needs from `top` BEFORE any
        // stack.push_back() below, which can reallocate `stack` and
        // invalidate this reference - same hazard json_tree.cpp's own
        // consumeNextChild() guards against for the identical reason.
        PendingContainer& top           = stack.back();
        const JsonNode&    containerNode = *top.node;
        const int           level         = top.level;

        if (top.childIndex >= containerNode.children.size()) {
            out.push_back(u'\n');
            appendIndent(out, level, indentWidth);
            out.push_back(closeDelimiter(containerNode.kind));
            stack.pop_back();
            continue;
        }

        const std::size_t childIndex = top.childIndex;
        ++top.childIndex;  // `top` must not be used below this line.

        if (childIndex > 0) {
            out.push_back(u',');
        }
        out.push_back(u'\n');
        appendIndent(out, level + 1, indentWidth);

        const JsonNode& child = containerNode.children[childIndex];
        if (child.key.has_value()) {
            out += escapeJsonString(*child.key);
            out += u": ";
        }

        if (child.kind == JsonNodeKind::Object || child.kind == JsonNodeKind::Array) {
            out.push_back(openDelimiter(child.kind));
            if (child.children.empty()) {
                out.push_back(closeDelimiter(child.kind));
            } else {
                stack.push_back(PendingContainer{.node = &child, .level = level + 1});
            }
        } else {
            // Raw source text, quotes/escapes already exactly as the
            // document itself spelled them (see json_tree.h's own
            // JsonNode::text comment) - emitted verbatim, never
            // re-serialized.
            out += child.text;
        }
    }

    return out;
}

}  // namespace neomifes::jsontree
