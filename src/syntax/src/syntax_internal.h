#pragma once

// Shared, non-public tree-sitter parsing internals (Phase 7k). Factored out
// of syntax.cpp's former anonymous namespace so incremental_parser.cpp (same
// leaf classification, but retaining a TSTree across calls instead of
// discarding it) can reuse the leaf-kind tables and the tree walk without
// duplicating them. Lives in src/ (not include/) - NOT part of the public
// neomifes::syntax API surface, only syntax.cpp/incremental_parser.cpp (both
// in this directory) include it, via a plain quoted #include resolved
// relative to this file. tree-sitter types stay confined to this file and
// syntax.cpp/incremental_parser.cpp, matching syntax.h's own header comment.

#include <tree_sitter/api.h>

#include <cctype>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "neomifes/syntax/syntax.h"

namespace neomifes::syntax::detail {

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_cpp(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_python(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_c(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_javascript(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_java(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_go(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_rust(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_json(void);

// Phase 7n1: single Language -> TSLanguage* mapping shared by syntax.cpp,
// incremental_parser.cpp, and outline.cpp - previously each of the first two
// had its own private copy of this switch (fine at 2 languages, Phase 7d),
// and outline.cpp had a 2-way ternary that would have silently routed any
// new Language through tree_sitter_python() (see outline.cpp's Phase 7n1
// comment for the concrete hazard this caused). Centralizing here means
// adding a language only requires updating this one switch, not three.
[[nodiscard]] inline const TSLanguage* tsLanguageFor(Language language) noexcept {
    switch (language) {
        case Language::Cpp:
            return tree_sitter_cpp();
        case Language::Python:
            return tree_sitter_python();
        case Language::C:
            return tree_sitter_c();
        case Language::JavaScript:
            return tree_sitter_javascript();
        case Language::Java:
            return tree_sitter_java();
        case Language::Go:
            return tree_sitter_go();
        case Language::Rust:
            return tree_sitter_rust();
        case Language::Json:
            return tree_sitter_json();
    }
    return tree_sitter_cpp();  // unreachable (all enumerators handled above)
}

using TSParserPtr = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using TSTreePtr   = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;

[[nodiscard]] inline TSParserPtr makeParser(const TSLanguage* language) {
    TSParserPtr parser(ts_parser_new(), &ts_parser_delete);
    ts_parser_set_language(parser.get(), language);
    return parser;
}

using LeafKindTable = std::unordered_map<std::string_view, TokenKind>;

// See syntax.cpp's original comment (Phase 7a/7d) for how this table was
// built (tree-sitter-cpp v0.23.4's node-types.json cross-checked against
// real parser output, not guessed from memory - CLAUDE.md rule 3).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForCpp() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"number_literal", TokenKind::Number},
        {"primitive_type", TokenKind::Type},
        {"type_identifier", TokenKind::Type},
        {"namespace_identifier", TokenKind::Type},
        {"identifier", TokenKind::Variable},
        {"field_identifier", TokenKind::Variable},
        {"statement_identifier", TokenKind::Variable},
        {"string_content", TokenKind::String},
        {"escape_sequence", TokenKind::String},
        {"character", TokenKind::String},
        {"raw_string_content", TokenKind::String},
        {"system_lib_string", TokenKind::String},
        {"preproc_directive", TokenKind::Preprocessor},
        {"preproc_arg", TokenKind::Preprocessor},
        {"true", TokenKind::Keyword},
        {"false", TokenKind::Keyword},
        {"this", TokenKind::Keyword},
        {"null", TokenKind::Keyword},
        {"auto", TokenKind::Keyword},
        {"noexcept", TokenKind::Keyword},
    };
    return table;
}

// See syntax.cpp's original comment (Phase 7d) for how this table was built
// (tree-sitter-python v0.25.0's node-types.json cross-checked against a
// standalone probe, not guessed from memory - CLAUDE.md rule 3).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForPython() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"integer", TokenKind::Number},
        {"float", TokenKind::Number},
        {"identifier", TokenKind::Variable},
        {"string_start", TokenKind::String},
        {"string_content", TokenKind::String},
        {"string_end", TokenKind::String},
        {"escape_sequence", TokenKind::String},
        {"escape_interpolation", TokenKind::String},
        {"type_conversion", TokenKind::String},  // f-string "!r"/"!s"/"!a" conversion flag
        {"true", TokenKind::Keyword},
        {"false", TokenKind::Keyword},
        {"none", TokenKind::Keyword},
        {"ellipsis", TokenKind::Keyword},  // "..." - a constant literal, colored like true/false/none
    };
    return table;
}

// Verified via a standalone probe (Phase 7n1) against real tree-sitter-c
// v0.24.2 output. C's grammar is a near-subset of tree-sitter-cpp's (no
// namespaces, no raw strings, no true/false/this/auto/noexcept as named
// keyword nodes - those are anonymous alphabetic tokens here, already
// correctly classified as Keyword by classifyAnonymousLeaf() below).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForC() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"number_literal", TokenKind::Number},
        {"primitive_type", TokenKind::Type},
        {"type_identifier", TokenKind::Type},
        {"identifier", TokenKind::Variable},
        {"field_identifier", TokenKind::Variable},
        {"string_content", TokenKind::String},
        {"escape_sequence", TokenKind::String},
        {"character", TokenKind::String},
        {"system_lib_string", TokenKind::String},
        {"preproc_arg", TokenKind::Preprocessor},
    };
    return table;
}

// Verified via a standalone probe (Phase 7n1) against real
// tree-sitter-javascript v0.25.0 output. "true"/"false"/"null"/"undefined"/
// "this"/"super" are all NAMED leaf nodes here (unlike C/C++'s anonymous
// alphabetic tokens), so each needs an explicit table entry.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForJavaScript() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"number", TokenKind::Number},
        {"identifier", TokenKind::Variable},
        {"property_identifier", TokenKind::Variable},
        {"string_fragment", TokenKind::String},
        {"escape_sequence", TokenKind::String},
        {"regex_pattern", TokenKind::String},
        {"regex_flags", TokenKind::String},
        {"true", TokenKind::Keyword},
        {"false", TokenKind::Keyword},
        {"null", TokenKind::Keyword},
        {"undefined", TokenKind::Keyword},
        {"this", TokenKind::Keyword},
        {"super", TokenKind::Keyword},
    };
    return table;
}

