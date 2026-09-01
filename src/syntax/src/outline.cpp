#include "neomifes/syntax/outline.h"

#include <tree_sitter/api.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "syntax_internal.h"

namespace neomifes::syntax {

namespace {

using detail::tsLanguageFor;

using TSParserPtr = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using TSTreePtr   = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;

// Deliberately duplicated from syntax.cpp's identical helper rather than
// shared - two lines, and this file's parse is already an independent
// second parse (see outline.h's header comment); factoring out a shared
// internal header for this alone isn't worth it yet (Phase 7e's kTabWidth
// duplication precedent).
[[nodiscard]] TSParserPtr makeParser(const TSLanguage* language) {
    TSParserPtr parser(ts_parser_new(), &ts_parser_delete);
    ts_parser_set_language(parser.get(), language);
    return parser;
}

using SymbolTable = std::unordered_map<std::string_view, SymbolKind>;

// Node types verified via a standalone probe (ts_probe_outline) against real
// tree-sitter-cpp v0.23.4 output before writing this table - see the Phase
// 7f plan's Context section. class_specifier/struct_specifier/
// namespace_definition all expose the symbol name directly via a "name"
// field; function_definition does not (handled separately, see
// resolveFunctionDefinitionName() below).
const SymbolTable& symbolTableForCpp() {
    static const SymbolTable table{
        {"class_specifier", SymbolKind::Class},
        {"struct_specifier", SymbolKind::Struct},
        {"namespace_definition", SymbolKind::Namespace},
        {"function_definition", SymbolKind::Function},
    };
    return table;
}

// Verified via the same probe against tree-sitter-python v0.25.0: both node
// types expose the symbol name directly via a required "name" field
// (identifier) - no unwrap needed, unlike C++'s function_definition.
const SymbolTable& symbolTableForPython() {
    static const SymbolTable table{
        {"function_definition", SymbolKind::Function},
        {"class_definition", SymbolKind::Class},
    };
    return table;
}

// Phase 7n1: C/JavaScript/Java/Go/Rust/Json get an empty table (no symbol
// definitions recognized) rather than symbol-extraction logic of their own -
// Phase 7r extended this same empty-table treatment to Html/Css/Shell/Yaml/
// Toml/Xml, and Phase 7s to TypeScript/Tsx/Php/Markdown, and Phase 7y to Sql,
// for the identical reason -
// deliberately deferred (see Phase 7n1 plan's Context section: outline
// extraction needs its own per-language declarator/name-resolution probing,
// same order of effort as symbolTableForCpp()'s resolveFunctionDefinitionName()
// unwrap chain, and doubling this batch's scope wasn't justified without a
// concrete need yet). An empty table is a SAFE degradation, not a silent
// one: extractOutline()'s own header comment already documents "text
// containing no recognized definitions yields an empty vector" as a normal,
// expected outcome - this just means every input currently falls into that
// case for these 10 languages. What this table intentionally does NOT do is
// route these languages through Cpp's or Python's table (which would invent
// fake symbols by matching an unrelated language's node-type names).
const SymbolTable& emptySymbolTable() {
    static const SymbolTable table{};
    return table;
}

// Centralizes Language -> SymbolTable the same way syntax_internal.h's
// tsLanguageFor() centralizes Language -> TSLanguage* (Phase 7n1) - see this
// file's extractOutline() for why a 2-way ternary was unsafe once Language
// grew past 2 enumerators.
const SymbolTable& symbolTableFor(Language language) {
    switch (language) {
        case Language::Cpp:
            return symbolTableForCpp();
        case Language::Python:
            return symbolTableForPython();
        case Language::C:
        case Language::JavaScript:
        case Language::Java:
        case Language::Go:
        case Language::Rust:
        case Language::Json:
        case Language::Html:
        case Language::Css:
        case Language::Shell:
        case Language::Yaml:
        case Language::Toml:
        case Language::Xml:
        case Language::TypeScript:
        case Language::Tsx:
        case Language::Php:
        case Language::Markdown:
        case Language::PowerShell:
        case Language::Ini:
        case Language::Batch:
        case Language::Sql:
            return emptySymbolTable();
    }
    return symbolTableForCpp();  // unreachable (all enumerators handled above)
}

[[nodiscard]] std::u16string textOf(TSNode node, std::u16string_view text) {
    const uint32_t startCu = ts_node_start_byte(node) / 2;
    const uint32_t endCu   = ts_node_end_byte(node) / 2;
    return std::u16string(text.substr(startCu, endCu - startCu));
}

// Returns `node`'s nested declarator, whether it's exposed via a "declarator"
// field (pointer_declarator, function_declarator) or - verified against
// tree-sitter-cpp v0.23.4's node-types.json, which lists reference_declarator
// as having "fields": {} and a single required *positional* child - carried
// as a plain unnamed-field child instead (reference_declarator). This grammar
// asymmetry between pointer_declarator and reference_declarator is a real
// tree-sitter-cpp design choice, not a probe artifact: it was confirmed by
// dumping the full parse tree for `int& getRef(int& x)`, where
// function_declarator appears as reference_declarator's child with no field
// name at all (contrast pointer_declarator, whose function_declarator child
// *is* field-labelled "declarator").
[[nodiscard]] TSNode declaratorChild(TSNode node) {
    const TSNode fieldChild = ts_node_child_by_field_name(node, "declarator", 10);
    if (!ts_node_is_null(fieldChild)) {
        return fieldChild;
    }
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        const TSNode child = ts_node_child(node, i);
        if (ts_node_is_named(child)) {
            return child;  // e.g. reference_declarator's sole positional child
        }
    }
    return TSNode{};  // ts_node_is_null() true - no candidate found
}

// C++ function_definition has no direct "name" field - the name is nested
// inside its "declarator" field, wrapped in zero or more of
// pointer_declarator/reference_declarator/function_declarator (one layer per
// `*`/`&`/parameter-list), and for out-of-line qualified definitions
// (`Widget::doThing()`) the innermost declarator is a qualified_identifier
// whose own "name" field holds just the unqualified name. This exact unwrap
// chain was verified via ts_probe_outline against 5 real cases (free
// function, member function, pointer return, reference return, qualified
// out-of-line) before being written - see the Phase 7f plan. Templates,
// operator overloads, and constructors/destructors were not probed; if the
// loop can't unwrap further it falls back to whatever node it last reached
// (a reasonable degradation, not a crash) - a known, accepted limitation.
[[nodiscard]] TSNode resolveFunctionDefinitionName(TSNode functionDefinition) {
    TSNode current = declaratorChild(functionDefinition);
    while (!ts_node_is_null(current)) {
        const std::string_view type = ts_node_type(current);
        if (type == "identifier" || type == "field_identifier") {
            return current;
        }
        if (type == "qualified_identifier") {
            const TSNode nameField = ts_node_child_by_field_name(current, "name", 4);
            return ts_node_is_null(nameField) ? current : nameField;
        }
        // pointer_declarator / reference_declarator / function_declarator -
        // each carries its own nested declarator pointing further in
        // (field-labelled or positional, see declaratorChild() above).
        const TSNode inner = declaratorChild(current);
        if (ts_node_is_null(inner)) {
            return current;  // can't unwrap further - best-effort fallback
        }
        current = inner;
    }
    return functionDefinition;  // no declarator at all - degenerate case
}

// Returns the null node (ts_node_is_null() true) if `node` has no name this
// function can resolve, matching the field-lookup convention used above.
// `language`-gated: resolveFunctionDefinitionName()'s declarator-unwrap logic
// is C++-specific (it looks for a "declarator" field/child that Python's
// grammar never produces). Both grammars name their function node type
// "function_definition" (confirmed via ts_probe_outline), so dispatching on
// nodeType alone would route Python functions through the C++ unwrap path,
// which finds no declarator and falls back to returning the whole node -
// this was the Phase 7f test suite's actual observed failure mode before
// this language check was added.
[[nodiscard]] TSNode resolveSymbolName(TSNode node, std::string_view nodeType, Language language) {
    if (language == Language::Cpp && nodeType == "function_definition") {
        return resolveFunctionDefinitionName(node);
    }
    return ts_node_child_by_field_name(node, "name", 4);
}

// One in-progress "scan this node's children" frame in walkForOutline()'s
// explicit stack. Splice frames (a non-symbol node's children) contribute
// directly to whatever level is already accumulating - control-flow blocks/
// expressions don't add nesting. SymbolBody frames (a matched symbol's
// "body" field) accumulate into a fresh level that becomes that symbol's
// OutlineNode::children once the frame finishes.
enum class ScanKind : std::uint8_t { Splice, SymbolBody };

struct ScanState {
    TSNode   node;
    uint32_t childIndex = 0;
    uint32_t childCount = 0;
    ScanKind kind        = ScanKind::Splice;
};

// Builds the OutlineNode for a matched symbol `child` (children left empty -
// the caller fills them in once the returned body, if any, has been scanned)
// into `outSymbol` and returns the symbol's "body" field via `outBody`
// (ts_node_is_null(outBody) if the symbol has none, e.g. a forward
// declaration). Returns false - leaving both out-params untouched - if
// `child` has no name this function can resolve.
[[nodiscard]] bool buildPendingSymbol(TSNode child, std::string_view childType, SymbolKind kind,
                                      std::u16string_view text, Language language, OutlineNode& outSymbol,
                                      TSNode& outBody) {
    const TSNode nameNode = resolveSymbolName(child, childType, language);
    if (ts_node_is_null(nameNode)) {
        return false;
    }
    outSymbol = OutlineNode{
        .name             = textOf(nameNode, text),
        .pos              = ts_node_start_byte(nameNode) / 2,
        .containingRange   = {.start = ts_node_start_byte(child) / 2, .end = ts_node_end_byte(child) / 2},
        .symbolKind        = kind,
        .children          = {},
    };
    outBody = ts_node_child_by_field_name(child, "body", 4);
    return true;
}

// Iterative, explicit-stack walk - see syntax.cpp's walkTree() for the same
// motivation (AST depth comes from the source file being edited, not from
// anything this project controls, so it isn't safely bounded) applied to a
// flat token walk. This walk additionally constructs a *nested* OutlineNode
// tree, so `scanStack` (mirroring the call stack a recursive version would
// use) is paired with `resultLevels`/`pendingSymbols` (mirroring the local
// `result`/`outlineNode` variables each recursive call would otherwise hold
// on that stack).
[[nodiscard]] std::vector<OutlineNode> walkForOutline(TSNode root, const SymbolTable& table,
                                                       std::u16string_view text, Language language) {
    std::vector<ScanState> scanStack{ScanState{.node = root, .childCount = ts_node_child_count(root)}};
    std::vector<std::vector<OutlineNode>> resultLevels(1);
    std::vector<OutlineNode>              pendingSymbols;

    while (!scanStack.empty()) {
        ScanState& scan = scanStack.back();
        if (scan.childIndex >= scan.childCount) {
            const ScanKind finishedKind = scan.kind;
            scanStack.pop_back();
            if (finishedKind == ScanKind::SymbolBody) {
                OutlineNode finished = std::move(pendingSymbols.back());
                pendingSymbols.pop_back();
                finished.children = std::move(resultLevels.back());
                resultLevels.pop_back();
                resultLevels.back().push_back(std::move(finished));
            }
            continue;
        }
        const TSNode child = ts_node_child(scan.node, scan.childIndex);
        ++scan.childIndex;
        const std::string_view childType = ts_node_type(child);
        const auto it = table.find(childType);
        if (it == table.end()) {
            // Not a symbol itself - descend into its own children, splicing
            // into the SAME level (no new nesting).
            scanStack.push_back(ScanState{.node = child, .childCount = ts_node_child_count(child)});
            continue;
        }
        OutlineNode outlineNode{};
        TSNode      bodyNode{};
        if (!buildPendingSymbol(child, childType, it->second, text, language, outlineNode, bodyNode)) {
            continue;  // e.g. a forward declaration with no body/name to anchor an entry on
        }
        if (ts_node_is_null(bodyNode)) {
            resultLevels.back().push_back(std::move(outlineNode));
        } else {
            pendingSymbols.push_back(std::move(outlineNode));
            resultLevels.emplace_back();
            scanStack.push_back(ScanState{
                .node = bodyNode, .childCount = ts_node_child_count(bodyNode), .kind = ScanKind::SymbolBody});
        }
    }
    return std::move(resultLevels.front());
}

}  // namespace

