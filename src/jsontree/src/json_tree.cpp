#include "neomifes/jsontree/json_tree.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "json_string_convert.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/util/utf8_convert.h"

namespace neomifes::jsontree {

namespace {

// nlohmann::ordered_json::parse() builds a DOM whose construction AND
// eventual destruction both recurse one C++ stack frame per nesting level
// (its own token-stream walk, parser::sax_parse_internal(), is iterative -
// verified by reading nlohmann/detail/input/parser.hpp directly - but
// json_sax_dom_parser's tree building and basic_json's destructor are not).
// docs/issues/json_tree_worker_deep_nesting_stack_overflow.md recorded a
// real STATUS_STACK_OVERFLOW at depth 2000 under the ubsan/clang-cl build
// (whose instrumentation uses far more stack per frame than MSVC
// Debug/Release, which survived the same input - build config, not just
// nesting depth, determines the actual crash point). 200 gives a 10x margin
// below the one crash depth we have real measurements for. Rejecting before
// nlohmann::ordered_json::parse() ever runs means neither the construction
// nor the destruction recursion is reached at all for anything over this.
constexpr int kMaxJsonNestingDepth = 200;

// Pre-parse pass that only tracks container nesting depth, so it can reject
// pathologically deep input BEFORE nlohmann::ordered_json::parse() builds
// (and eventually destroys) a DOM tree deep enough to overflow the stack -
// see kMaxJsonNestingDepth's comment. Verified via a standalone probe
// (depth 50000 through this handler, well past the 2000 that crashed real
// DOM construction, does not crash - json_sax_dom_parser's own recursion is
// never reached because this handler never builds a DOM) before being wired
// in here, per CLAUDE.md rule 3. Deriving from nlohmann::json_sax<T> here
// triggers clang-tidy's portability-template-virtual-member-function on
// every one of the base template's pure virtuals; that check is disabled
// project-wide in .clang-tidy (see the comment there) because a NOLINT
// comment cannot suppress it - its primary diagnostic location is inside
// the third-party header, not this derivation site.
class DepthLimitSax : public nlohmann::json_sax<nlohmann::ordered_json> {
public:
    bool null() override { return true; }
    bool boolean(bool /*val*/) override { return true; }
    bool number_integer(number_integer_t /*val*/) override { return true; }
    bool number_unsigned(number_unsigned_t /*val*/) override { return true; }
    bool number_float(number_float_t /*val*/, const string_t& /*s*/) override { return true; }
    bool string(string_t& /*val*/) override { return true; }
    bool binary(binary_t& /*val*/) override { return true; }
    bool key(string_t& /*val*/) override { return true; }

    bool start_object(std::size_t /*elements*/) override { return ++m_depth <= kMaxJsonNestingDepth; }
    bool end_object() override {
        --m_depth;
        return true;
    }
    bool start_array(std::size_t /*elements*/) override { return ++m_depth <= kMaxJsonNestingDepth; }
    bool end_array() override {
        --m_depth;
        return true;
    }

    bool parse_error(std::size_t /*position*/, const std::string& /*lastToken*/,
                      const nlohmann::detail::exception& /*ex*/) override {
        return false;
    }

private:
    int m_depth = 0;
};

// True if `utf8` either fails to parse or nests containers deeper than
// kMaxJsonNestingDepth allows - both collapse to the same std::nullopt
// contract in parseJsonTree(), so this doesn't need to distinguish "not
// JSON" from "too deep" any more than parseJsonTree() already treats
// malformed input and non-JSON input alike.
[[nodiscard]] bool exceedsMaxNestingDepth(std::string_view utf8) {
    DepthLimitSax handler;
    return !nlohmann::ordered_json::sax_parse(utf8, &handler);
}

[[nodiscard]] bool isJsonWhitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

[[nodiscard]] bool isJsonStructuralOrWhitespace(char c) noexcept {
    return isJsonWhitespace(c) || c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',';
}

// Scans utf8 text that a prior nlohmann::ordered_json::parse() call has
// ALREADY validated as syntactically correct JSON - never needs to reject
// malformed input itself, only find token boundaries. Safe to scan the
// UTF-8 byte-by-byte for the single-byte ASCII structural characters
// ('{'/'}'/'['/']'/':'/','/'"') even though the text may contain multi-byte
// UTF-8 sequences elsewhere: every continuation/lead byte of a multi-byte
// UTF-8 sequence has its high bit set (>= 0x80), so it can never be
// mistaken for one of these ASCII (< 0x80) delimiters.
class PositionScanner {
public:
    explicit PositionScanner(std::string_view utf8) : m_text(utf8) {}