// Verified via a standalone probe (Phase 7n1) against real tree-sitter-java
// v0.23.5 output. Unlike tree-sitter-cpp, comments split into two distinct
// leaf node types (line_comment/block_comment) rather than one shared
// "comment"; true/false/null_literal/this are named nodes (not anonymous).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForJava() {
    static const LeafKindTable table{
        {"line_comment", TokenKind::Comment},
        {"block_comment", TokenKind::Comment},
        {"decimal_integer_literal", TokenKind::Number},
        {"type_identifier", TokenKind::Type},
        {"identifier", TokenKind::Variable},
        {"string_fragment", TokenKind::String},
        {"escape_sequence", TokenKind::String},
        {"character_literal", TokenKind::String},
        {"true", TokenKind::Keyword},
        {"false", TokenKind::Keyword},
        {"null_literal", TokenKind::Keyword},
        {"this", TokenKind::Keyword},
    };
    return table;
}

// Verified via a standalone probe (Phase 7n1) against real tree-sitter-go
// v0.25.0 output. package_identifier is classified as Type (same treatment
// as C++'s namespace_identifier) since it names a package, not a value.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForGo() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"int_literal", TokenKind::Number},
        {"float_literal", TokenKind::Number},
        {"type_identifier", TokenKind::Type},
        {"package_identifier", TokenKind::Type},
        {"identifier", TokenKind::Variable},
        {"field_identifier", TokenKind::Variable},
        {"interpreted_string_literal_content", TokenKind::String},
        {"raw_string_literal_content", TokenKind::String},
        {"escape_sequence", TokenKind::String},
        {"rune_literal", TokenKind::String},
        {"true", TokenKind::Keyword},
        {"false", TokenKind::Keyword},
        {"nil", TokenKind::Keyword},
    };
    return table;
}

// Verified via a standalone probe (Phase 7n1) against real tree-sitter-rust
// v0.24.2 output. IMPORTANT deviation from every other language probed so
// far: line_comment/block_comment are NOT leaf nodes here (they have
// anonymous "//" / "/*" + "*/" delimiter children, with the comment BODY
// text left uncaptured by any child node) - a plain child_count==0 leaf
// test would misclassify the delimiters as Punctuation and silently drop
// the body text from the token stream entirely (confirmed via probe before
// this table was written). walkTree()/walkTreeIncremental() were both
// generalized (see detail::isAtomicNode() below) to treat any node whose
// type has a table entry as atomic - i.e. never descended into - which
// makes this table entry alone sufficient to color the whole comment
// correctly, delimiters included.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForRust() {
    static const LeafKindTable table{
        {"line_comment", TokenKind::Comment},
        {"block_comment", TokenKind::Comment},
        {"integer_literal", TokenKind::Number},
        {"float_literal", TokenKind::Number},
        {"type_identifier", TokenKind::Type},
        {"primitive_type", TokenKind::Type},
        {"identifier", TokenKind::Variable},
        {"field_identifier", TokenKind::Variable},
        {"string_content", TokenKind::String},
        {"escape_sequence", TokenKind::String},
        {"char_literal", TokenKind::String},
    };
    return table;
}

