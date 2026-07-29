#pragma once

// neomifes::syntax::IncrementalParser - stateful, single-language wrapper
// around tree-sitter's incremental reparse API (Phase 7k). Unlike
// parseCpp()/parsePython()/parse() (syntax.h, single-shot: new TSParser +
// TSTree per call, tree discarded immediately), this class retains the
// previous TSTree across reparseRange() calls and feeds each accumulated edit
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
// Phase 7q narrowed this further: reparseDelta() (since removed) returned
// only a DELTA of changed tokens (TokenPatch) instead of the full list. The
// caller (SyntaxWorker) persisted its own token list across calls and
// merged each patch into it via applyTokenPatch(). The tree-sitter side of
// that really was O(edit size) (ts_node_descendant_for_byte_range() finds
// the smallest subtree spanning the changed region, detail::walkTree()
// re-walks only that subtree) - but benchmark-grounded (CLAUDE.md rule 10):
// applyTokenPatch() itself turned out to be a single linear pass over the
// ENTIRE persisted token list on every call (shifting/copying every token
// after the invalidated range, even when the edit touched one character).
// Confirmed empirically: the 500,000-line variant of the single-character-
// edit benchmark cost ~9.6x the 50,000-line one - genuinely proportional to
// document size, not flat (103ms/989ms measured on Release, still well over
// roadmap sec.7.11's <=50ms DoD). See master_roadmap.md sec.7's Phase 7q
// completion note.
//
// Phase 7t removes the "persisted token list" concept entirely instead of
// trying to make its shift cheaper. reparseRange() below takes the byte
// range the CALLER actually wants tokens for (RenderPipeline's visible
// viewport plus a prefetch margin - see computeDesiredTokenRange()) and
// returns a complete, self-contained, ready-to-use token list for AT LEAST
// that range - no shift arithmetic, no merge, no coordinate translation, no
// state carried by the caller between calls. This works because
// RenderPipeline::m_tokens no longer needs to cover the whole document -
// drawTokensOnLine()'s token-cursor sweep was already tolerant of gaps (it
// simply leaves ungapped spans at the default/uncolored brush), so a token
// list scoped to "whatever was last requested" draws correctly with zero
// changes to the painting code. The tree-sitter side is unchanged from
// Phase 7q (ts_tree_edit() still lets tree-sitter reuse unaffected
// subtrees internally); what changed is that the caller no longer has to
// reconcile a growing persisted list against every edit, because there
// isn't one anymore.
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

class IncrementalParser {
public:
    explicit IncrementalParser(Language language);
    ~IncrementalParser();

    IncrementalParser(const IncrementalParser&)            = delete;
    IncrementalParser& operator=(const IncrementalParser&) = delete;
    IncrementalParser(IncrementalParser&&) noexcept;
    IncrementalParser& operator=(IncrementalParser&&) noexcept;

    // Phase 7t: `edits` (same accumulate-since-last-call contract as
    // before) are applied via ts_tree_edit() (in the given order) to the
    // retained tree before reparsing - this is what lets tree-sitter reuse
    // unaffected subtrees internally, independent of what gets walked into
    // tokens afterward. `edits` empty (including the very first call, when
    // no tree is retained yet) simply means no edits happened since the
    // previous call - same "always succeeds, even on malformed input"
    // contract parseCpp() documents either way.
    //
    // `rangeStartByte`/`rangeEndByte` describe the byte range the CALLER
    // wants tokens for (typically RenderPipeline's visible viewport plus a
    // prefetch margin, converted to bytes the same *2 way ReparseEdit's
    // fields are - see SyntaxWorker::workerLoop()). The returned token list
    // is sorted ascending and covers AT LEAST that range:
    // ts_node_descendant_for_byte_range() resolves to the smallest
    // ENCLOSING node, which can be wider than requested when the range
    // lands inside a larger syntactic construct - the same granularity
    // limitation any tree-sitter-based highlighter has. No merging with any
    // previous call's result happens here or on the caller's side - each
    // call is a fresh, self-contained answer for the range it was asked
    // about (see this header's top comment for why that's safe).
    //
    // Caller is responsible for `edits` reflecting EVERY mutation since the
    // previous reparseRange() call, in chronological order - skipping one
    // silently corrupts the retained tree's byte-offset bookkeeping (a
    // documented hazard, not guarded against here; SyntaxWorker's
    // accumulate-never-drop queue, Phase 7l, is what actually prevents this
    // once wired to real edits).
    [[nodiscard]] std::vector<Token> reparseRange(std::u16string_view text, std::span<const ReparseEdit> edits,
                                                   std::uint32_t rangeStartByte, std::uint32_t rangeEndByte);

private:
    struct Impl;  // holds no persisted token list - each reparseRange() call is self-contained (Phase 7t)
    std::unique_ptr<Impl> m_impl;
};

}  // namespace neomifes::syntax
