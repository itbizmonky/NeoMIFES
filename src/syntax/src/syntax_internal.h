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
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_html(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_css(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_bash(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_yaml(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_toml(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_xml(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_typescript(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_tsx(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_php(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_markdown(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_powershell(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_ini(void);
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" const TSLanguage* tree_sitter_batch(void);

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
        case Language::Html:
            return tree_sitter_html();
        case Language::Css:
            return tree_sitter_css();
        case Language::Shell:
            return tree_sitter_bash();
        case Language::Yaml:
            return tree_sitter_yaml();
        case Language::Toml:
            return tree_sitter_toml();
        case Language::Xml:
            return tree_sitter_xml();
        case Language::TypeScript:
            return tree_sitter_typescript();
        case Language::Tsx:
            return tree_sitter_tsx();
        case Language::Php:
            return tree_sitter_php();
        case Language::Markdown:
            return tree_sitter_markdown();
        case Language::PowerShell:
            return tree_sitter_powershell();
        case Language::Ini:
            return tree_sitter_ini();
        case Language::Batch:
            return tree_sitter_batch();
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

// Verified via a standalone probe (Phase 7r) against real tree-sitter-html
// v0.23.2 output. "text" (element body text) and "raw_text" (<script>/
// <style> body) are genuine leaves too but deliberately left unclassified
// (fall through to the default TokenKind::Text) - this batch does not
// attempt embedded-language highlighting for <script>/<style> contents.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForHtml() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"tag_name", TokenKind::Type},
        {"attribute_name", TokenKind::Variable},
        {"attribute_value", TokenKind::String},
        {"entity", TokenKind::String},
    };
    return table;
}

// Verified via a standalone probe (Phase 7r) against real tree-sitter-css
// v0.25.0 output. "plain_value" (e.g. the "red" in "color: red;") is left
// unclassified - the probe sample only exercised keyword-like values, and
// CLAUDE.md rule 3 forbids extending the table to numeric/dimension values
// that were never actually observed in real parser output.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForCss() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"property_name", TokenKind::Variable},
        {"identifier", TokenKind::Variable},
        {"id_name", TokenKind::Type},
        {"tag_name", TokenKind::Type},
        {"string_content", TokenKind::String},
    };
    return table;
}

// Verified via a standalone probe (Phase 7r) against real tree-sitter-bash
// v0.25.1 output (used for Language::Shell). "word" is bash's generic
// bareword node (command names, plain arguments, ...) - classified as
// Variable for lack of a more precise structural signal, matching this
// table's existing "best defensible bucket, not a perfect one" precedent
// (see e.g. namedLeafKindsForJson()'s comment above).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForBash() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"variable_name", TokenKind::Variable},
        {"number", TokenKind::Number},
        {"word", TokenKind::Variable},
        {"string_content", TokenKind::String},
    };
    return table;
}

// Verified via a standalone probe (Phase 7r) against real tree-sitter-yaml
// v0.7.2 output. tree-sitter-yaml's grammar does not distinguish a mapping
// key from a plain scalar value at the node-type level - both are
// "string_scalar" (visible in the probe: "key" and "value" in "key: value"
// are both string_scalar) - so map keys are colored as strings too, an
// accepted grammar-level limitation (same class of trade-off as JSON object
// keys sharing string_content with JSON string values).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForYaml() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"string_scalar", TokenKind::String},
        {"integer_scalar", TokenKind::Number},
        {"boolean_scalar", TokenKind::Keyword},
        {"null_scalar", TokenKind::Keyword},
    };
    return table;
}

// Verified via a standalone probe (Phase 7r) against real tree-sitter-toml
// v0.7.0 output. "string" is NOT a true leaf (children=2: only the two
// anonymous quote-delimiter children, no separate content child) - same
// non-leaf-atomic-node situation as tree-sitter-rust's line_comment/
// block_comment (see isAtomicNode()'s Phase 7n1 comment above). Without this
// table entry, isAtomicNode() would descend into "string" and only emit
// Punctuation tokens for the two quote characters, silently dropping the
// quoted text itself from the token stream.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForToml() {
    static const LeafKindTable table{
        {"bare_key", TokenKind::Variable},
        {"comment", TokenKind::Comment},
        {"integer", TokenKind::Number},
        {"boolean", TokenKind::Keyword},
        {"string", TokenKind::String},  // non-leaf, quote-only children - see comment above
    };
    return table;
}

// Verified via a standalone probe (Phase 7r) against real tree-sitter-xml
// v0.7.0 output. Two non-obvious findings from the probe:
//  - "Name" is used by this grammar for BOTH element tag names and
//    attribute names (no separate AttributeName node type exists) - so
//    attribute names are colored as Type too, an accepted grammar-level
//    limitation (same trade-off class as YAML's key/value ambiguity above).
//  - "AttValue" is NOT a true leaf (children=2: only the two anonymous
//    quote-delimiter children, no separate content child) - the exact same
//    situation as TOML's "string" above (and tree-sitter-rust's comments
//    before that). Without this entry, the quoted attribute value text
//    (e.g. "val" in attr="val") would be silently dropped from the token
//    stream.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForXml() {
    static const LeafKindTable table{
        {"Name", TokenKind::Type},
        {"Comment", TokenKind::Comment},
        {"AttValue", TokenKind::String},  // non-leaf, quote-only children - see comment above
    };
    return table;
}

// Verified via a standalone probe (Phase 7s) against real tree-sitter-
// typescript v0.23.2 output. tree-sitter-typescript's grammar extends
// tree-sitter-javascript's (documented upstream architecture) - every node
// type this table shares with namedLeafKindsForJavaScript() above
// (comment/number/identifier/property_identifier/string_fragment/
// escape_sequence/regex_pattern/regex_flags/true/false/null/undefined/
// this/super) was independently re-probed here and confirmed identical,
// not assumed from the inheritance relationship alone. "type_identifier"
// (interface/class names) and "predefined_type" are TypeScript-only
// additions. "predefined_type" is NOT a true leaf (children=1: a single
// anonymous child - e.g. "number" - spanning the exact same byte range as
// the parent) but is registered anyway for consistency with how Cpp/Rust's
// own "primitive_type" colors built-in type keywords as Type rather than
// falling through to classifyAnonymousLeaf()'s generic alphabetic-keyword
// rule (unlike TOML's "string"/XML's "AttValue" above, this one is not a
// data-loss bug fix - the single child already covers the full span - just
// a deliberate cross-language consistency choice).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForTypeScript() {
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
        {"type_identifier", TokenKind::Type},
        {"predefined_type", TokenKind::Type},  // non-leaf, see comment above
    };
    return table;
}

