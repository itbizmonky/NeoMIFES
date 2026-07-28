#pragma once

// neomifes::syntax::IncrementalParser - stateful, single-language wrapper
// around tree-sitter's incremental reparse API (Phase 7k). Unlike
// parseCpp()/parsePython()/parse() (syntax.h, single-shot: new TSParser +
// TSTree per call, tree discarded immediately), this class retains the
// previous TSTree across reparse() calls and feeds each accumulated edit
// through ts_tree_edit() before reparsing, letting tree-sitter reuse
// unaffected subtrees instead of re-walking the whole document.
//
// Headless only (Phase 7k) - not wired into RenderPipeline/SyntaxWorker yet.
// SyntaxWorker's "discard superseded, keep only the latest request" queueing
// model is incompatible with incremental correctness (skipping even one
// edit permanently desyncs the retained tree's byte offsets from the real
// document), so that integration is deferred to a later sub-phase; see
// master_roadmap.md sec.7.9's "実装後の確定事項" for the Phase 7k/7l split
// rationale. NOT thread-safe for concurrent calls - single-writer use only,
// same assumption as document::Document.
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
