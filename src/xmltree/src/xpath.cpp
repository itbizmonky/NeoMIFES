#include "neomifes/xmltree/xpath.h"

#include <utility>

#include "neomifes/xmltree/xml_tree.h"

namespace neomifes::xmltree {

namespace {

// A step-name character stops at any of the characters that would otherwise
// separate steps or open/close a predicate - '/', '[', ']' - the XPath
// counterpart of json_path.cpp's isDotKeyChar().
[[nodiscard]] bool isStepNameChar(char16_t c) noexcept {
    return c != u'/' && c != u'[' && c != u']';
}

// expression[pos] points at the first character of a step name (NOT '*',
// that's handled by the caller before this is reached). Advances pos past
// the name. An empty name (e.g. the trailing '/' in "/a/", or "//") is
// rejected.
[[nodiscard]] bool parseStepName(std::u16string_view expression, std::size_t& pos, std::u16string& outName) {
    const std::size_t start = pos;
    while (pos < expression.size() && isStepNameChar(expression[pos])) {
        ++pos;
    }
    if (pos == start) {
        return false;
    }
    outName.assign(expression.substr(start, pos - start));
    return true;
}

// expression[pos] points at the '[' introducing a positional predicate.
// Advances pos past the closing ']'. Accepts a run of ASCII digits only
// (unlike json_path.cpp's parseBracketContent(), there is no '*'/quoted-key
// alternative here - a bare wildcard step is already spelled without
// brackets, "/*", and XPath has no bracket-quoted-key concept at all).
// Rejects an empty/non-digit body, a missing ']', or a "[0]" predicate (see
// this file's xpath.h header comment on why 1-based, not clamped).
[[nodiscard]] bool parseIndexPredicate(std::u16string_view expression, std::size_t& pos, std::size_t& outIndex) {
    ++pos;  // consume '['
    const std::size_t start = pos;
    while (pos < expression.size() && expression[pos] >= u'0' && expression[pos] <= u'9') {
        ++pos;
    }
    if (pos == start) {
        return false;  // empty or non-digit body
    }
    std::size_t index = 0;
    for (std::size_t i = start; i < pos; ++i) {
        index = (index * 10) + static_cast<std::size_t>(expression[i] - u'0');
    }
    if (pos >= expression.size() || expression[pos] != u']') {
        return false;  // missing ']'
    }
    ++pos;  // consume ']'
    if (index == 0) {
        return false;  // XPath positions are 1-based
    }
    outIndex = index;
    return true;
}

// Scans `node`'s own Element children for `segment`'s tag-name/wildcard
// filter (in source order), then applies `segment.index` (if any) to just
// that per-PARENT filtered list - see xpath.h's own header comment on why
// the positional predicate must be computed per parent rather than across
// the whole current match set. A predicate that's out of range for this
// particular parent silently drops this branch (no match appended), the
// same "nonexistent path yields zero results" behavior json_path.cpp's
// appendIndexMatch() already establishes.
void appendStepMatches(const XmlNode& node, const XPathSegment& segment, std::vector<const XmlNode*>& out) {
    std::vector<const XmlNode*> candidates;
    for (const XmlNode& child : node.children) {
        if (child.kind != XmlNodeKind::Element) {
            continue;
        }
        if (segment.kind == XPathSegmentKind::TagName && child.tagName != segment.tagName) {
            continue;
        }
        candidates.push_back(&child);
    }
    if (segment.index == 0) {
        out.insert(out.end(), candidates.begin(), candidates.end());
    } else if (segment.index <= candidates.size()) {
        out.push_back(candidates[segment.index - 1]);
    }
}

}  // namespace

std::optional<XPathExpression> parseXPath(std::u16string_view expression) noexcept {
    if (expression.empty() || expression.front() != u'/') {
        return std::nullopt;
    }

    XPathExpression result;
    std::size_t      pos = 1;
    while (pos < expression.size()) {
        XPathSegment segment;
        if (expression[pos] == u'*') {
            ++pos;
            segment.kind = XPathSegmentKind::Wildcard;
        } else {
            std::u16string name;
            if (!parseStepName(expression, pos, name)) {
                return std::nullopt;
            }
            segment.kind    = XPathSegmentKind::TagName;
            segment.tagName = std::move(name);
        }

        if (pos < expression.size() && expression[pos] == u'[') {
            if (!parseIndexPredicate(expression, pos, segment.index)) {
                return std::nullopt;
            }
        }
        result.push_back(std::move(segment));

        if (pos < expression.size()) {
            if (expression[pos] != u'/') {
                return std::nullopt;  // stray character between steps, e.g. "/a b"
            }
            ++pos;  // consume '/' before the next step
            if (pos >= expression.size()) {
                return std::nullopt;  // trailing '/', e.g. "/a/"
            }
        }
    }
    return result;
}

std::vector<const XmlNode*> evaluateXPath(const XmlNode& root, const XPathExpression& expression) {
    std::vector<const XmlNode*> current{&root};
    for (const XPathSegment& segment : expression) {
        std::vector<const XmlNode*> next;
        for (const XmlNode* node : current) {
            appendStepMatches(*node, segment, next);
        }
        current = std::move(next);
    }
    return current;
}

}  // namespace neomifes::xmltree