// tree-sitter-tsx is TypeScript's grammar plus JSX-only rules (documented
// upstream architecture, same repo/release as TypeScript above). Verified
// via the same standalone probe that every leaf type TSX's own sample
// exercised (interface/function/JSX element/attribute/expression) reuses
// TypeScript's exact node names (identifier/property_identifier/comment/
// type_identifier/string/string_fragment) - no JSX-specific named leaf type
// was found needing its own table entry, so this shares TypeScript's table
// verbatim rather than duplicating it (any JSX-only leaf this sample didn't
// exercise, e.g. plain JSX text content, would safely fall through to the
// default Text classification if ever encountered - not a data-loss risk
// the way an unregistered non-leaf node would be).
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForTsx() {
    return namedLeafKindsForTypeScript();
}

// Verified via a standalone probe (Phase 7s) against real tree-sitter-php
// v0.24.2 output (the php/ grammar, not php_only/ - see Dependencies.cmake's
// comment for why). "php_tag"/"php_end_tag" (the "<?php"/"?>" delimiters)
// are classified as Preprocessor, matching this table's existing convention
// for other source/non-source boundary markers (C++'s "#include" etc.) -
// text outside these tags (plain embedded HTML, node type "text") is left
// unclassified (default Text), the same "no embedded-language highlighting"
// simplification already accepted for HTML's raw_text/CSS's plain_value.
// "name" is reused by this grammar for both function/variable names (e.g.
// "foo" in a function_definition, the "x" inside a "$x" variable_name) -
// same kind of grammar-level reuse already accepted for YAML's
// string_scalar/XML's Name above.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForPhp() {
    static const LeafKindTable table{
        {"php_tag", TokenKind::Preprocessor},
        {"php_end_tag", TokenKind::Preprocessor},
        {"comment", TokenKind::Comment},
        {"name", TokenKind::Variable},
        {"integer", TokenKind::Number},
        {"string_content", TokenKind::String},
    };
    return table;
}

