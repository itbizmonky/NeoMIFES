#include "neomifes/jsontree/json_path.h"

#include <utility>

#include "neomifes/jsontree/json_tree.h"

namespace neomifes::jsontree {

namespace {

// A dot-notation key character stops at any of the characters that would
// otherwise start a new segment or close a bracket - '.', '[', ']' - and at
// quote characters (those only ever appear inside a bracket-quoted key,
// never in a bare dot key).
[[nodiscard]] bool isDotKeyChar(char16_t c) noexcept {
    return c != u'.' && c != u'[' && c != u']' && c != u'\'' && c != u'"';
}

// expression[pos] points just past the '.' that introduced this segment.
// Advances pos past the key. An empty key (e.g. the trailing '.' in "$.",
// or ".." - unsupported recursive descent) is rejected.
[[nodiscard]] bool parseDotKey(std::u16string_view expression, std::size_t& pos, std::u16string& outKey) {
    const std::size_t start = pos;
    while (pos < expression.size() && isDotKeyChar(expression[pos])) {
        ++pos;
    }
    if (pos == start) {
        return false;
    }
    outKey.assign(expression.substr(start, pos - start));
    return true;
}

// expression[pos] points just past the '[' that introduced this segment.
// Advances pos past the closing ']'. Accepts '*' -> Wildcard; a
// single/double-quoted span -> Key; a run of ASCII digits -> Index. Any
// other content (empty brackets, an unterminated quote, a non-digit
// unquoted span, a missing ']') is rejected.
[[nodiscard]] bool parseBracketContent(std::u16string_view expression, std::size_t& pos,
                                       JsonPathSegment& outSegment) {
    if (pos >= expression.size()) {
        return false;
    }

    if (expression[pos] == u'*') {
        ++pos;
        if (pos >= expression.size() || expression[pos] != u']') {
            return false;
        }
        ++pos;
        outSegment.kind = JsonPathSegmentKind::Wildcard;
        return true;
    }

    if (expression[pos] == u'\'' || expression[pos] == u'"') {
        const char16_t quote = expression[pos];
        ++pos;
        const std::size_t start = pos;
        while (pos < expression.size() && expression[pos] != quote) {
            ++pos;
        }
        if (pos >= expression.size()) {
            return false;  // unterminated quote
        }
        std::u16string key(expression.substr(start, pos - start));
        ++pos;  // consume the closing quote
        if (pos >= expression.size() || expression[pos] != u']') {
            return false;
        }
        ++pos;  // consume ']'
        outSegment.kind = JsonPathSegmentKind::Key;
        outSegment.key  = std::move(key);
        return true;
    }

    const std::size_t start = pos;
    while (pos < expression.size() && expression[pos] >= u'0' && expression[pos] <= u'9') {
        ++pos;
    }
    if (pos == start) {
        return false;  // neither '*', a quoted key, nor digits
    }
    std::size_t index = 0;
    for (std::size_t i = start; i < pos; ++i) {
        index = (index * 10) + static_cast<std::size_t>(expression[i] - u'0');
    }
    if (pos >= expression.size() || expression[pos] != u']') {
        return false;
    }
    ++pos;  // consume ']'
    outSegment.kind  = JsonPathSegmentKind::Index;
    outSegment.index = index;
    return true;
}

// The three per-segment-kind matchers evaluateJsonPath() below dispatches
// to. Split out (rather than inlined in a switch inside the double loop)
// purely to keep evaluateJsonPath() itself under this project's clang-tidy
// cognitive-complexity threshold - each one mirrors exactly the branch it
// replaced, no behavior change.

void appendKeyMatches(const JsonNode& node, const std::u16string& key, std::vector<const JsonNode*>& out) {
    if (node.kind != JsonNodeKind::Object) {
        return;
    }
    // Not stopping at the first match: a well-formed document never has a
    // duplicate key within one object, so in practice this yields at most
    // one node anyway - but matching all occurrences (rather than picking
    // "first" or "last" arbitrarily) is the simpler, better-defined choice
    // for the pathological duplicate-key case.
    for (const JsonNode& child : node.children) {
        if (child.key.has_value() && *child.key == key) {
            out.push_back(&child);
        }
    }
}

void appendIndexMatch(const JsonNode& node, std::size_t index, std::vector<const JsonNode*>& out) {
    if (node.kind == JsonNodeKind::Array && index < node.children.size()) {
        out.push_back(&node.children[index]);
    }
}

void appendWildcardMatches(const JsonNode& node, std::vector<const JsonNode*>& out) {
    // Every child regardless of Object/Array, per evaluateJsonPath()'s own
    // header comment - an Object's members and an Array's elements are both
    // just JsonNode entries in the same children vector (json_tree.h's own
    // documented design), so no kind check is needed here.
    for (const JsonNode& child : node.children) {
        out.push_back(&child);
    }
}

}  // namespace

std::optional<JsonPathExpression> parseJsonPath(std::u16string_view expression) noexcept {
    if (expression.empty() || expression.front() != u'$') {
        return std::nullopt;
    }

    JsonPathExpression result;
    std::size_t          pos = 1;
    while (pos < expression.size()) {
        const char16_t c = expression[pos];
        if (c == u'.') {
            ++pos;
            std::u16string key;
            if (!parseDotKey(expression, pos, key)) {
                return std::nullopt;
            }
            result.push_back(JsonPathSegment{.kind = JsonPathSegmentKind::Key, .key = std::move(key)});
        } else if (c == u'[') {
            ++pos;
            JsonPathSegment segment;
            if (!parseBracketContent(expression, pos, segment)) {
                return std::nullopt;
            }
            result.push_back(std::move(segment));
        } else {
            return std::nullopt;  // stray character between segments, e.g. "$x" or "$ .a"
        }
    }
    return result;
}

std::vector<const JsonNode*> evaluateJsonPath(const JsonNode& root, const JsonPathExpression& expression) {
    std::vector<const JsonNode*> current{&root};
    for (const JsonPathSegment& segment : expression) {
        std::vector<const JsonNode*> next;
        for (const JsonNode* node : current) {
            switch (segment.kind) {
                case JsonPathSegmentKind::Key:
                    appendKeyMatches(*node, segment.key, next);
                    break;
                case JsonPathSegmentKind::Index:
                    appendIndexMatch(*node, segment.index, next);
                    break;
                case JsonPathSegmentKind::Wildcard:
                    appendWildcardMatches(*node, next);
                    break;
            }
        }
        current = std::move(next);
    }
    return current;
}

}  // namespace neomifes::jsontree