    // Byte offset of the next non-whitespace character, without consuming
    // it - used to learn a value's startPos before its kind (and therefore
    // which consume*() applies) is known.
    [[nodiscard]] std::size_t peekTokenStart() {
        skipWhitespace();
        return m_cursor;
    }

    // Consumes '"' ... '"', honoring '\' escapes (skips the escaped
    // character too, so an escaped quote doesn't end the scan early) -
    // never decodes the content, just finds the matching close quote.
    // Returns [start, end) spanning both quotes.
    [[nodiscard]] std::pair<std::size_t, std::size_t> consumeString() {
        skipWhitespace();
        const std::size_t start = m_cursor;
        ++m_cursor;  // opening quote
        while (m_cursor < m_text.size() && m_text[m_cursor] != '"') {
            if (m_text[m_cursor] == '\\' && m_cursor + 1 < m_text.size()) {
                ++m_cursor;  // skip the escaped character too
            }
            ++m_cursor;
        }
        ++m_cursor;  // closing quote
        return {start, m_cursor};
    }

    // Consumes exactly one structural byte ('{' '}' '[' ']' ':' ','; the
    // caller already knows which one from the DOM shape it's walking, so
    // this doesn't re-verify it). Returns the position just after it.
    std::size_t consumeDelimiter() {
        skipWhitespace();
        ++m_cursor;
        return m_cursor;
    }

    // Consumes a number/true/false/null literal (no internal escaping or
    // nesting) - scans until whitespace or a structural character
    // terminates it. Returns [start, end).
    [[nodiscard]] std::pair<std::size_t, std::size_t> consumeLiteral() {
        skipWhitespace();
        const std::size_t start = m_cursor;
        while (m_cursor < m_text.size() && !isJsonStructuralOrWhitespace(m_text[m_cursor])) {
            ++m_cursor;
        }
        return {start, m_cursor};
    }

private:
    void skipWhitespace() {
        while (m_cursor < m_text.size() && isJsonWhitespace(m_text[m_cursor])) {
            ++m_cursor;
        }
    }

