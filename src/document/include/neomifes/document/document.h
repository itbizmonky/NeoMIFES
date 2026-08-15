#pragma once

// Document - facade that combines PieceTable and LineIndex.
// This is what upper layers (Editor Core, Application) will use; they should
// not touch PieceTable / LineIndex directly.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/document/line_index.h"
#include "neomifes/document/piece_table.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {

class BufferSnapshot;
class OriginalBuffer;

// One mutation's before/after position info (Phase 7k), in a shape
// tree-sitter's TSInputEdit can be built from directly by callers in
// neomifes::syntax (byte offset = code-unit offset * 2, kept out of this
// header since syntax:: owns tree-sitter types, not document::).
// startLine/startColumn are the SAME in both the old and new coordinate
// space (the edit starts there and nothing before it moves); oldEndLine/
// oldEndColumn are computed against the PRE-edit line structure,
// newEndLine/newEndColumn against the POST-edit one - Document captures
// both at the right moment inside each mutating method (see document.cpp),
// since PieceTable mutates in place and there is no per-version snapshot
// to query "the old line structure" from afterward.
struct EditDelta {
    TextPos       startPos;
    LineNumber    startLine;
    std::uint32_t startColumn = 0;
    TextPos       oldEndPos;
    LineNumber    oldEndLine;
    std::uint32_t oldEndColumn = 0;
    TextPos       newEndPos;
    LineNumber    newEndLine;
    std::uint32_t newEndColumn = 0;
};

class Document {
public:
    Document();
    explicit Document(std::shared_ptr<const OriginalBuffer> original);

