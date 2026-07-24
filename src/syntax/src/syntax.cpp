#include "neomifes/syntax/syntax.h"

#include <tree_sitter/api.h>

#include <cctype>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace neomifes::syntax {

namespace {

// Declared by the tree-sitter-cpp / tree-sitter-python grammars (their
// bindings/c/*.h headers) - not included directly so this translation unit
// doesn't need those grammar repos' include paths wired in just for one
// function declaration each (see cmake/Dependencies.cmake's
// tree-sitter-cpp-grammar/tree-sitter-python-grammar targets, ADR-014). Names
// are fixed by that C ABI, not ours to rename.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_cpp(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_python(void);

using TSParserPtr = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using TSTreePtr   = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;

[[nodiscard]] TSParserPtr makeParser(const TSLanguage* language) {
    TSParserPtr parser(ts_parser_new(), &ts_parser_delete);
    ts_parser_set_language(parser.get(), language);
    return parser;
}

using LeafKindTable = std::unordered_map<std::string_view, TokenKind>;

// Named leaf node types (ts_node_child_count() == 0, ts_node_is_named() ==
// true) that need a specific TokenKind. Built from tree-sitter-cpp v0.23.4's
// src/node-types.json (230 named types) cross-checked against real parser
// output for representative C++ snippets (declarations/preprocessor/string &
// char literals/member access) - not guessed from memory (CLAUDE.md rule 3).
// Named leaf types absent from this table (e.g. `raw_string_delimiter`,
// `destructor_name`, `operator_name`) fall through to TokenKind::Text.
const LeafKindTable& namedLeafKindsForCpp() {
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

// Built from tree-sitter-python v0.25.0's src/node-types.json (the pure-
// terminal named types, i.e. entries with neither `fields` nor `children`)
// cross-checked against real parser output from a standalone probe covering
// function defs/classes/decorators/comments/numbers, f-strings (string_start/
// string_content/interpolation/type_conversion/string_end), raw/byte strings,
// escape sequences, async/await/lambda/walrus/comprehensions, and boolean
// operators (Phase 7d plan) - not guessed from memory (CLAUDE.md rule 3).
// Unlike C++, Python's grammar has no separate "type" node for annotations
// (`x: int` parses `int` as a plain `identifier`) - so no entry here ever
// produces TokenKind::Type; that is an accepted limitation, not an omission.
const LeafKindTable& namedLeafKindsForPython() {
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

// Anonymous leaf types (literal keywords/operators/punctuation, e.g. "class",
// "+=", "{") have no node-types.json entry worth tabulating individually -
// tree-sitter-cpp's grammar alone has ~200 of them. Classified structurally
// instead: alphabetic strings are keywords by construction in both grammars
// (no operator or punctuation token in either the C++ or the Python grammar
// is purely alphabetic - verified for Python via the Phase 7d standalone
// probe, which is why this function stays shared rather than growing a
// per-language variant), a leading '#' marks a C++ preprocessor directive
// keyword ("#include", "#define", ... - never produced by the Python grammar,
// so harmless there), and the quote delimiters '"'/'\'' are grouped with
// TokenKind::String so a whole C++ literal (open quote + content + close
// quote) colors uniformly (Python's grammar has no bare quote token - its
// quotes are folded into the named string_start/string_end nodes above, so
// this branch is simply never reached for Python input). Everything else
// (braces, separators, arithmetic/logical/comparison operators) becomes
// Punctuation - see syntax.h's TokenKind comment for why Operator is not a
// separate value yet.
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
    // Node type strings are grammar-defined ASCII tokens (keywords have no
    // non-ASCII spelling in either grammar), so plain std::isalpha is
    // sufficient here - the same "ASCII range only" scope already established
    // for util::globMatch() and util::fuzzyMatchScore()'s casefolding.
    bool allAlpha = true;
    for (const char ch : type) {
        if (std::isalpha(static_cast<unsigned char>(ch)) == 0) {
            allAlpha = false;
            break;
        }
    }
    return allAlpha ? TokenKind::Keyword : TokenKind::Punctuation;
}

[[nodiscard]] TokenKind classifyLeaf(TSNode node, const LeafKindTable& namedKinds) {
    const std::string_view type = ts_node_type(node);
    if (ts_node_is_named(node)) {
        const auto it = namedKinds.find(type);
        return it != namedKinds.end() ? it->second : TokenKind::Text;
    }
    return classifyAnonymousLeaf(type);
}

void appendLeafToken(std::vector<Token>& tokens, TSNode node, const LeafKindTable& namedKinds) {
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
[[nodiscard]] std::vector<Token> walkTree(TSNode root, const LeafKindTable& namedKinds) {
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

[[nodiscard]] std::vector<Token> parseWithLanguage(std::u16string_view text, const TSLanguage* language,
                                                    const LeafKindTable& namedKinds) {
    const TSParserPtr parser = makeParser(language);
    const auto*        bytes  = reinterpret_cast<const char*>(text.data());
    const auto          length = static_cast<uint32_t>(text.size() * sizeof(char16_t));

    const TSTreePtr tree(
        ts_parser_parse_string_encoding(parser.get(), nullptr, bytes, length, TSInputEncodingUTF16LE),
        &ts_tree_delete);
    if (!tree) {
        return {};  // ts_parser_parse_string_encoding only returns NULL if no language is set
    }

    return walkTree(ts_tree_root_node(tree.get()), namedKinds);
}

}  // namespace

std::vector<Token> parseCpp(std::u16string_view text) {
    return parseWithLanguage(text, tree_sitter_cpp(), namedLeafKindsForCpp());
}

std::vector<Token> parsePython(std::u16string_view text) {
    return parseWithLanguage(text, tree_sitter_python(), namedLeafKindsForPython());
}

std::vector<Token> parse(std::u16string_view text, Language language) {
    switch (language) {
        case Language::Cpp:
            return parseCpp(text);
        case Language::Python:
            return parsePython(text);
    }
    return {};  // unreachable (all Language enumerators handled above)
}

}  // namespace neomifes::syntax
