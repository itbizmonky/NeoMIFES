// Micro-benchmarks for PieceTable (RB-tree backed since Phase 2b2).
// Targets from detailed_design.md sec.18.3 / docs/issues/piece_table_rb_tree.md
// are reproduced here as reminders. Numbers reported by CI serve as both a
// baseline for tracking regressions and as the actual verification evidence
// for those targets (CLAUDE.md rule #10: performance claims need bench proof).
//
// BM_PieceTable_Snapshot_100K exists specifically to verify the "snapshot at
// 100K piece scale <= 1ms" target directly rather than extrapolating from the
// smaller BM_PieceTable_Snapshot (1000 pieces) - see the 2026-07-15 review
// finding in piece_table_rb_tree.md.

#include <benchmark/benchmark.h>

#include <random>
#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/piece_table.h"

using neomifes::document::PieceTable;
using neomifes::document::TextRange;

static void BM_PieceTable_InsertAtEnd(benchmark::State& state) {
    PieceTable pt;
    const std::u16string chunk(16, u'x');
    for (auto _ : state) {
        pt.insert(pt.length(), chunk);
        benchmark::DoNotOptimize(pt.length());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PieceTable_InsertAtEnd);

static void BM_PieceTable_InsertRandom(benchmark::State& state) {
    PieceTable pt;
    pt.insert(0, std::u16string(4096, u'a'));  // seed
    std::mt19937 rng{0xABCD1234u};
    const std::u16string chunk(4, u'y');
    for (auto _ : state) {
        state.PauseTiming();
        const auto pos = rng() % (pt.length() + 1);
        state.ResumeTiming();
        pt.insert(pos, chunk);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PieceTable_InsertRandom);

static void BM_PieceTable_Snapshot(benchmark::State& state) {
    PieceTable pt;
    for (int i = 0; i < 1000; ++i) {
        pt.insert(pt.length(), u"hello ");
    }
    for (auto _ : state) {
        auto snap = pt.snapshot();
        benchmark::DoNotOptimize(snap);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PieceTable_Snapshot);

// Direct verification of the "snapshot <= 1ms @ 100K pieces" target (rather
// than extrapolating from the 1000-piece BM_PieceTable_Snapshot above).
// Setup cost (100K inserts) happens once outside the timed loop; at the
// ~276ns/insert measured for BM_PieceTable_InsertAtEnd in Release this is
// roughly ~28ms one-time, well within CI's time budget.
static void BM_PieceTable_Snapshot_100K(benchmark::State& state) {
    PieceTable pt;
    constexpr int kPieceCount = 100'000;
    for (int i = 0; i < kPieceCount; ++i) {
        pt.insert(pt.length(), u"x");  // one char16_t == one piece per insert
    }
    for (auto _ : state) {
        auto snap = pt.snapshot();
        benchmark::DoNotOptimize(snap);
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["piece_count"] = static_cast<double>(kPieceCount);
}
BENCHMARK(BM_PieceTable_Snapshot_100K);

static void BM_PieceTable_ExtractAll(benchmark::State& state) {
    PieceTable pt;
    for (int i = 0; i < 1000; ++i) {
        pt.insert(pt.length(), u"hello ");
    }
    auto snap = pt.snapshot();
    for (auto _ : state) {
        auto s = snap->extract({0, snap->length()});
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * snap->length() * sizeof(char16_t));
}
BENCHMARK(BM_PieceTable_ExtractAll);
