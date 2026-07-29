#pragma once

// neomifes::syntax::IncrementalParser - stateful, single-language wrapper
// around tree-sitter's incremental reparse API (Phase 7k). Unlike
// parseCpp()/parsePython()/parse() (syntax.h, single-shot: new TSParser +
// TSTree per call, tree discarded immediately), this class retains the
// previous TSTree across reparseDelta() calls and feeds each accumulated edit
// through ts_tree_edit() before reparsing, letting tree-sitter reuse
// unaffected subtrees instead of re-walking the whole document.
//
// Wired into RenderPipeline via render::SyntaxWorker (Phase 7l) - one
// instance lives on SyntaxWorker's background thread, reconstructed
// wholesale whenever the active language changes or the caller signals a
// document switch (see SyntaxWorker::requestParse()'s resetIncrementalState).
// NOT thread-safe for concurrent calls - single-writer use only, same
// assumption as document::Document.
//
// Phase 7k/7m both discovered the same bottleneck from different angles:
// reparse() had to return a token vector sized to the WHOLE document on
// EVERY call, which dominates the cost even once tree-sitter's own
// incremental reparse and the token-splice logic around it are both
// properly incremental internally (Phase 7m: ~2.2x win vs. Phase 7k, but
// still O(document size), confirmed via a 10x-larger-document benchmark
// that cost ~10x more, not the hoped-for flat cost). See
// master_roadmap.md sec.7's Phase 7k/7m completion notes for that history.
//
// Phase 7q narrows this further: reparseDelta() below returns only a DELTA
// of changed tokens (TokenPatch) instead of the full list. The caller
// (SyntaxWorker) persists its own token list across calls and merges each
// patch into it via applyTokenPatch(). The tree-sitter side of this really
// is O(edit size) now (ts_node_descendant_for_byte_range() finds the
// smallest subtree spanning the changed region, detail::walkTree()
// re-walks only that subtree, instead of Phase 7m's whole-tree pre-order
// walk with per-node splicing).
//
// IMPORTANT, benchmark-grounded (CLAUDE.md rule 10): this is again a
// substantial CONSTANT-FACTOR win (~30% faster than Phase 7m on both the
// 50,000-line and 500,000-line benchmarks below), NOT the asymptotic one
// hoped for - reparseDelta()+applyTokenPatch() together still cost roughly
// O(document size), because applyTokenPatch() itself is a single linear
// pass over the ENTIRE persisted token list on every call (shifting/
// copying every token after the invalidated range, even when the edit
// touched one character). Confirmed empirically: the 500,000-line variant
// of the single-character-edit benchmark costs ~9.6x the 50,000-line one -
// genuinely proportional to document size, not flat
// (BM_IncrementalReparse_SingleCharEdit[_LargeDocument], syntax_parse_bench.
// cpp; 103ms/989ms measured on Release, still well over roadmap sec.7.11's
// <=50ms DoD). Reaching true O(edit size) end-to-end would require
// restructuring how the persisted token list itself is stored (e.g. so a
// shift only has to touch tokens actually near the edit, not every token
// after it) - deliberately out of scope here to keep Phase 7q's blast
// radius to the tree-sitter-facing half of the problem; see
// master_roadmap.md sec.7's Phase 7q completion note.
//
// tree-sitter types (TSNode/TSTree/TSParser) never appear in this header -
// they are an implementation detail confined to incremental_parser.cpp/
// syntax_internal.h, matching syntax.h's own "not our public API surface"
// rule.

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "neomifes/syntax/syntax.h"