// Verified via a standalone probe (Phase 7n1) against real tree-sitter-json
// v0.24.8 output. JSON has no comments/identifiers/keywords beyond the 3
// literal values below - a deliberately much smaller table than the other
// languages, matching JSON's own much smaller grammar.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForJson() {
    static const LeafKindTable table{
        {"string_content", TokenKind::String},
        {"number", TokenKind::Number},
        {"true", TokenKind::Keyword},
        {"false", TokenKind::Keyword},
        {"null", TokenKind::Keyword},
    };
    return table;
}

// See syntax.cpp's original comment (Phase 7a/7d) for the classification
// rationale (structural: alphabetic -> keyword, leading '#' -> preprocessor,
// quote delimiters -> string, everything else -> punctuation). Phase 7n1
// added backtick to the quote-delimiter set: both tree-sitter-go's raw
// string literals and tree-sitter-javascript's template strings wrap their
// content in anonymous "`" delimiter leaves (verified via probe), and
// coloring them like every other quote character (part of the string, not
// plain punctuation) matches this project's existing convention for `"`/`'`.
[[nodiscard]] inline TokenKind classifyAnonymousLeaf(std::string_view type) {
    if (type.empty()) {
        return TokenKind::Text;
    }
    if (type.front() == '#') {
        return TokenKind::Preprocessor;
    }
    if (type == "\"" || type == "'" || type == "`") {
        return TokenKind::String;
    }
    bool allAlpha = true;
    for (const char ch : type) {
        if (std::isalpha(static_cast<unsigned char>(ch)) == 0) {
            allAlpha = false;
            break;
        }
    }
    return allAlpha ? TokenKind::Keyword : TokenKind::Punctuation;
}

[[nodiscard]] inline TokenKind classifyLeaf(TSNode node, const LeafKindTable& namedKinds) {
    const std::string_view type = ts_node_type(node);
    if (ts_node_is_named(node)) {
        const auto it = namedKinds.find(type);
        return it != namedKinds.end() ? it->second : TokenKind::Text;
    }
    return classifyAnonymousLeaf(type);
}

// Phase 7n1: true for an actual leaf (ts_node_child_count()==0, the original
// Phase 7a condition) OR a named node whose type has an explicit table
// entry - the latter case exists because tree-sitter-rust's line_comment/
// block_comment are NOT leaves (they wrap anonymous "//"/"/*"/"*/" delimiter
// children, with the comment body itself uncaptured by any child - verified
// via a standalone probe), so treating them as leaves is the only way to
// color the whole node as one Comment token instead of misclassifying just
// the delimiters as Punctuation and silently dropping the body from the
// token stream. Harmless for every other language in this table (Cpp/
// Python/C/JavaScript/Java/Go's comment-equivalent node types are already
// genuine leaves, so the OR's second branch is redundant-but-true for them,
// never changing behavior established since Phase 7a).
[[nodiscard]] inline bool isAtomicNode(TSNode node, const LeafKindTable& namedKinds) {
    if (ts_node_child_count(node) == 0) {
        return true;
    }
    return ts_node_is_named(node) && namedKinds.contains(ts_node_type(node));
}

inline void appendLeafToken(std::vector<Token>& tokens, TSNode node, const LeafKindTable& namedKinds) {
    // tree-sitter byte offsets are always even for TSInputEncodingUTF16LE
    // input (2 bytes per UTF-16 code unit); dividing by 2 recovers the
    // document::TextPos-style code-unit offset (verified via a standalone
    // probe before this module was written, see ADR-014).
    const document::TextPos start = ts_node_start_byte(node) / 2;
    const document::TextPos end   = ts_node_end_byte(node) / 2;
    if (start == end) {
        return;  // zero-width leaf (tree-sitter's own "missing token" error recovery nodes)
    }
    tokens.push_back(Token{.range = {.start = start, .end = end}, .kind = classifyLeaf(node, namedKinds)});
}

// Iterative pre-order walk (TSTreeCursor carries its own stack, so this
// avoids C++ call-stack recursion depth concerns for deeply nested
// expressions) - descends into every node, appending a Token for each atomic
// node (see isAtomicNode() above) it reaches, in left-to-right document
// order.
[[nodiscard]] inline std::vector<Token> walkTree(TSNode root, const LeafKindTable& namedKinds) {
    std::vector<Token> tokens;
    TSTreeCursor        cursor     = ts_tree_cursor_new(root);
    bool                descending = true;

    while (true) {
        if (descending) {
            const TSNode node = ts_tree_cursor_current_node(&cursor);
            if (isAtomicNode(node, namedKinds)) {
                appendLeafToken(tokens, node, namedKinds);
                descending = false;
            } else if (!ts_tree_cursor_goto_first_child(&cursor)) {
                descending = false;
            }
        } else if (ts_tree_cursor_goto_next_sibling(&cursor)) {
            descending = true;
        } else if (!ts_tree_cursor_goto_parent(&cursor)) {
            break;  // back at the root with nowhere left to go
        }
    }

    ts_tree_cursor_delete(&cursor);
    return tokens;
}

}  // namespace neomifes::syntax::detail
