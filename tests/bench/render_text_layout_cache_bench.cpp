// Micro-benchmarks for TextLayoutCache (Phase 3c, ADR-011).
// Targets from detailed_design.md sec.4.3 are reproduced here as reminders
// and verified directly (CLAUDE.md rule #10: performance claims need bench
// proof, not speculation):
//   - 1 line's layout generation (cache miss): < 50us
//   - 1 line's cached-hit draw setup (cache hit): < 5us
//
// Deliberately does NOT go through RenderDevice/RenderPipeline/a live HWND -
// IDWriteTextLayout creation needs only the process-wide DirectWrite factory
// (d2d_factories.h), so this isolates TextLayoutCache's own CPU cost from
// D3D/D2D device/swap-chain/vsync overhead entirely (see the --measure-frame
// harness for the complementary full-frame-including-Present measurement).

#include <benchmark/benchmark.h>

#include <string>

#include "neomifes/render/d2d_factories.h"
#include "neomifes/render/text_layout_cache.h"

using neomifes::render::sharedDWriteFactory;
using neomifes::render::TextLayoutCache;

namespace {

constexpr float kMaxWidthDips  = 65536.0F;
constexpr float kMaxHeightDips = 65536.0F;

}  // namespace

static void BM_TextLayoutCache_Miss(benchmark::State& state) {
    auto factory = sharedDWriteFactory();
    if (!factory) {
        state.SkipWithError("sharedDWriteFactory() failed");
        return;
    }
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    const HRESULT hr = (*factory)->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"en-us", format.GetAddressOf());
    if (FAILED(hr)) {
        state.SkipWithError("CreateTextFormat() failed");
        return;
    }
    const std::u16string line = u"the quick brown fox jumps over the lazy dog 0123456789";

    // A fresh cache + incrementing line number every iteration guarantees
    // every getOrCreate() call is a genuine miss (no line number repeats).
    std::uint64_t lineNumber = 0;
    for (auto _ : state) {
        TextLayoutCache cache;
        const auto result = cache.getOrCreate(lineNumber++, line, *factory->Get(), *format.Get(),
                                               kMaxWidthDips, kMaxHeightDips);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TextLayoutCache_Miss);

static void BM_TextLayoutCache_Hit(benchmark::State& state) {
    auto factory = sharedDWriteFactory();
    if (!factory) {
        state.SkipWithError("sharedDWriteFactory() failed");
        return;
    }
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    const HRESULT hr = (*factory)->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"en-us", format.GetAddressOf());
    if (FAILED(hr)) {
        state.SkipWithError("CreateTextFormat() failed");
        return;
    }
    const std::u16string line = u"the quick brown fox jumps over the lazy dog 0123456789";

    TextLayoutCache cache;
    // Warm the cache once, outside the timed loop.
    const auto warm =
        cache.getOrCreate(0, line, *factory->Get(), *format.Get(), kMaxWidthDips, kMaxHeightDips);
    if (!warm.has_value()) {
        state.SkipWithError("warm-up getOrCreate() failed");
        return;
    }

    for (auto _ : state) {
        const auto result =
            cache.getOrCreate(0, line, *factory->Get(), *format.Get(), kMaxWidthDips, kMaxHeightDips);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TextLayoutCache_Hit);
