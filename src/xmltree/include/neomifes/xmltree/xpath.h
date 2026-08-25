#pragma once

// XPath - self-implemented query language subset (WI-15i, Phase 10.3), the
// XML counterpart of jsontree::json_path.h (WI-15e) - same "self-implemented,
// no new external library, no ADR" reasoning (roadmap's own
// master_roadmap.md §10.3 mockup already calls this "自前実装"), and a
// deliberately symmetric scope: XPath supports roughly the same GRAMMAR
// complexity JSONPath does (a plain step chain with an optional single
// positional predicate per step), not real XPath's full axis/function/
// predicate-expression language.
//
// Supported grammar subset:
//   /                  root itself (the empty expression)
//   /tag               child step, every Element child whose tagName
//                      matches (non-Element children - Text/Comment/Cdata/
//                      ProcessingInstruction/EntityReference/Error - are
//                      never candidates for ANY step, silently excluded)
//   /*                 wildcard step: every Element child, any tag name
//   /tag[N] /*[N]      positional predicate (1-based, real XPath's own
//                      convention, NOT JSONPath's 0-based array index):
//                      the Nth child THAT ALREADY MATCHED this step's own
//                      tag-name/wildcard filter, counted per PARENT (a
//                      wildcard/tag-name step that fans out to multiple
//                      current nodes - e.g. after an earlier /* - computes
//                      this position independently for each one)
//   segments chain, e.g. /a/b or /a/*/c[2] or /a/b[1]/c
//
// NOT supported (left for a future WI if ever needed, same "future work"
// framing json_path.h uses): attribute selection/predicates (`/@attr`,
// `[@attr='v']`), the descendant-or-self axis (`//`), node-test functions
// (`text()`, `comment()`, ...), unions (`|`), any predicate more complex
// than a single positive integer. A malformed/unsupported construct makes
// parseXPath() return std::nullopt, same "never throw on bad input"
// convention as parseJsonPath()/goto_line_parser.h/tag_jump_parser.h.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neomifes::xmltree {

struct XmlNode;

enum class XPathSegmentKind : std::uint8_t {
    TagName,
    Wildcard,
};

struct XPathSegment {
    XPathSegmentKind kind = XPathSegmentKind::Wildcard;
    std::u16string    tagName = u"";  // TagName only
    // 0 = no positional predicate (every step-matching child passes
    // through); otherwise a 1-based position among the children that
    // already matched this step's own tag-name/wildcard filter (see this
    // file's header comment on why this differs from a raw child index).
    std::size_t index = 0;

    friend bool operator==(const XPathSegment&, const XPathSegment&) = default;
};

using XPathExpression = std::vector<XPathSegment>;

// Parses a single XPath expression string. Rejects (returns nullopt)
// anything that doesn't start with '/', an empty step (e.g. the trailing
// '/' in "/a/", or "//" - unsupported descendant axis), an unterminated '['
// or a non-digit/empty bracket body, a "[0]" predicate (XPath positions are
// 1-based - never producible by a real XPath tool, so treated the same as
// any other malformed input rather than silently clamped), or a stray
// character between steps. Single-pass scan over `expression` - no
// recursion, mirrors parseJsonPath()'s own "hand-written, noexcept-shaped,
// std::optional result" convention, adapted from JSONPath's '.'/'['
// segment separators to XPath's single '/' step separator.
[[nodiscard]] std::optional<XPathExpression> parseXPath(std::u16string_view expression) noexcept;

// Applies a parsed expression to `root`, returning pointers to every
// matching node (root itself is the sole match for the empty-segment "/"
// expression, same "root is always a valid match" contract
// evaluateJsonPath() establishes for "$"). Pointers are only valid as long
// as `root` (and the tree it owns) outlives them - same caller
// responsibility as any other XmlNode consumer in this module.
// Segment-by-segment, same "current match set -> new match set" pipeline
// evaluateJsonPath() uses: each step scans EVERY node in the current set for
// Element children matching its own tag-name/wildcard filter, applies its
// own positional predicate (if any) to just that per-parent filtered list,
// and a step/predicate that matches nothing for a given parent silently
// drops that branch from the set (same "a path that doesn't exist yields
// zero results" behavior real query languages have) rather than treated as
// an error. Purely iterative (the grammar itself doesn't recurse), so this
// never risked misc-no-recursion in the first place, same as
// evaluateJsonPath().
[[nodiscard]] std::vector<const XmlNode*> evaluateXPath(const XmlNode& root, const XPathExpression& expression);

}  // namespace neomifes::xmltree
