#pragma once

// TextLayoutCache - caches IDWriteTextLayout objects keyed by line number so
// RenderPipeline doesn't redo DirectWrite text analysis/shaping every frame
// for lines whose content hasn't changed since the last render (Phase 3c,
// ADR-011). Standalone (not a RenderPipeline nested type) so it can be built
// and unit-tested without an HWND/RenderDevice - IDWriteTextLayout creation
// needs only the process-wide DirectWrite factory (d2d_factories.h) and a
// text format, neither of which is device-bound.

#include <dwrite_3.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "neomifes/document/text_pos.h"
#include "neomifes/render/render_error.h"

namespace neomifes::render {

struct TextLayoutCacheStats {
    std::uint64_t hits   = 0;
    std::uint64_t misses = 0;
};

class TextLayoutCache {
public:
    // Returns a non-owning pointer to a cached-or-freshly-created layout for
    // `line`. On a cache miss, calls dwriteFactory.CreateTextLayout(lineText,
    // ..., textFormat, maxWidthDips, maxHeightDips, ...) and stores the
    // result. maxWidthDips/maxHeightDips must stay the same across the
    // cache's lifetime - a hit does NOT re-validate the stored layout
    // against these parameters, so callers must pass values that don't
    // depend on anything that changes without also calling clear() (see
    // RenderPipeline: a fixed, window-size-independent bound is used
    // precisely so resize() never needs to invalidate this cache).
    [[nodiscard]] RenderExpected<IDWriteTextLayout*> getOrCreate(
        document::LineNumber line, std::u16string_view lineText,
        IDWriteFactory7& dwriteFactory, IDWriteTextFormat& textFormat, float maxWidthDips,
        float maxHeightDips) noexcept;

    // Wholesale invalidation - the only granularity Phase 3c needs (no
    // per-region change source exists yet; see ADR-011).
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }
    [[nodiscard]] TextLayoutCacheStats stats() const noexcept { return m_stats; }
    void resetStats() noexcept { m_stats = {}; }

private:
    std::unordered_map<document::LineNumber, Microsoft::WRL::ComPtr<IDWriteTextLayout>> m_entries;
    TextLayoutCacheStats m_stats;
};

}  // namespace neomifes::render