    Document(const Document&)            = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) noexcept        = default;
    Document& operator=(Document&&) noexcept = default;
    ~Document() = default;

    // --- Mutation ----------------------------------------------------------
    void insertText(TextPos pos, std::u16string_view text);
    void eraseRange(TextRange range);
    void replaceRange(TextRange range, std::u16string_view text);

    // --- Read ---------------------------------------------------------------
    [[nodiscard]] std::shared_ptr<const BufferSnapshot> snapshot() const {
        return m_pieceTable.snapshot();
    }
    [[nodiscard]] std::uint64_t length()    const noexcept { return m_pieceTable.length(); }
    [[nodiscard]] std::uint64_t lineCount() const noexcept { return m_pieceTable.lineCount(); }
    [[nodiscard]] std::size_t   pieceCount() const noexcept { return m_pieceTable.pieceCount(); }

    // Monotonically increasing counter, bumped by every mutating call
    // (insertText/eraseRange/replaceRange). Single UI thread only (ADR-009)
    // - plain uint64_t, no atomics needed. Observers (RenderPipeline) compare
    // this against the version they last cached a snapshot at, per
    // detailed_design.md sec.4.3's "don't call snapshot() every frame" rule
    // (see ADR-010).
    [[nodiscard]] std::uint64_t version() const noexcept { return m_version; }

    // WI-01: true iff at least one mutation has happened since the last
    // markSaved() call (or since construction, if markSaved() was never
    // called - a freshly-loaded or freshly-constructed empty Document
    // starts at m_version == m_savedVersion == 0, so this correctly reports
    // "not dirty" with no special-casing needed for either path).
    [[nodiscard]] bool isDirty() const noexcept { return m_version != m_savedVersion; }

    // Called by document::saveFile() (file_saver.h) after a successful
    // save. Snapshots the current version as "the last saved one" - any
    // mutation after this call makes isDirty() true again.
    void markSaved() noexcept { m_savedVersion = m_version; }

    // WI-11: symmetric counterpart to markSaved() - forces isDirty()==true
    // WITHOUT a real mutation. A freshly-constructed Document always starts
    // m_version==m_savedVersion==0 (clean by definition), even when its
    // content came from a crash-recovery autosave snapshot rather than the
    // document's own associated real file - see app::main.cpp's recovery
    // path, the only caller. Does not append an EditDelta (unlike
    // insertText()/eraseRange()/replaceRange()) since nothing was actually
    // edited; RenderPipeline's own staleness detection doesn't need one
    // either here, since attaching a recovered session to the active tab
    // already goes through setLanguage()'s unconditional
    // m_hasCachedSnapshot=false path (same as any other document swap).
    void markDirty() noexcept { ++m_version; }

    // Convenience read of the whole document.
    [[nodiscard]] std::u16string toU16String() const;

    // Drains and returns every EditDelta recorded since the last call to
    // this method (or since construction) - Phase 7k. insertText()/
    // eraseRange()/replaceRange() each append exactly one entry, in call
    // order; UndoStack's commands go through those same three methods (see
    // edit_commands.cpp), so undo/redo need no separate tracking path.
    // Single UI thread only (ADR-009, same as version()) - no
    // synchronization needed.
    [[nodiscard]] std::vector<EditDelta> takePendingEdits() noexcept {
        return std::exchange(m_pendingEdits, {});
    }

    // --- Line queries -------------------------------------------------------
    // The Document keeps a LineIndex consistent incrementally: every
    // mutating method below applies the edit to it directly (LineIndex::
    // applyInsert()/applyErase(), Phase 7p) rather than marking it dirty for
    // a lazy full rebuild - see docs/issues/line_index_o_log_n.md ("案C") for
    // why a lazy rebuild-on-next-query design (Phase 2a-7o) became an O(document
    // length) cost on every single edit once EditDelta computation (Phase 7k)
    // started querying offsetToLine()/lineToOffset() from inside insertText()
    // itself. m_lineIndexDirty now only matters for the very first query
    // after construction. Logically const (callers only ever observe query
    // results, never the cache itself) - m_lineIndex/m_lineIndexDirty are
    // `mutable` so RenderPipeline can query through a `const Document*`
    // (ADR-010).
    [[nodiscard]] LineNumber offsetToLine(TextPos pos) const;
    [[nodiscard]] TextPos    lineToOffset(LineNumber line) const;

    // Returns line `line`'s text, EXCLUDING the trailing '\n' - same
    // convention as RenderPipeline::extractLineText() (render_pipeline.cpp,
    // Phase 7o). `line` is clamped the same way every other line query in
    // this class already is (lineToOffset()/offsetToLine() delegate to
    // LineIndex, which clamps out-of-range input - see line_index.cpp), so
    // this method has no failure mode.
    //
    // Deliberately NOT shared with RenderPipeline::extractLineText(): that
    // method reads through RenderPipeline's own m_cachedSnapshot member (a
    // Phase 3c/ADR-010 perf optimization - "don't call snapshot() every
    // frame", since it runs once per visible line on every repaint).
    // Document has no per-frame call rate and no cached snapshot member of
    // its own, so this takes a fresh snapshot() on every call - sharing an
    // implementation across these two different performance contexts would
    // add indirection without removing duplication that matters (CLAUDE.md
    // rule 10).
    [[nodiscard]] std::u16string lineText(LineNumber line) const;

    // Converts a (line, column) pair to a TextPos (Phase 8b, for the
    // NeoMifesCoreApi plugin bridge). `column` is a UTF-16 code-unit offset
    // from the line's start (same convention as EditDelta::startColumn/
    // newEndColumn above). Computed as lineToOffset(line) + column, clamped
    // to length() so an out-of-range column never produces an out-of-range
    // TextPos.
    //
    // Deliberately does NOT further clamp `column` to stay within *this
    // line's own* bounds - e.g. against a multi-line document,
    // (line=0, column=999) returns length() (the document's own end),
    // silently spilling onto later lines rather than clamping to line 0's
    // actual end. This is a minimal, documented boundary-clamp choice (only
    // the document's own end is guaranteed), not a promise of per-line
    // precision - see ADR-016.
    [[nodiscard]] TextPos lineColumnToOffset(LineNumber line, std::uint32_t column) const;

private:
    void ensureLineIndex() const;

    PieceTable            m_pieceTable;
    mutable LineIndex     m_lineIndex;
    mutable bool          m_lineIndexDirty = true;
    std::uint64_t         m_version        = 0;
    // WI-01: version() value as of the last markSaved() call. See
    // isDirty()'s comment for why both start at 0 with no extra ceremony.
    std::uint64_t         m_savedVersion   = 0;
    // Phase 7k: accumulated by insertText()/eraseRange()/replaceRange(),
    // drained by takePendingEdits().
    std::vector<EditDelta> m_pendingEdits;
};

}  // namespace neomifes::document
