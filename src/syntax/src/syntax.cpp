#include "neomifes/syntax/syntax.h"

#include <tree_sitter/api.h>

#include <cctype>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace neomifes::syntax {

namespace {

// Declared by the tree-sitter-cpp grammar (bindings/c/tree-sitter-cpp.h) -
// not included directly so this translation unit doesn't need that grammar
// repo's include path wired in just for one function declaration (see
// cmake/Dependencies.cmake's tree-sitter-cpp-grammar target, ADR-014). Name
// is fixed by that C ABI, not ours to rename.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_cpp(void);

using TSParserPtr = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using TSTreePtr   = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;

[[nodiscard]] TSParserPtr makeCppParser() {
    TSParserPtr parser(ts_parser_new(), &ts_parser_delete);
    ts_parser_set_language(parser.get(), tree_sitter_cpp());
    return parser;
}

// Named leaf node types (ts_node_child_count() == 0, ts_node_is_named() ==
// true) that need a specific TokenKind. Built from tree-sitter-cpp v0.23.4's
// src/node-types.json (230 named types) cross-checked against real parser
// output for representative C++ snippets (declarations/preprocessor/string &
// char literals/member access) - not guessed from memory (CLAUDE.md rule 3).
// Named leaf types absent from this table (e.g. `raw_string_delimiter`,
// `destructor_name`, `operator_name`) fall through to TokenKind::Text.
const std::unordered_map<std::string_view, TokenKind>& namedLeafKinds() {
    static const std::unordered_map<std::string_view, TokenKind> table{
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

// Anonymous leaf types (literal keywords/operators/punctuation, e.g. "class",
// "+=", "{") have no node-types.json entry worth tabulating individually -
// tree-sitter-cpp's grammar has ~200 of them. Classified structurally
// instead: alphabetic strings are C++ keywords by construction (no operator
// or punctuation token in the C++ grammar is purely alphabetic), a leading
// '#' marks a preprocessor directive keyword ("#include", "#define", ...),
// and the quote delimiters '"'/'\'' are grouped with TokenKind::String so
// the whole literal (open quote + content + close quote) colors uniformly.
// Everything else (braces, separators, arithmetic/logical/comparison
// operators) becomes Punctuation - see syntax.h's TokenKind comment for why
// Operator is not a separate value yet.
[[nodiscard]] TokenKind classifyAnonymousLeaf(std::string_view type) {
    if (type.empty()) {
        return TokenKind::Text;
    }
    if (type.front() == '#') {
        return TokenKind::Preprocessor;
    }
    if (type == "\"" || type == "'") {
        return TokenKind::String;
    }
    // Node type strings are grammar-defined ASCII tokens (C++ keywords have
    // no non-ASCII spelling), so plain std::isalpha is sufficient here - the
    // same "ASCII range only" scope already established for util::globMatch()
    // and util::fuzzyMatchScore()'s casefolding.
    bool allAlpha = true;
    for (const char ch : type) {
        if (std::isalpha(static_cast<unsigned char>(ch)) == 0) {
            allAlpha = false;
            break;
        }
    }
    return allAlpha ? TokenKind::Keyword : TokenKind::Punctuation;
}

[[nodiscard]] TokenKind classifyLeaf(TSNode node) {
    const std::string_view type = ts_node_type(node);
    if (ts_node_is_named(node)) {
        const auto& table = namedLeafKinds();
        const auto  it     = table.find(type);
        return it != table.end() ? it->second : TokenKind::Text;
    }
    return classifyAnonymousLeaf(type);
}

void appendLeafToken(std::vector<Token>& tokens, TSNode node) {
    // tree-sitter byte offsets are always even for TSInputEncodingUTF16LE
    // input (2 bytes per UTF-16 code unit); dividing by 2 recovers the
    // document::TextPos-style code-unit offset (verified via a standalone
    // probe before this module was written, see ADR-014).
    const document::TextPos start = ts_node_start_byte(node) / 2;
    const document::TextPos end   = ts_node_end_byte(node) / 2;
    if (start == end) {
        return;  // zero-width leaf (tree-sitter's own "missing token" error recovery nodes)
    }
    tokens.push_back(Token{.range = {.start = start, .end = end}, .kind = classifyLeaf(node)});
}

// Iterative pre-order walk (TSTreeCursor carries its own stack, so this
// avoids C++ call-stack recursion depth concerns for deeply nested
// expressions) - descends into every node, appending a Token for each leaf
// (ts_node_child_count() == 0) it reaches, in left-to-right document order.
[[nodiscard]] std::vector<Token> walkTree(TSNode root) {
    std::vector<Token> tokens;
    TSTreeCursor        cursor     = ts_tree_cursor_new(root);
    bool                descending = true;

    while (true) {
        if (descending) {
            const TSNode node = ts_tree_cursor_current_node(&cursor);
            if (ts_node_child_count(node) == 0) {
                appendLeafToken(tokens, node);
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

}  // namespace

std::vector<Token> parseCpp(std::u16string_view text) {
    const TSParserPtr parser = makeCppParser();
    const auto*        bytes  = reinterpret_cast<const char*>(text.data());
    const auto          length = static_cast<uint32_t>(text.size() * sizeof(char16_t));

    const TSTreePtr tree(
        ts_parser_parse_string_encoding(parser.get(), nullptr, bytes, length, TSInputEncodingUTF16LE),
        &ts_tree_delete);
    if (!tree) {
        return {};  // ts_parser_parse_string_encoding only returns NULL if no language is set
    }

    return walkTree(ts_tree_root_node(tree.get()));
}

}  // namespace neomifes::syntax
