#pragma once

// neomifes::syntax::IncrementalParser - stateful, single-language wrapper
// around tree-sitter's incremental reparse API (Phase 7k). Unlike
// parseCpp()/parsePython()/parse() (syntax.h, single-shot: new TSParser +
// TSTree per call, tree discarded immediately), this class retains the
// previous TSTree across reparse() calls and feeds each accumulated edit
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
// Phase 7k's own token EXTRACTION (walkTree() over the whole resulting
// tree, every reparse() call) was, once benchmarked, the dominant cost even
// though tree-sitter's internal reparse was already properly incremental -
// see master_roadmap.md sec.7's Phase 7k completion note. Phase 7m narrowed
// that gap: reparse() now also uses ts_tree_get_changed_ranges() to only
// re-walk the subtrees that actually changed, splicing position-shifted
// tokens from the previous call into everywhere else - an internal
// optimization only, this class's public contract (reparse()'s output is
// always byte-identical to a full parse() of the resulting text) is
// unchanged and still what every caller/test relies on.
//
// IMPORTANT, benchmark-grounded (CLAUDE.md rule 10): this is a substantial
// CONSTANT-FACTOR win (~8x over a full parse for a single-character edit,
// measured on a 50,000-line file), not an asymptotic one - reparse() still
// costs roughly O(document size), not O(edit size), because it still
// returns (and therefore still allocates/copies) a token vector sized to
// the WHOLE document on every call, and shifting the retained token list to
// account for an edit (shiftTokensForEdits(), incremental_parser.cpp) walks
// that entire vector once per edit even when nothing in most of it changed.
// Confirmed empirically, not assumed: a 500,000-line variant of the same
// single-character-edit benchmark costs roughly 10x the 50,000-line one,
// i.e. genuinely proportional to document size
// (BM_IncrementalReparse_SingleCharEdit_LargeDocument, syntax_parse_bench.cpp).
// Reaching true O(edit size) would require changing this class's contract
// to return only a DELTA of changed tokens instead of the full list, with
// the caller (SyntaxWorker/RenderPipeline) responsible for merging that
// delta into a persisted token list - deliberately out of scope for Phase
// 7m to keep its blast radius to this class alone; see master_roadmap.md
// sec.7's Phase 7m completion note for the full analysis.
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

    // `edits` empty (including the very first call, when no tree is
    // retained yet) triggers a full parse - identical output to
    // parse(text, language), same "always succeeds, even on malformed
    // input" contract (see syntax.h's parseCpp() comment). Non-empty
    // `edits` applies each via ts_tree_edit() (in the given order) to the
    // retained tree before reparsing.
    //
    // Caller is responsible for `edits` reflecting EVERY mutation since the
    // previous reparse() call, in chronological order - skipping one
    // silently corrupts the retained tree's byte-offset bookkeeping (a
    // documented hazard, not guarded against here; a future queueing
    // design that never drops an edit is what actually prevents this once
    // wired to real edits, see this file's header comment).
    [[nodiscard]] std::vector<Token> reparse(std::u16string_view text, std::span<const ReparseEdit> edits);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace neomifes::syntax
