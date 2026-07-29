#include "neomifes/syntax/incremental_parser.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
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

// Phase 7m: for each edit in `edits`, its own [startByte, newEndByte) span -
// forward-propagated through every LATER edit in the batch via
// ts_range_edit() (tree-sitter's own "shift a range through an edit"
// primitive, the same one ts_tree_edit() effectively applies to node byte
// offsets internally) - so the result is expressed in the FINAL text's byte
// coordinates, matching newTree's node ranges.
//
// This exists because ts_tree_get_changed_ranges() alone is NOT sufficient:
// it only reports genuine HIERARCHICAL/structural differences between the
// two trees, and a boundary-touching edit that merges into an adjacent leaf
// without changing the surrounding node STRUCTURE (e.g. inserting a digit
// right after another digit, extending one number_literal leaf rather than
// adding a new node) can come back with an EMPTY changed-ranges array even
// though that leaf's byte range plainly did change - confirmed empirically
// (not guessed) via a failing test before this function existed. Every
// edit's own literal span is therefore always treated as changed,
// independent of what ts_tree_get_changed_ranges() reports; that function
// still matters for cascades BEYOND the literal edit (see the unterminated-
// comment-insertion test).
[[nodiscard]] std::vector<TSRange> computeDirtyRangesInFinalCoordinates(std::span<const ReparseEdit> edits) {
    std::vector<TSRange> dirty;
    dirty.reserve(edits.size());
    for (std::size_t i = 0; i < edits.size(); ++i) {
        TSRange range{
            .start_point = TSPoint{.row = edits[i].startRow, .column = edits[i].startColumn},
            .end_point   = TSPoint{.row = edits[i].newEndRow, .column = edits[i].newEndColumn},
            .start_byte  = edits[i].startByte,
            .end_byte    = edits[i].newEndByte,
        };
        for (std::size_t j = i + 1; j < edits.size(); ++j) {
            const TSInputEdit laterEdit = toTsInputEdit(edits[j]);
            ts_range_edit(&range, &laterEdit);
        }
        // Phase 7q: a pure deletion (this edit inserted nothing) collapses
        // to a ZERO-WIDTH range in final coordinates - e.g. "12" -> "1"
        // deletes the '2' at code-unit 9, giving [18,18) in bytes. Querying
        // ts_node_descendant_for_byte_range() with a zero-width range sitting
        // exactly on a node boundary is ambiguous: it can resolve to either
        // the node ending there or the one starting there, and empirically
        // (confirmed via a failing test before this adjustment existed) it
        // picked the WRONG one here - the unchanged ';' immediately after,
        // not the number_literal that actually shrank from "12" to "1" -
        // silently dropping that node's token from the re-walk entirely.
        // Backing the start up by one code unit (2 bytes, ADR-014's
        // byte=codeUnit*2 convention) makes the range unambiguously
        // non-empty and guarantees it reaches into the node immediately
        // before the deletion point, where a shrinking/merging node is
        // actually anchored.
        if (range.start_byte == range.end_byte && range.start_byte >= 2) {
            range.start_byte -= 2;
        }
        dirty.push_back(range);
    }
    return dirty;
}

// Phase 7q: the smallest [start, end) byte span containing every range in
// `ranges` - reparseDelta() looks up the AST node covering this span via
// ts_node_descendant_for_byte_range() rather than re-walking multiple
// independent subtrees separately (a deliberate simplification over Phase
// 7m's per-range splicing - see incremental_parser.h's header comment).
// `ranges` is never empty when called (always seeded with at least one
// entry from computeDirtyRangesInFinalCoordinates()).
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> mergedByteSpan(std::span<const TSRange> ranges) noexcept {
    std::uint32_t start = ranges.front().start_byte;
    std::uint32_t end   = ranges.front().end_byte;
    for (const TSRange& range : ranges) {
        start = std::min(start, range.start_byte);
        end   = std::max(end, range.end_byte);
    }
    return {start, end};
}

}  // namespace

struct IncrementalParser::Impl {
    explicit Impl(Language lang)
        : language(lang), parser(detail::makeParser(tsLanguageFor(lang))), tree(nullptr, &ts_tree_delete) {}

    Language            language;
    detail::TSParserPtr parser;
    detail::TSTreePtr   tree;  // nullptr until the first reparseDelta() call completes
};

IncrementalParser::IncrementalParser(Language language) : m_impl(std::make_unique<Impl>(language)) {}
IncrementalParser::~IncrementalParser() = default;
IncrementalParser::IncrementalParser(IncrementalParser&&) noexcept            = default;
IncrementalParser& IncrementalParser::operator=(IncrementalParser&&) noexcept = default;

