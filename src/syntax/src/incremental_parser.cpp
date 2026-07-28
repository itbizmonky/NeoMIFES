#include "neomifes/syntax/incremental_parser.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

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
        dirty.push_back(range);
    }
    return dirty;
}

// Phase 7m: true if tree-sitter byte ranges [aStart,aEnd) and [bStart,bEnd)
// overlap OR merely touch (aEnd == bStart or bEnd == aStart also count).
// Deliberately inclusive, not the usual half-open "touching is not overlap"
// rule document::TextRange uses elsewhere - a node/token sitting exactly at
// an edit's boundary can still merge with the edit (e.g. a digit inserted
// immediately after another digit extends one number_literal leaf; deleting
// the character right after a leaf can equally pull a following leaf into
// it), so a dirty/changed range must flag anything touching it as affected,
// never just anything strictly inside it. A zero-width dirty range (a pure
// deletion's own span, see computeDirtyRangesInFinalCoordinates()) can only
// ever flag a node under this inclusive rule - under a strict one it could
// never overlap anything, silently missing the exact case it exists for
// (confirmed empirically via a failing test before this was inclusive).
// Being more inclusive than strictly necessary only ever costs a few extra
// fresh leaf reclassifications, never correctness.
[[nodiscard]] bool rangesOverlap(std::uint32_t aStart, std::uint32_t aEnd, std::uint32_t bStart,
                                  std::uint32_t bEnd) noexcept {
    return aStart <= bEnd && bStart <= aEnd;
}

[[nodiscard]] bool nodeOverlapsAnyChangedRange(std::uint32_t nodeStartByte, std::uint32_t nodeEndByte,
                                                std::span<const TSRange> changedRanges) noexcept {
    return std::ranges::any_of(changedRanges, [&](const TSRange& range) {
        return rangesOverlap(nodeStartByte, nodeEndByte, range.start_byte, range.end_byte);
    });
}

// Shifts one token's range according to a single edit (code-unit space,
// derived from ReparseEdit's byte fields halved - ADR-014's "byte offset =
// code-unit offset * 2" convention). nullopt means `range` overlaps the
// edit - the caller discards it, since walkTreeIncremental() re-derives its
// replacement from ts_tree_get_changed_ranges() instead of guessing at the
// exact boundary here.
[[nodiscard]] std::optional<document::TextRange> shiftRange(document::TextRange range, document::TextPos startPos,
                                                              document::TextPos oldEndPos,
                                                              document::TextPos newEndPos) noexcept {
    if (range.end <= startPos) {
        return range;  // entirely before the edit, unaffected
    }
    if (range.start >= oldEndPos) {
        const auto delta = static_cast<std::int64_t>(newEndPos) - static_cast<std::int64_t>(oldEndPos);
        return document::TextRange{
            .start = static_cast<document::TextPos>(static_cast<std::int64_t>(range.start) + delta),
            .end   = static_cast<document::TextPos>(static_cast<std::int64_t>(range.end) + delta)};
    }
    return std::nullopt;  // overlaps the edit
}

// Applies every edit in `edits` (call order - the same "reflects every
// mutation chronologically" contract reparse() already documents for
// ts_tree_edit()) to `tokens`, dropping any token that overlapped an edit.
// Mirrors how ts_tree_edit() itself gets applied edit-by-edit to the
// retained tree.
[[nodiscard]] std::vector<Token> shiftTokensForEdits(std::vector<Token> tokens, std::span<const ReparseEdit> edits) {
    for (const ReparseEdit& edit : edits) {
        const auto startPos  = static_cast<document::TextPos>(edit.startByte / 2);
        const auto oldEndPos = static_cast<document::TextPos>(edit.oldEndByte / 2);
        const auto newEndPos = static_cast<document::TextPos>(edit.newEndByte / 2);
        std::vector<Token> shifted;
        shifted.reserve(tokens.size());
        for (const Token& token : tokens) {
            if (const auto shiftedRange = shiftRange(token.range, startPos, oldEndPos, newEndPos)) {
                shifted.push_back(Token{.range = *shiftedRange, .kind = token.kind});
            }
        }
        tokens = std::move(shifted);
    }
    return tokens;
}

