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

// See syntax.cpp's original comment (Phase 7a/7d) for the classification
// rationale (structural: alphabetic -> keyword, leading '#' -> preprocessor,
// quote delimiters -> string, everything else -> punctuation).
[[nodiscard]] inline TokenKind classifyAnonymousLeaf(std::string_view type) {
    if (type.empty()) {
        return TokenKind::Text;
    }
    if (type.front() == '#') {
        return TokenKind::Preprocessor;
    }
    if (type == "\"" || type == "'") {
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
// expressions) - descends into every node, appending a Token for each leaf
// (ts_node_child_count() == 0) it reaches, in left-to-right document order.
[[nodiscard]] inline std::vector<Token> walkTree(TSNode root, const LeafKindTable& namedKinds) {
    std::vector<Token> tokens;
    TSTreeCursor        cursor     = ts_tree_cursor_new(root);
    bool                descending = true;

    while (true) {
        if (descending) {
            const TSNode node = ts_tree_cursor_current_node(&cursor);
            if (ts_node_child_count(node) == 0) {
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
