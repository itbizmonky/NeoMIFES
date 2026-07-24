#pragma once

// neomifes::syntax - headless tokenizer built on tree-sitter (ADR-014,
// Phase 7a). Given UTF-16 source text, parseCpp() returns a flat token
// stream suitable for coloring - no Document/RenderPipeline dependency, no
// async/incremental reparsing (single-shot ts_parser_parse_string_encoding()
// per call). Both are deferred to later Phase 7 sub-phases; see
// master_roadmap.md sec.7 "実装後の確定事項".
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
};

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

}  // namespace neomifes::syntax
