// Micro-benchmarks for the Phase 2a PieceTable MVP.
// Targets from detailed_design.md sec.18.3 are reproduced here as reminders;
// the vector-backed MVP is not expected to hit them yet (that is Phase 2b work
// with the RB-tree). Numbers reported by CI serve as a baseline for tracking
// improvement over time.

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