// Phase 7m: walkTree()'s pruned sibling. Pre-order cursor walk over
// `newRoot`, but for any node whose byte range doesn't overlap
// `changedRanges`, splices in the corresponding tokens from `oldTokens`
// (already shifted into newRoot's coordinate space by shiftTokensForEdits())
// instead of descending and reclassifying - safe per
// ts_tree_get_changed_ranges()'s documented guarantee that characters
// outside the changed ranges have identical ancestor nodes in both trees.
// `oldTokens` must be sorted ascending (shiftTokensForEdits() preserves
// walkTree()'s left-to-right order) so it can be consumed via a single
// forward-advancing cursor without random access.
[[nodiscard]] std::vector<Token> walkTreeIncremental(TSNode newRoot, const detail::LeafKindTable& namedKinds,
                                                      std::span<const Token>   oldTokens,
                                                      std::span<const TSRange> changedRanges) {
    std::vector<Token> tokens;
    std::size_t        oldIdx = 0;

    // Appends every old token fully contained in [start,end) (code-unit
    // space), first skipping past any earlier ones that fell inside a
    // changed region already handled by walking fresh - those must be
    // discarded, not appended anywhere.
    auto appendOldTokensInSpan = [&](document::TextPos start, document::TextPos end) {
        while (oldIdx < oldTokens.size() && oldTokens[oldIdx].range.start < start) {
            ++oldIdx;
        }
        while (oldIdx < oldTokens.size() && oldTokens[oldIdx].range.end <= end) {
            tokens.push_back(oldTokens[oldIdx]);
            ++oldIdx;
        }
    };

    TSTreeCursor cursor     = ts_tree_cursor_new(newRoot);
    bool         descending = true;
    while (true) {
        if (descending) {
            const TSNode node          = ts_tree_cursor_current_node(&cursor);
            const auto   nodeStartByte = ts_node_start_byte(node);
            const auto   nodeEndByte   = ts_node_end_byte(node);
            if (!nodeOverlapsAnyChangedRange(nodeStartByte, nodeEndByte, changedRanges)) {
                appendOldTokensInSpan(static_cast<document::TextPos>(nodeStartByte / 2),
                                      static_cast<document::TextPos>(nodeEndByte / 2));
                descending = false;
            } else if (ts_node_child_count(node) == 0) {
                detail::appendLeafToken(tokens, node, namedKinds);
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

struct IncrementalParser::Impl {
    explicit Impl(Language lang)
        : language(lang), parser(detail::makeParser(tsLanguageFor(lang))), tree(nullptr, &ts_tree_delete) {}

    Language            language;
    detail::TSParserPtr parser;
    detail::TSTreePtr   tree;  // nullptr until the first reparse() call completes
    // Phase 7m: full token list from the previous successful reparse() call -
    // input to the next incremental splice (walkTreeIncremental()). Empty
    // until the first call completes, mirroring `tree`'s own lifecycle.
    std::vector<Token> lastTokens;
};

IncrementalParser::IncrementalParser(Language language) : m_impl(std::make_unique<Impl>(language)) {}
IncrementalParser::~IncrementalParser() = default;
IncrementalParser::IncrementalParser(IncrementalParser&&) noexcept            = default;
IncrementalParser& IncrementalParser::operator=(IncrementalParser&&) noexcept = default;

std::vector<Token> IncrementalParser::reparse(std::u16string_view text, std::span<const ReparseEdit> edits) {
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
        return {};  // ts_parser_parse_string_encoding only returns NULL if no language is set
    }

    std::vector<Token> tokens;
    if (hadTree && !edits.empty()) {
        // Phase 7m: incremental token splice - only the subtrees flagged as
        // changed (by ts_tree_get_changed_ranges() OR by directly
        // overlapping one of the edits' own literal spans, see
        // computeDirtyRangesInFinalCoordinates()'s comment on why both are
        // needed) actually get freshly re-walked; everything else reuses
        // (position-shifted) tokens from the previous call.
        // ts_tree_get_changed_ranges() must run here, before m_impl->tree is
        // overwritten below - this is the one window where both the
        // (already ts_tree_edit()ed) old tree and the freshly parsed new
        // tree are simultaneously valid, which that call requires.
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

        const std::vector<Token> shiftedOld = shiftTokensForEdits(std::move(m_impl->lastTokens), edits);
        tokens = walkTreeIncremental(ts_tree_root_node(newTree.get()), namedKindsFor(m_impl->language), shiftedOld,
                                     affectedRanges);
    } else {
        tokens = detail::walkTree(ts_tree_root_node(newTree.get()), namedKindsFor(m_impl->language));
    }

    m_impl->tree       = std::move(newTree);
    m_impl->lastTokens = tokens;
    return tokens;
}

}  // namespace neomifes::syntax
