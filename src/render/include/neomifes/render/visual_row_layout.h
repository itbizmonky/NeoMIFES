#pragma once

// visual_row_layout (WI-21a) - given an already-built IDWriteTextLayout for
// ONE logical line's text (no embedded '\n' - drawVisibleLines() already
// splits on '\n' before calling TextLayoutCache::getOrCreate(), so this
// function never has to worry about that case itself), reports how many
// VISUAL rows that layout actually occupies once word wrap is on, and the
// [startColumn, endColumn) UTF-16-code-unit range each row covers.
//
// Standalone header (like viewport_math.h/gutter_math.h) rather than a
// RenderPipeline member, for the same reason TextLayoutCache itself is
// standalone: IDWriteTextLayout creation/inspection needs only the
// process-wide DirectWrite factory (d2d_factories.h), never an HWND/D3D
// device, so this stays unit-testable exactly the way
// tests/unit/render_text_layout_cache_test.cpp already proves is practical
// for DirectWrite-backed code in this codebase - real IDWriteTextFormat/
// IDWriteTextLayout objects, no window.
//
// When word wrap is OFF, RenderPipeline never calls this at all
// (visualRowCountForLine() short-circuits to a constant 1) - this module
// exists purely to serve the wrap-ON path.

#include <dwrite_3.h>

#include <cstdint>
#include <vector>

namespace neomifes::render {

struct VisualRowSpan {
    std::uint32_t startColumn;  // inclusive, UTF-16 code units into the line
    std::uint32_t endColumn;    // exclusive
};

// Uses IDWriteTextLayout::GetLineMetrics()'s standard 2-call sizing pattern
// (first call with maxLineCount=0 to learn the real count via
// E_NOT_SUFFICIENT_BUFFER, matching every other DirectWrite "give me the
// count" API in this codebase's own conventions). DWRITE_LINE_METRICS::
// length is documented to include any trailing whitespace/newline
// characters in that row - since the input layout never contains an
// embedded newline (see this header's own top comment), the per-row
// lengths sum cleanly into contiguous, non-overlapping [start, end) ranges
// with no gap or overlap.
//
// A non-wrapping (or short-enough-to-not-wrap) layout yields exactly one
// span covering the whole line - callers on the wrap-OFF path never call
// this at all (see top comment), but a wrap-ON layout for a short line
// naturally degrades to the same single-span shape, so no separate
// wrap-off code path is needed inside RenderPipeline's row-counting logic
// either.
//
// Always returns at least one span. GetLineMetrics() failing on a layout
// that was itself successfully created is not a realistic failure mode in
// practice (no I/O, no external resource - see TextLayoutCache's own
// contract, which never reports a "layout built but line metrics
// unavailable" case) - DWRITE_TEXT_METRICS has no text-length field to
// build a more informative fallback span from, so on the off chance of an
// unexpected HRESULT this falls back to a single empty-range span rather
// than an empty vector - callers (visualRowCountForLine()) rely on "at
// least 1 row for a real line" as an invariant, same as the wrap-off
// constant-1 path; an empty range is harmless (draws/hit-tests nothing
// extra) whereas an empty vector would violate that invariant outright.
[[nodiscard]] inline std::vector<VisualRowSpan> computeVisualRows(IDWriteTextLayout& layout) {
    UINT32        lineCount = 0;
    const HRESULT sizingHr  = layout.GetLineMetrics(nullptr, 0, &lineCount);
    if (sizingHr != E_NOT_SUFFICIENT_BUFFER && sizingHr != S_OK) {
        return {VisualRowSpan{.startColumn = 0, .endColumn = 0}};
    }
    if (lineCount == 0) {
        return {VisualRowSpan{.startColumn = 0, .endColumn = 0}};
    }

    std::vector<DWRITE_LINE_METRICS> lineMetrics(lineCount);
    const HRESULT fillHr = layout.GetLineMetrics(lineMetrics.data(), lineCount, &lineCount);
    if (FAILED(fillHr)) {
        return {VisualRowSpan{.startColumn = 0, .endColumn = 0}};
    }

    std::vector<VisualRowSpan> rows;
    rows.reserve(lineCount);
    std::uint32_t column = 0;
    for (UINT32 i = 0; i < lineCount; ++i) {
        const std::uint32_t length = lineMetrics[i].length;
        rows.push_back(VisualRowSpan{.startColumn = column, .endColumn = column + length});
        column += length;
    }
    return rows;
}

}  // namespace neomifes::render