std::vector<const OutlineNode*> findBreadcrumbPath(document::TextPos pos, const std::vector<OutlineNode>& nodes) {
    std::vector<const OutlineNode*> path;
    const std::vector<OutlineNode>* level = &nodes;
    while (level != nullptr) {
        const OutlineNode* match = nullptr;
        for (const auto& node : *level) {
            if (pos >= node.containingRange.start && pos < node.containingRange.end) {
                match = &node;
                break;
            }
        }
        if (match == nullptr) {
            break;
        }
        path.push_back(match);
        level = &match->children;
    }
    return path;
}

std::vector<OutlineNode> extractOutline(std::u16string_view text, Language language) {
    // Phase 7n1: was a 2-way ternary (Cpp ? tree_sitter_cpp() :
    // tree_sitter_python()) that silently routed every OTHER language
    // through Python's grammar once Language grew past 2 enumerators - a
    // real correctness bug (opening a .rs file would extractOutline() its
    // Rust source AS Python), not just a missing-feature gap. Fixed by
    // sharing the same tsLanguageFor() syntax.cpp/incremental_parser.cpp use.
    const TSLanguage*  tsLanguage = tsLanguageFor(language);
    const SymbolTable& table      = symbolTableFor(language);

    // json_syntax_highlight_large_file_open_hang.md: symbolTableFor()
    // returns emptySymbolTable() for 19 of the languages listed there
    // (Json/Html/Css/Shell/Yaml/Toml/Xml/TypeScript/Tsx/Php/Markdown/
    // PowerShell/Ini/Batch/Sql among them) - walkForOutline() below can
    // NEVER match a node against an empty table, so parsing at all is
    // strictly wasted work for these languages, independent of how large
    // the document is. This was previously unconditional: a full,
    // non-incremental ts_parser_parse_string_encoding() call - on a
    // 1.45M-line JSON array, ~22.8s measured, entirely synchronous on the
    // caller's thread (refreshDocumentCacheIfStale()) - to walk a tree that
    // could only ever produce an empty result.
    if (table.empty()) {
        return {};
    }

    const TSParserPtr parser = makeParser(tsLanguage);
    const auto*        bytes  = reinterpret_cast<const char*>(text.data());
    const auto          length = static_cast<uint32_t>(text.size() * sizeof(char16_t));

    const TSTreePtr tree(
        ts_parser_parse_string_encoding(parser.get(), nullptr, bytes, length, TSInputEncodingUTF16LE),
        &ts_tree_delete);
    if (!tree) {
        return {};  // ts_parser_parse_string_encoding only returns NULL if no language is set
    }

    return walkForOutline(ts_tree_root_node(tree.get()), table, text, language);
}

}  // namespace neomifes::syntax
