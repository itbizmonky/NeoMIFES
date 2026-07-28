#pragma once

// neomifes::syntax - headless tokenizer built on tree-sitter (ADR-014,
// Phase 7a). Given UTF-16 source text, parseCpp()/parsePython()/parse()
// return a flat token stream suitable for coloring - no Document/
// RenderPipeline dependency, no async/incremental reparsing (single-shot
// ts_parser_parse_string_encoding() per call). Both are deferred to later
// Phase 7 sub-phases; see master_roadmap.md sec.7 "実装後の確定事項".
//
// tree-sitter types (TSNode/TSTree/...) never appear in this header - they
// are an implementation detail confined to syntax.cpp, matching the
// nlohmann::json precedent (ADR-013).

#include <cstdint>
#include <string_view>
#include <vector>

#include "neomifes/document/text_pos.h"

namespace neomifes::syntax {

// Deliberately narrower than master_roadmap.md sec.7.3's full sketch (which
// also lists Function/Operator/TypeParameter/Enum/Namespace/Interface/
// Attribute/Error plus a `modifiers` bitfield). Phase 7a's node-type mapping
// table (see syntax.cpp) does not produce those values yet - Function would
// need parent-node context (call_expression/function_declarator) beyond a
// single leaf's type name, Operator vs. Punctuation has no crisp boundary in
// tree-sitter-cpp's anonymous token set, and the rest are LSP semantic-token
// concerns (Phase 11+). Following the same "don't put unimplemented
// enumerators in a public API" rule established for encoding::Encoding
// (Phase 6a), those are added only once a mapping actually produces them.
enum class TokenKind {
    Text,          // fallback: no specific classification applies
    Keyword,       // class/const/if/return/... plus true/false/this/etc.
    Type,          // primitive_type, type_identifier, namespace_identifier
    Variable,      // identifier, field_identifier, statement_identifier, ...
    Number,        // number_literal
    String,        // string/char literal bodies, escape sequences, quotes
    Comment,
    Punctuation,   // braces, operators, separators - see syntax.cpp
    Preprocessor,  // #include/#define/... directives and their bodies
};

struct Token {
    document::TextRange range;  // UTF-16 code-unit offsets, same convention as document::TextPos
    TokenKind           kind = TokenKind::Text;

    friend constexpr bool operator==(const Token&, const Token&) = default;
};

// Phase 7d (multi-language dispatch generalization): only languages with an
// actual tree-sitter grammar wired into cmake/Dependencies.cmake are listed
// here, matching the same "don't put unimplemented enumerators in a public
// API" rule cited for TokenKind above. Adding a language repeats the same
// pattern (grammar FetchContent block + namedLeafKindsForX() table in
// syntax_internal.h + parseX() below) rather than requiring a new
// abstraction - see master_roadmap.md sec.7 "実装後の確定事項" for the
// Phase 7d rationale. Phase 7n1 added C/JavaScript/Java/Go/Rust/Json (the
// first batch of roadmap sec.7.2's remaining required languages); the
// Language -> TSLanguage* mapping itself was centralized into
// syntax_internal.h's detail::tsLanguageFor() at the same time so syntax.cpp/
// incremental_parser.cpp/outline.cpp share one switch instead of each
// maintaining their own (see outline.cpp's Phase 7n1 comment for why that
// mattered).
enum class Language { Cpp, Python, C, JavaScript, Java, Go, Rust, Json };

// Parses `text` as C++ and returns a flat, left-to-right, non-overlapping
// token stream covering every leaf of the syntax tree (whitespace and
// newlines are skipped - tree-sitter has no leaf node for them). Synchronous,
// single-shot parse (no incremental reparse - see header comment above).
//
// tree-sitter never fails to produce a tree, even for malformed input (it
// returns a tree containing error nodes); this function mirrors that and
// never fails either - a syntactically invalid `text` still yields tokens,
// classified the same as valid input would be, with no attempt to flag the
// error location (no TokenKind::Error - see enum comment above).
[[nodiscard]] std::vector<Token> parseCpp(std::u16string_view text);

// Same contract as parseCpp(), for Python (tree-sitter-python v0.25.0).
[[nodiscard]] std::vector<Token> parsePython(std::u16string_view text);

// Same contract as parseCpp(), for C (tree-sitter-c v0.24.2).
[[nodiscard]] std::vector<Token> parseC(std::u16string_view text);

// Same contract as parseCpp(), for JavaScript (tree-sitter-javascript v0.25.0).
[[nodiscard]] std::vector<Token> parseJavaScript(std::u16string_view text);

// Same contract as parseCpp(), for Java (tree-sitter-java v0.23.5).
[[nodiscard]] std::vector<Token> parseJava(std::u16string_view text);

// Same contract as parseCpp(), for Go (tree-sitter-go v0.25.0).
[[nodiscard]] std::vector<Token> parseGo(std::u16string_view text);

// Same contract as parseCpp(), for Rust (tree-sitter-rust v0.24.2).
[[nodiscard]] std::vector<Token> parseRust(std::u16string_view text);

// Same contract as parseCpp(), for JSON (tree-sitter-json v0.24.8).
[[nodiscard]] std::vector<Token> parseJson(std::u16string_view text);

// Thin dispatcher over parseCpp()/parsePython()/parseC()/parseJavaScript()/
// parseJava()/parseGo()/parseRust()/parseJson(). Kept alongside the
// individual functions (rather than replacing them) so existing callers/
// tests that only care about one language can keep calling it directly, and
// so per-language expected-output tests stay easy to write (Phase 7b/7c
// precedent) - see syntax_syntax_test.cpp.
[[nodiscard]] std::vector<Token> parse(std::u16string_view text, Language language);

}  // namespace neomifes::syntax
