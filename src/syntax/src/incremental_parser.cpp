#include "neomifes/syntax/incremental_parser.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "syntax_internal.h"

namespace neomifes::syntax {

namespace {

// Phase 7n1: Language -> TSLanguage* now lives in syntax_internal.h's
// detail::tsLanguageFor(), shared with syntax.cpp/outline.cpp - this file's
// own private copy (Phase 7k/7d era) was removed to avoid a 3rd place that
// needed updating whenever a language is added.
using detail::tsLanguageFor;

[[nodiscard]] const detail::LeafKindTable& namedKindsFor(Language language) {
    switch (language) {
        case Language::Cpp:
            return detail::namedLeafKindsForCpp();
        case Language::Python:
            return detail::namedLeafKindsForPython();
        case Language::C:
            return detail::namedLeafKindsForC();
        case Language::JavaScript:
            return detail::namedLeafKindsForJavaScript();
        case Language::Java:
            return detail::namedLeafKindsForJava();
        case Language::Go:
            return detail::namedLeafKindsForGo();
        case Language::Rust:
            return detail::namedLeafKindsForRust();
        case Language::Json:
            return detail::namedLeafKindsForJson();
        case Language::Html:
            return detail::namedLeafKindsForHtml();
        case Language::Css:
            return detail::namedLeafKindsForCss();
        case Language::Shell:
            return detail::namedLeafKindsForBash();
        case Language::Yaml:
            return detail::namedLeafKindsForYaml();
        case Language::Toml:
            return detail::namedLeafKindsForToml();
        case Language::Xml:
            return detail::namedLeafKindsForXml();
        case Language::TypeScript:
            return detail::namedLeafKindsForTypeScript();
        case Language::Tsx:
            return detail::namedLeafKindsForTsx();
        case Language::Php:
            return detail::namedLeafKindsForPhp();
        case Language::Markdown:
            return detail::namedLeafKindsForMarkdown();
    }
    return detail::namedLeafKindsForCpp();  // unreachable (all enumerators handled above)
}

[[nodiscard]] TSInputEdit toTsInputEdit(const ReparseEdit& edit) noexcept {
    return TSInputEdit{
        .start_byte    = edit.startByte,
        .old_end_byte  = edit.oldEndByte,
        .new_end_byte  = edit.newEndByte,
        .start_point   = TSPoint{.row = edit.startRow, .column = edit.startColumn},
        .old_end_point = TSPoint{.row = edit.oldEndRow, .column = edit.oldEndColumn},
        .new_end_point = TSPoint{.row = edit.newEndRow, .column = edit.newEndColumn},
    };
}

}  // namespace

struct IncrementalParser::Impl {
    explicit Impl(Language lang)
        : language(lang), parser(detail::makeParser(tsLanguageFor(lang))), tree(nullptr, &ts_tree_delete) {}

    Language            language;
    detail::TSParserPtr parser;
    detail::TSTreePtr   tree;  // nullptr until the first reparseRange() call completes
};

IncrementalParser::IncrementalParser(Language language) : m_impl(std::make_unique<Impl>(language)) {}
IncrementalParser::~IncrementalParser() = default;
IncrementalParser::IncrementalParser(IncrementalParser&&) noexcept            = default;
IncrementalParser& IncrementalParser::operator=(IncrementalParser&&) noexcept = default;

std::vector<Token> IncrementalParser::reparseRange(std::u16string_view text, std::span<const ReparseEdit> edits,
                                                    std::uint32_t rangeStartByte, std::uint32_t rangeEndByte) {
    if (m_impl->tree) {
        for (const ReparseEdit& edit : edits) {
            const TSInputEdit tsEdit = toTsInputEdit(edit);
            ts_tree_edit(m_impl->tree.get(), &tsEdit);
        }
    }

    const auto* bytes  = reinterpret_cast<const char*>(text.data());
    const auto  length = static_cast<uint32_t>(text.size() * sizeof(char16_t));
    detail::TSTreePtr newTree(ts_parser_parse_string_encoding(m_impl->parser.get(), m_impl->tree.get(), bytes,
                                                              length, TSInputEncodingUTF16LE),
                              &ts_tree_delete);
    if (!newTree) {
        // ts_parser_parse_string_encoding only returns NULL if no language is
        // set - unreachable via this class's public API (makeParser() always
        // sets one), kept only as a defensive fallback matching parse()'s own
        // "always succeeds" contract.
        return {};
    }
    m_impl->tree = std::move(newTree);

    // Phase 7t: no changed-range diffing needed anymore - the caller tells
    // us exactly what range it wants (RenderPipeline's visible viewport +
    // margin), so we just resolve the smallest node covering that range and
    // walk it. Clamped defensively against the tree's own byte span (the
    // caller's range is always computed against the SAME text this call
    // parses, so this is a belt-and-suspenders bound, not an expected path).
    const TSNode         root         = ts_tree_root_node(m_impl->tree.get());
    const std::uint32_t  clampedEnd   = std::min(rangeEndByte, ts_node_end_byte(root));
    const std::uint32_t  clampedStart = std::min(rangeStartByte, clampedEnd);
    const TSNode         coveringNode = ts_node_descendant_for_byte_range(root, clampedStart, clampedEnd);
    return detail::walkTree(coveringNode, namedKindsFor(m_impl->language));
}

}  // namespace neomifes::syntax