// Verified via a standalone probe (Phase 7s) against real tree-sitter-
// markdown v0.5.3 output (the block-level tree-sitter-markdown grammar
// only - see Dependencies.cmake's comment for why tree-sitter-markdown-
// inline is out of scope). Markdown's grammar has no comment concept
// (matching JSON's own "deliberately much smaller table" precedent) and,
// without the inline grammar's language injection, most paragraph/heading
// text is exposed by the block grammar's own "inline" node only as a mix of
// a few flanking delimiter characters (e.g. the "*"/"`" around emphasis/
// code spans - which classifyAnonymousLeaf() already colors as Punctuation/
// String respectively, an incidental but harmless side effect of existing
// logic) with the actual prose words left uncovered by any node at all -
// this renders identically to an explicit Text token (both fall back to the
// editor's default text color) so no table entry is needed to paper over
// it. "language" (the info-string language tag on a fenced code block, e.g.
// "js" in "```js") is the one addition with a clean semantic fit.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForMarkdown() {
    static const LeafKindTable table{
        {"language", TokenKind::Type},
    };
    return table;
}

// Verified via a standalone probe (Phase 7x) against real tree-sitter-
// powershell output (airbus-cert/tree-sitter-powershell, commit
// e7bd348c, ADR-015's Phase 7x scope - a community grammar, not from the
// tree-sitter/ or tree-sitter-grammars/ orgs, see Dependencies.cmake's
// comment). Two grammar-specific findings from the probe:
//  - `$true`/`$false`/`$null` are NOT distinct boolean/null node types -
//    PowerShell treats them as automatic *variables* syntactically, and the
//    probe confirms they parse as plain "variable" nodes indistinguishable
//    from "$name"/"$num" - so they get Variable's color, not Keyword's (an
//    accepted, grammar-driven simplification, not a probe oversight).
//  - "expandable_string_literal" (double-quoted, e.g. "Hello, $who!") is a
//    genuine leaf when it contains no interpolation, but gains a nested
//    "variable" child when it does (verified with both cases) - registering
//    it here makes isAtomicNode() treat it as atomic in EITHER case (this
//    table's membership test, not just the child-count-0 test), so an
//    interpolated variable inside a string is colored uniformly as String
//    rather than getting its own nested Variable token. Same trade-off
//    class as this table's Rust/TOML precedents (see namedLeafKindsForRust()/
//    namedLeafKindsForToml() comments above).
// "comparison_operator" (wraps "-gt"/"-lt" etc.) is deliberately NOT
// registered: the probe shows "-and"/"-or" appear as bare ANONYMOUS tokens
// at the same syntax level (not wrapped in a named node), which
// classifyAnonymousLeaf() already colors as Punctuation (leading '-' fails
// its all-alphabetic check) - leaving comparison_operator unregistered lets
// it fall through to the same Punctuation coloring via descent, keeping
// "-gt"/"-lt"/"-and"/"-or" visually consistent instead of splitting them
// across two different colors depending on which the grammar happened to
// wrap in a named node.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForPowerShell() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},
        {"decimal_integer_literal", TokenKind::Number},
        {"variable", TokenKind::Variable},  // covers $var AND $true/$false/$null - see comment above
        {"expandable_string_literal", TokenKind::String},  // non-leaf when interpolated - see comment above
        {"verbatim_string_characters", TokenKind::String},  // single-quoted 'literal'
        {"function_name", TokenKind::Variable},
        {"command_name", TokenKind::Variable},  // cmdlet/command invocation name (no Function TokenKind exists)
        {"generic_token", TokenKind::Variable},  // bareword command argument (same bucket as Bash's "word")
    };
    return table;
}

