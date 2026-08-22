#pragma once

// JSONPath - self-implemented query language subset (WI-15e, Phase 10.3).
// Read-only queries against an already-parsed JsonNode tree (WI-15a) - no
// new external library, no ADR (roadmap's own master_roadmap.md §10.3
// mockup already calls this "自前実装"). XML/XPath deliberately excluded -
// those need a separate XML parser library decision, kept out of this WI's
// scope (same reasoning WI-15a used to split XML out of the original JSON
// tree work).
//
// Supported grammar subset:
//   $                  root itself
//   $.key              dot notation, Object member by name
//   $['key'] $["key"]  bracket notation, Object member by name (quotes
//                      required - this is how a key containing '.', '[',
//                      whitespace etc. is addressed, since dot notation
//                      stops at those characters)
//   $[0]               bracket notation, Array element by index (digits
//                      only - an unquoted bracket content is never a key)
//   $[*]               wildcard: every child of the current node(s),
//                      regardless of Object/Array
//   segments chain, e.g. $.users[0].name or $.users[*].age (wildcard
//                      fans a single current node out into all of its
//                      children before the next segment applies to each)
//
// NOT supported (left for a future WI if ever needed): recursive descent
// (..), filter expressions ([?(@.age>30)]), slices ([0:2]), unions
// ([0,1]), functions. A malformed/unsupported construct makes
// parseJsonPath() return std::nullopt, same "never throw on bad input"
// convention as goto_line_parser.h/tag_jump_parser.h.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neomifes::jsontree {

struct JsonNode;

enum class JsonPathSegmentKind : std::uint8_t {
    Key,
    Index,
    Wildcard,
};

struct JsonPathSegment {
    JsonPathSegmentKind kind = JsonPathSegmentKind::Wildcard;
    std::u16string       key;        // Key only
    std::size_t           index = 0;  // Index only

    friend bool operator==(const JsonPathSegment&, const JsonPathSegment&) = default;
};

using JsonPathExpression = std::vector<JsonPathSegment>;

// Parses a single JSONPath expression string. Rejects (returns nullopt)
// anything that doesn't start with '$', an unterminated '[' or quote, ".."
// (unsupported recursive descent), or bracket content that is neither
// digits, a quoted key, nor '*'. Single-pass scan over `expression` - no
// recursion, mirrors the "hand-written, noexcept-shaped, std::optional
// result" convention goto_line_parser.h/tag_jump_parser.h already
// established in this codebase, extended to handle a variable-length
// segment chain rather than their fixed one/two-field grammars.
[[nodiscard]] std::optional<JsonPathExpression> parseJsonPath(std::u16string_view expression) noexcept;

// Applies a parsed expression to `root`, returning pointers to every
// matching node (root itself is a valid match for the empty-segment "$"
// expression). Pointers are only valid as long as `root` (and the tree it
// owns) outlives them - same caller responsibility as any other JsonNode
// consumer in this module. Segment-by-segment: each segment maps the
// current match set to a new match set (Key/Index narrow each current
// match to at most one child; Wildcard fans each current match out to all
// of its children). A branch with no matching child (wrong kind, missing
// key, out-of-range index) is silently dropped from the set rather than
// treated as an error - the same "a path that doesn't exist yields zero
// results" behavior real JSONPath implementations have. Purely iterative
// (the grammar itself doesn't recurse), so unlike formatJsonNode() this
// never risked misc-no-recursion in the first place.
[[nodiscard]] std::vector<const JsonNode*> evaluateJsonPath(const JsonNode& root,
                                                              const JsonPathExpression& expression);

}  // namespace neomifes::jsontree
