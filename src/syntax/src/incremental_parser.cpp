#include "neomifes/syntax/incremental_parser.h"

#include <tree_sitter/api.h>

#include "syntax_internal.h"

namespace neomifes::syntax {

namespace {

[[nodiscard]] const TSLanguage* tsLanguageFor(Language language) {
    switch (language) {
        case Language::Cpp:
            return detail::tree_sitter_cpp();
        case Language::Python:
            return detail::tree_sitter_python();
    }
    return detail::tree_sitter_cpp();  // unreachable (all enumerators handled above)
}

[[nodiscard]] const detail::LeafKindTable& namedKindsFor(Language language) {
    switch (language) {
        case Language::Cpp:
            return detail::namedLeafKindsForCpp();
        case Language::Python:
            return detail::namedLeafKindsForPython();
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
    detail::TSTreePtr   tree;  // nullptr until the first reparse() call completes
};

IncrementalParser::IncrementalParser(Language language) : m_impl(std::make_unique<Impl>(language)) {}
IncrementalParser::~IncrementalParser() = default;
IncrementalParser::IncrementalParser(IncrementalParser&&) noexcept            = default;
IncrementalParser& IncrementalParser::operator=(IncrementalParser&&) noexcept = default;

std::vector<Token> IncrementalParser::reparse(std::u16string_view text, std::span<const ReparseEdit> edits) {
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
        return {};  // ts_parser_parse_string_encoding only returns NULL if no language is set
    }

    const std::vector<Token> tokens = detail::walkTree(ts_tree_root_node(newTree.get()), namedKindsFor(m_impl->language));
    m_impl->tree                    = std::move(newTree);
    return tokens;
}

}  // namespace neomifes::syntax