// Verified via a standalone probe (Phase 7x) against real tree-sitter-ini
// output (justinmk/tree-sitter-ini v1.4.0, a community grammar - see
// Dependencies.cmake's comment). "section_name" wraps the anonymous "["/"]"
// delimiters plus a "text" child (children=3, not a true leaf) - registering
// it here makes the WHOLE "[section]" span (brackets included) atomic and
// colored as one Type token, rather than descending and losing the bracket
// delimiters to Punctuation while only the inner text gets colored. This
// grammar distinguishes setting_name (the key) from setting_value (the
// value) as separate node types - unlike tree-sitter-yaml's single shared
// "string_scalar" for both (see namedLeafKindsForYaml()'s comment above),
// so key and value get genuinely different colors here rather than a forced
// uniform one.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForIni() {
    static const LeafKindTable table{
        {"comment", TokenKind::Comment},  // covers both ';' and '#' comment delimiters (both probed)
        {"section_name", TokenKind::Type},  // non-leaf ([name]) - see comment above
        {"setting_name", TokenKind::Variable},
        {"setting_value", TokenKind::String},
    };
    return table;
}

// Verified via a standalone probe (Phase 7x) against real tree-sitter-batch
// output (wharflab/tree-sitter-batch v0.11.1, a community grammar - see
// Dependencies.cmake's comment). Several grammar-specific findings:
//  - "echo_off" (the "@echo off" header line) has only ONE actual child (the
//    anonymous "@"), leaving "echo off" itself uncovered by any node -
//    registering "echo_off" here makes the whole "@echo off" span atomic
//    and colored as one Preprocessor token (matching this table's existing
//    convention for other source/directive boundary markers, e.g. PHP's
//    php_tag), instead of coloring only the "@" and leaving "echo off"
//    unstyled.
//  - "set_keyword" (the literal "set" in `set VAR=value`) and "goto_stmt"
//    (the ENTIRE "goto target" span, itself a genuine leaf with no further
//    decomposition - confirmed via probe on both "goto mylabel" and
//    "goto :eof") are dedicated NAMED node types, not anonymous alphabetic
//    tokens - classifyAnonymousLeaf()'s generic "alphabetic anonymous token
//    -> Keyword" rule does NOT apply to named nodes, so both need explicit
//    table entries to be colored as Keyword at all (otherwise they default
//    to Text).
//  - "comparison_op" (e.g. "==" inside an `if` comparison) is a genuine
//    leaf but deliberately left unregistered - no TokenKind bucket fits it
//    cleanly (not quite Keyword, not quite Punctuation since it bypasses
//    classifyAnonymousLeaf() by being a named node), an accepted minor gap
//    of the same class as this table's CSS plain_value/HTML raw_text
//    precedents.
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForBatch() {
    static const LeafKindTable table{
        {"echo_off", TokenKind::Preprocessor},  // non-leaf ("@echo off") - see comment above
        {"comment", TokenKind::Comment},  // covers both "REM" and "::" comment syntaxes (both probed)
        {"set_keyword", TokenKind::Keyword},
        {"variable_name", TokenKind::Variable},  // declared name in `set VAR=...`
        {"assignment_literal", TokenKind::String},  // the value literal in `set VAR=value`
        {"command_name", TokenKind::Variable},  // invoked command name, e.g. "echo" (no Function TokenKind exists)
        {"variable_reference", TokenKind::Variable},  // %VAR% expansion
        {"label", TokenKind::Type},  // :label definition
        {"string", TokenKind::String},  // quoted literal, e.g. in `if "%VAR%"=="hello"`
        {"goto_stmt", TokenKind::Keyword},  // whole "goto target" span - see comment above
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
