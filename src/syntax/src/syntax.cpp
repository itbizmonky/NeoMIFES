#include "neomifes/syntax/syntax.h"

#include <tree_sitter/api.h>

#include "syntax_internal.h"

namespace neomifes::syntax {

namespace {

using detail::LeafKindTable;
using detail::makeParser;
using detail::namedLeafKindsForBash;
using detail::namedLeafKindsForC;
using detail::namedLeafKindsForCpp;
using detail::namedLeafKindsForCss;
using detail::namedLeafKindsForGo;
using detail::namedLeafKindsForHtml;
using detail::namedLeafKindsForJava;
using detail::namedLeafKindsForJavaScript;
using detail::namedLeafKindsForJson;
using detail::namedLeafKindsForPython;
using detail::namedLeafKindsForRust;
using detail::namedLeafKindsForToml;
using detail::namedLeafKindsForXml;
using detail::namedLeafKindsForYaml;
using detail::TSParserPtr;
using detail::TSTreePtr;
using detail::walkTree;

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
    return parseWithLanguage(text, detail::tree_sitter_cpp(), namedLeafKindsForCpp());
}

std::vector<Token> parsePython(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_python(), namedLeafKindsForPython());
}

std::vector<Token> parseC(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_c(), namedLeafKindsForC());
}

std::vector<Token> parseJavaScript(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_javascript(), namedLeafKindsForJavaScript());
}

std::vector<Token> parseJava(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_java(), namedLeafKindsForJava());
}

std::vector<Token> parseGo(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_go(), namedLeafKindsForGo());
}

std::vector<Token> parseRust(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_rust(), namedLeafKindsForRust());
}

std::vector<Token> parseJson(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_json(), namedLeafKindsForJson());
}

std::vector<Token> parseHtml(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_html(), namedLeafKindsForHtml());
}

std::vector<Token> parseCss(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_css(), namedLeafKindsForCss());
}

std::vector<Token> parseShell(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_bash(), namedLeafKindsForBash());
}

std::vector<Token> parseYaml(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_yaml(), namedLeafKindsForYaml());
}

std::vector<Token> parseToml(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_toml(), namedLeafKindsForToml());
}

std::vector<Token> parseXml(std::u16string_view text) {
    return parseWithLanguage(text, detail::tree_sitter_xml(), namedLeafKindsForXml());
}

std::vector<Token> parse(std::u16string_view text, Language language) {
    switch (language) {
        case Language::Cpp:
            return parseCpp(text);
        case Language::Python:
            return parsePython(text);
        case Language::C:
            return parseC(text);
        case Language::JavaScript:
            return parseJavaScript(text);
        case Language::Java:
            return parseJava(text);
        case Language::Go:
            return parseGo(text);
        case Language::Rust:
            return parseRust(text);
        case Language::Json:
            return parseJson(text);
        case Language::Html:
            return parseHtml(text);
        case Language::Css:
            return parseCss(text);
        case Language::Shell:
            return parseShell(text);
        case Language::Yaml:
            return parseYaml(text);
        case Language::Toml:
            return parseToml(text);
        case Language::Xml:
            return parseXml(text);
    }
    return {};  // unreachable (all Language enumerators handled above)
}

}  // namespace neomifes::syntax