namespace neomifes::syntax {

// tree-sitter's TSInputEdit, expressed without exposing tree-sitter types
// here. Byte offsets are UTF-16 code-unit offsets * 2, rows are 0-based
// logical line numbers, columns are UTF-16 code-unit offsets into that
// line * 2 - this module's existing encoding convention (see syntax.cpp's
// appendLeafToken()/ADR-014). document::EditDelta (document.h) already
// carries the code-unit-space values this struct's fields are built from;
// converting EditDelta -> ReparseEdit (bridging document:: and syntax::) is
// the caller's responsibility, kept out of this header so neomifes::syntax
// stays independent of neomifes::document beyond the existing TextPos/
// TextRange types syntax.h already uses.
struct ReparseEdit {
    std::uint32_t startByte    = 0;
    std::uint32_t oldEndByte   = 0;
    std::uint32_t newEndByte   = 0;
    std::uint32_t startRow     = 0;
    std::uint32_t startColumn  = 0;
    std::uint32_t oldEndRow    = 0;
    std::uint32_t oldEndColumn = 0;
    std::uint32_t newEndRow    = 0;
    std::uint32_t newEndColumn = 0;
};

// Describes how to update an existing, PERSISTED token list after an
// incremental reparse (Phase 7q), instead of reparseDelta() itself
// returning the full list every time. `invalidatedRange`/`replacementTokens`
// are both expressed in the FINAL (post-edit) text's coordinate space -
// straight from the freshly parsed tree, no translation needed on that
// side. `shiftAmount` (code units, the sum of every edit's newEndPos -
// oldEndPos in this batch) is what applyTokenPatch() uses to translate
// `invalidatedRange` back into the PRE-edit coordinate space the caller's
// existing token list is still in: `invalidatedRange.start` needs no
// translation (it sits before every edit in this batch, by construction -
// see reparseDelta()'s comment), `invalidatedRange.end` does (subtract
// shiftAmount to recover its PRE-edit position).
struct TokenPatch {
    document::TextRange invalidatedRange;
    std::int64_t         shiftAmount = 0;
    std::vector<Token>   replacementTokens;
};

// Merges `patch` into `tokens` - discards every token overlapping
// `patch.invalidatedRange` (translated to `tokens`' own PRE-edit coordinate
// space, see TokenPatch's comment), shifts every token entirely after it by
// `patch.shiftAmount`, and splices in `patch.replacementTokens` at that
// position. `tokens` must be sorted ascending by range.start (reparseDelta()
// always produces patches consistent with that invariant, since
// detail::walkTree() itself visits nodes in left-to-right document order).
// O(tokens.size() + patch.replacementTokens.size()) - a single linear pass,
// no tree-sitter API calls or node classification, but NOT O(edit size):
// every token after the invalidated range gets touched (shifted) on every
// call, regardless of edit size (see this file's header comment on the
// benchmark that confirms this is the dominant remaining cost).
[[nodiscard]] std::vector<Token> applyTokenPatch(std::vector<Token> tokens, const TokenPatch& patch);

class IncrementalParser {
public:
    explicit IncrementalParser(Language language);
    ~IncrementalParser();

    IncrementalParser(const IncrementalParser&)            = delete;
    IncrementalParser& operator=(const IncrementalParser&) = delete;
    IncrementalParser(IncrementalParser&&) noexcept;
    IncrementalParser& operator=(IncrementalParser&&) noexcept;

    // `edits` empty (including the very first call, when no tree is
    // retained yet) triggers a full parse - the resulting TokenPatch's
    // `invalidatedRange` then spans the WHOLE text and `replacementTokens`
    // is therefore the full token list, same "always succeeds, even on
    // malformed input" contract parseCpp() documents. Feeding that patch to
    // applyTokenPatch() against an EMPTY persisted list naturally reduces to
    // "the full list" - callers never need to special-case the first call
    // vs. every incremental one after it (see SyntaxWorker::workerLoop()).
    // Non-empty `edits` applies each via ts_tree_edit() (in the given
    // order) to the retained tree before reparsing.
    //
    // Caller is responsible for `edits` reflecting EVERY mutation since the
    // previous reparseDelta() call, in chronological order - skipping one
    // silently corrupts the retained tree's byte-offset bookkeeping (a
    // documented hazard, not guarded against here; SyntaxWorker's
    // accumulate-never-drop queue, Phase 7l, is what actually prevents this
    // once wired to real edits).
    [[nodiscard]] TokenPatch reparseDelta(std::u16string_view text, std::span<const ReparseEdit> edits);

private:
    struct Impl;  // no longer holds a persisted token list (Phase 7m's
                  // lastTokens) - the caller owns that now (Phase 7q)
    std::unique_ptr<Impl> m_impl;
};

}  // namespace neomifes::syntax
