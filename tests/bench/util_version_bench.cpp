// Smoke benchmark: proves the google-benchmark toolchain is wired up.
// Real editor benchmarks (PieceTable, Rendering, Search, Undo) land in later phases.

#include <benchmark/benchmark.h>

#include "neomifes/util/version.h"

static void BM_ProductName(benchmark::State& state) {
    for (auto _ : state) {
        auto sv = neomifes::util::productName();
        benchmark::DoNotOptimize(sv);
    }
}
BENCHMARK(BM_ProductName);

static void BM_VersionString(benchmark::State& state) {
    for (auto _ : state) {
        auto sv = neomifes::util::versionString();
        benchmark::DoNotOptimize(sv);
    }
}
BENCHMARK(BM_VersionString);