    std::string_view m_text;
    std::size_t      m_cursor = 0;
};

// One "currently being consumed" Object/Array frame in buildTree()'s
// explicit stack (see this file's header comment on why this is iterative,
// not recursive). `node` points at the JsonNode this frame is filling in -
// always an element inside SOME OTHER JsonNode's `children` vector (or the
// root JsonNode owned by the caller), never inside `stack` itself, so it
// stays valid across `stack`'s own reallocations. It is only ever taken
// once its own slot has been created via `children.emplace_back()` and
// only handed to a new frame right before that frame is pushed - by the
// time the parent frame adds this container's next SIBLING (the only other
// operation that could reallocate the same `children` vector), this frame
// has already been fully consumed and popped, per the depth-first order
// the while-loop in buildTree() enforces.
struct PendingContainer {
    JsonNodeKind                           kind;
    nlohmann::ordered_json::const_iterator it;
    nlohmann::ordered_json::const_iterator end;
    JsonNode*                              node;
    bool                                   isFirstChild;
};

// Bundles the state buildTree()'s helper functions below all need, so none
// of them has to take 4-5 separate parameters (which would just move the
// same cognitive-complexity problem from "one giant function" to "one
// function with an unreadable parameter list"). Reference members are safe
// here: every ParseState is a local variable constructed and destroyed
// within a single buildTree() call, and never outlives the scanner/buffer/
// stack it points at - the same "reference-bundle passed to sibling
// functions" shape src/app/include/neomifes/app/command_dispatch.h's
// CommandDispatchContext already established for this codebase (that
// struct's own 6 reference members have simply never been individually
// clang-tidy'd by any WI's changed-file sweep, so this is the first time
// this check has actually fired against the pattern, not a new pattern).
// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
struct ParseState {
    PositionScanner&                  scanner;
    const std::vector<std::uint32_t>& byteToUtf16;
    const std::u16string&             buffer;
    std::vector<PendingContainer>&    stack;
};
// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

[[nodiscard]] document::TextPos mapPos(const ParseState& state, std::size_t bytePos) {
    return static_cast<document::TextPos>(state.byteToUtf16[bytePos]);
}

[[nodiscard]] std::u16string sliceText(const ParseState& state, document::TextPos start, document::TextPos end) {
    return state.buffer.substr(start, end - start);
}

// Positions `node` for `domValue`: sets kind + startPos always, and for
// leaf kinds also endPos + text (the raw source substring - see this
// module's header comment on why every leaf kind, including String, keeps
// its exact source spelling rather than a decoded value). For Object/Array,
// only startPos is set here; a new frame is pushed onto state.stack so
// buildTree()'s while-loop consumes its children and its own closing
// delimiter later. Not recursive - never calls itself or buildTree().
[[nodiscard]] bool openValue(const nlohmann::ordered_json& domValue, JsonNode& node, ParseState& state) {
    if (domValue.is_object()) {
        node.kind                 = JsonNodeKind::Object;
        const std::size_t openPos = state.scanner.peekTokenStart();
        state.scanner.consumeDelimiter();
        node.startPos = mapPos(state, openPos);
        state.stack.push_back(PendingContainer{.kind         = JsonNodeKind::Object,
                                                .it           = domValue.cbegin(),
                                                .end          = domValue.cend(),
                                                .node         = &node,
                                                .isFirstChild = true});
        return true;
    }
    if (domValue.is_array()) {
        node.kind                 = JsonNodeKind::Array;
        const std::size_t openPos = state.scanner.peekTokenStart();
        state.scanner.consumeDelimiter();
        node.startPos = mapPos(state, openPos);
        state.stack.push_back(PendingContainer{.kind         = JsonNodeKind::Array,
                                                .it           = domValue.cbegin(),
                                                .end          = domValue.cend(),
                                                .node         = &node,
                                                .isFirstChild = true});
        return true;
    }
    if (domValue.is_string()) {
        node.kind         = JsonNodeKind::String;
        const auto [s, e] = state.scanner.consumeString();
        node.startPos     = mapPos(state, s);
        node.endPos       = mapPos(state, e);
        node.text         = sliceText(state, node.startPos, node.endPos);
        return true;
    }
    if (domValue.is_boolean()) {
        node.kind = JsonNodeKind::Boolean;
    } else if (domValue.is_null()) {
        node.kind = JsonNodeKind::Null;
    } else if (domValue.is_number_integer() || domValue.is_number_unsigned() || domValue.is_number_float()) {
        node.kind = JsonNodeKind::Number;
    } else {
        return false;  // binary_t - not producible by parsing JSON text
    }
    const auto [s, e] = state.scanner.consumeLiteral();
    node.startPos     = mapPos(state, s);
    node.endPos       = mapPos(state, e);
    node.text         = sliceText(state, node.startPos, node.endPos);
    return true;
}

// Consumes `top`'s closing delimiter ('}' or ']') and records it as `top`'s
// own JsonNode's endPos - called once top.it == top.end (no children left).
void closeContainer(const PendingContainer& top, ParseState& state) {
    const std::size_t closeEnd = state.scanner.consumeDelimiter();
    top.node->endPos           = mapPos(state, closeEnd);
}

// Consumes exactly one child of `top`'s container (a separating ',' first,
// unless it's the first child) and appends it to `top.node->children`.
// Returns false only if the child's own key/value turned out unparseable
// (folds into parseJsonTree()'s overall nullopt contract).
[[nodiscard]] bool consumeNextChild(PendingContainer& top, ParseState& state) {
    // Capture everything this call needs from `top` NOW: openValue() below
    // may push a new PendingContainer for a container child, which can
    // reallocate state.stack and invalidate `top` (a reference into it).
    JsonNode* const containerNode = top.node;
    const bool      isObject      = (top.kind == JsonNodeKind::Object);
    const bool      needsComma    = !top.isFirstChild;
    top.isFirstChild              = false;

    std::string keyUtf8;
    if (isObject) {
        keyUtf8 = top.it.key();
    }
    const nlohmann::ordered_json& domValue = isObject ? top.it.value() : *top.it;
    ++top.it;  // `top` must not be used below this line.

    if (needsComma) {
        state.scanner.consumeDelimiter();
    }

    JsonNode& child = containerNode->children.emplace_back();

    if (!isObject) {
        return openValue(domValue, child, state);
    }

    const auto [ks, ke] = state.scanner.consumeString();
    auto decodedKey     = detail::fromUtf8(keyUtf8);
    if (!decodedKey) {
        return false;
    }
    child.key = std::move(*decodedKey);
    state.scanner.consumeDelimiter();  // ':'
    if (!openValue(domValue, child, state)) {
        return false;
    }
    // Override: an Object member's own span starts at its key, not its
    // value (see json_tree.h's field comment on startPos/endPos).
    child.startPos = mapPos(state, ks);
    return true;
}

// Walks `root` (an nlohmann::ordered_json value from an already-successful
// parse) and the SAME underlying text via an explicit stack of
// PendingContainer frames (see this file's header comment on why this is
// iterative, not recursive), building the JsonNode tree. `buffer` is the
// original UTF-16 document text `conv` was derived from - leaf text/keys
// are sliced from it directly via the byte->UTF-16 offset table, never
// re-decoded from UTF-8.
[[nodiscard]] std::optional<JsonNode> buildTree(const nlohmann::ordered_json& root,
                                                 const std::u16string&        buffer,
                                                 const util::Utf8Conversion&  conv) {
    PositionScanner               scanner(conv.utf8);
    std::vector<PendingContainer> stack;
    ParseState state{.scanner = scanner, .byteToUtf16 = conv.byteToUtf16, .buffer = buffer, .stack = stack};

    JsonNode rootNode;
    if (!openValue(root, rootNode, state)) {
        return std::nullopt;
    }

    while (!stack.empty()) {
        PendingContainer& top = stack.back();
        if (top.it == top.end) {
            closeContainer(top, state);
            stack.pop_back();
            continue;
        }
        if (!consumeNextChild(top, state)) {
            return std::nullopt;
        }
    }

    return rootNode;
}

}  // namespace

std::optional<JsonNode> parseJsonTree(const document::BufferSnapshot& snapshot) {
    std::u16string buffer;
    for (const auto& piece : snapshot.pieces()) {
        buffer.append(snapshot.pieceView(piece));
    }

    const util::Utf8Conversion conv = util::toUtf8WithOffsets(buffer);
    if (exceedsMaxNestingDepth(conv.utf8)) {
        return std::nullopt;
    }

    const auto parsed = nlohmann::ordered_json::parse(conv.utf8, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }

    return buildTree(parsed, buffer, conv);
}

std::optional<JsonNode> parseJsonTree(const document::Document& doc) {
    return parseJsonTree(*doc.snapshot());
}

}  // namespace neomifes::jsontree