TokenPatch IncrementalParser::reparseDelta(std::u16string_view text, std::span<const ReparseEdit> edits) {
    const bool hadTree = static_cast<bool>(m_impl->tree);
    if (hadTree) {
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
        return TokenPatch{.invalidatedRange = {.start = 0, .end = static_cast<document::TextPos>(text.size())},
                          .shiftAmount       = 0,
                          .replacementTokens = {}};
    }

    if (hadTree && !edits.empty()) {
        // Phase 7q: only the subtree covering every affected byte range (by
        // ts_tree_get_changed_ranges() OR by directly overlapping one of the
        // edits' own literal spans, see computeDirtyRangesInFinalCoordinates()'s
        // comment on why both are needed) gets freshly re-walked; everything
        // else is left to the caller's own persisted token list.
        // ts_tree_get_changed_ranges() must run here, before m_impl->tree is
        // overwritten below - this is the one window where both the (already
        // ts_tree_edit()ed) old tree and the freshly parsed new tree are
        // simultaneously valid, which that call requires.
        std::uint32_t changedCount = 0;
        // This is unique_ptr's array specialization RAII-wrapping a
        // malloc'd C array (ts_tree_get_changed_ranges()'s documented
        // ownership contract), not a raw C-style array declaration; the
        // checker can't tell the two apart.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
        const std::unique_ptr<TSRange[], decltype(&std::free)> changedRanges(
            ts_tree_get_changed_ranges(m_impl->tree.get(), newTree.get(), &changedCount), &std::free);

        std::vector<TSRange> affectedRanges(changedRanges.get(), changedRanges.get() + changedCount);
        const std::vector<TSRange> dirtyRanges = computeDirtyRangesInFinalCoordinates(edits);
        affectedRanges.insert(affectedRanges.end(), dirtyRanges.begin(), dirtyRanges.end());

        const auto [spanStart, spanEnd] = mergedByteSpan(affectedRanges);
        const TSNode newRoot            = ts_tree_root_node(newTree.get());
        const TSNode coveringNode       = ts_node_descendant_for_byte_range(newRoot, spanStart, spanEnd);
        auto replacement = detail::walkTree(coveringNode, namedKindsFor(m_impl->language));

        std::int64_t shiftBytes = 0;
        for (const ReparseEdit& edit : edits) {
            shiftBytes += static_cast<std::int64_t>(edit.newEndByte) - static_cast<std::int64_t>(edit.oldEndByte);
        }

        const auto invalidatedStart = static_cast<document::TextPos>(ts_node_start_byte(coveringNode) / 2);
        const auto invalidatedEnd   = static_cast<document::TextPos>(ts_node_end_byte(coveringNode) / 2);

        m_impl->tree = std::move(newTree);
        return TokenPatch{.invalidatedRange = {.start = invalidatedStart, .end = invalidatedEnd},
                          .shiftAmount       = shiftBytes / 2,
                          .replacementTokens = std::move(replacement)};
    }

    auto tokens  = detail::walkTree(ts_tree_root_node(newTree.get()), namedKindsFor(m_impl->language));
    m_impl->tree = std::move(newTree);
    return TokenPatch{.invalidatedRange = {.start = 0, .end = static_cast<document::TextPos>(text.size())},
                      .shiftAmount       = 0,
                      .replacementTokens = std::move(tokens)};
}

std::vector<Token> applyTokenPatch(std::vector<Token> tokens, const TokenPatch& patch) {
    // invalidatedRange is in the FINAL (post-edit) coordinate space (see
    // TokenPatch's header comment); `tokens` is still in the PRE-edit space.
    // The start needs no translation (it sits before every edit in this
    // batch, by construction of mergedByteSpan()/computeDirtyRangesInFinal
    // Coordinates()); the end does (subtract shiftAmount to recover its
    // PRE-edit position).
    const document::TextPos oldInvalidatedStart = patch.invalidatedRange.start;
    const auto               oldInvalidatedEnd =
        static_cast<document::TextPos>(static_cast<std::int64_t>(patch.invalidatedRange.end) - patch.shiftAmount);

    std::vector<Token> merged;
    merged.reserve(tokens.size() + patch.replacementTokens.size());

    std::size_t i = 0;
    while (i < tokens.size() && tokens[i].range.end <= oldInvalidatedStart) {
        merged.push_back(tokens[i]);
        ++i;
    }
    while (i < tokens.size() && tokens[i].range.start < oldInvalidatedEnd) {
        ++i;  // overlaps the invalidated span - discard, replaced below
    }
    merged.insert(merged.end(), patch.replacementTokens.begin(), patch.replacementTokens.end());
    for (; i < tokens.size(); ++i) {
        merged.push_back(Token{
            .range = {.start = static_cast<document::TextPos>(static_cast<std::int64_t>(tokens[i].range.start) +
                                                                patch.shiftAmount),
                      .end   = static_cast<document::TextPos>(static_cast<std::int64_t>(tokens[i].range.end) +
                                                              patch.shiftAmount)},
            .kind  = tokens[i].kind});
    }
    return merged;
}

}  // namespace neomifes::syntax
