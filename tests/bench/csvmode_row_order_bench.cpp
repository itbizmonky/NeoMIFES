// WI-16d: measures computeCsvRowOrder()'s filter and sort passes against
// master_roadmap.md §10.2's stated performance targets ("フィルタ適用
// (100万行): ≤1秒" / "ソート(100万行): ≤3秒"). Modeled directly on
// logmode_index_bench.cpp's structure (WI-14b) - a synthetic corpus built
// once outside the timed loop (reserve()+bounded loop of += appends, no
// real file I/O), Small/Large 10x pairing to show the cost scales with row
// count rather than something else, CsvModel::build() itself run once
// outside the loop so only computeCsvRowOrder()'s own cost is measured.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <string>

#include "neomifes/csvmode/csv_model.h"
#include "neomifes/csvmode/csv_row_order.h"
#include "neomifes/document/document.h"

using neomifes::csvmode::computeCsvRowOrder;
using neomifes::csvmode::CsvFilterOptions;
using neomifes::csvmode::CsvModel;
using neomifes::csvmode::CsvSortDirection;
using neomifes::csvmode::CsvSortOptions;
using neomifes::document::Document;

namespace {

constexpr int kRowCount = 100'000;
// roadmap §10.2's targets are both stated at 100万 rows - the
// *_LargeDocument benchmarks below are the DoD-relevant ones.
constexpr int kLargeRowCount = 1'000'000;

void appendNumber(std::u16string& out, long long value) {
    const std::string digits = std::to_string(value);
    for (const char c : digits) {
        out += static_cast<char16_t>(c);
    }
}

// 3-column synthetic CSV (id, name, score). `name` alternates between
// "needle" (1 row in 10) and "haystack" so a filter query has a realistic,
// non-degenerate hit rate rather than matching everything or nothing.
// `score` is a pseudo-scattered integer (i*7919 mod 1000000, NOT a
// monotonic sequence) so the sort benchmark does real comparison work
// instead of confirming an already-sorted column.
std::u16string makeSyntheticCsvSource(int rowCount) {
    std::u16string content;
    content.reserve(static_cast<std::size_t>(rowCount) * 24 + 16);
    content += u"id,name,score\n";
    for (int i = 0; i < rowCount; ++i) {
        appendNumber(content, i);
        content += u",";
        content += (i % 10 == 0) ? u"needle" : u"haystack";
        content += u",";
        appendNumber(content, (static_cast<long long>(i) * 7919) % 1'000'000);
        content += u"\n";
    }
    return content;
}

void runFilterBenchmark(benchmark::State& state, int rowCount) {
    const std::u16string source = makeSyntheticCsvSource(rowCount);
    Document              doc;
    doc.insertText(0, source);
    const auto model = CsvModel::build(doc);

    CsvFilterOptions filter;
    filter.query = u"needle";

    for (auto _ : state) {
        auto order = computeCsvRowOrder(*model, doc, filter, CsvSortOptions{});
        benchmark::DoNotOptimize(order);
    }
    state.SetItemsProcessed(state.iterations() * rowCount);
    state.counters["source_KiB"] = static_cast<double>(source.size() * sizeof(char16_t)) / 1024.0;
}

void runSortBenchmark(benchmark::State& state, int rowCount) {
    const std::u16string source = makeSyntheticCsvSource(rowCount);
    Document              doc;
    doc.insertText(0, source);
    const auto model = CsvModel::build(doc);

    CsvSortOptions sort;
    sort.column    = 2;  // score
    sort.direction = CsvSortDirection::Ascending;

    for (auto _ : state) {
        auto order = computeCsvRowOrder(*model, doc, CsvFilterOptions{}, sort);
        benchmark::DoNotOptimize(order);
    }
    state.SetItemsProcessed(state.iterations() * rowCount);
    state.counters["source_KiB"] = static_cast<double>(source.size() * sizeof(char16_t)) / 1024.0;
}

}  // namespace

// Baseline: does a modest CSV filter well within a reasonable time?
static void BM_ComputeCsvRowOrder_Filter_SmallDocument(benchmark::State& state) {
    runFilterBenchmark(state, kRowCount);
}
BENCHMARK(BM_ComputeCsvRowOrder_Filter_SmallDocument)->Unit(benchmark::kMillisecond)->Iterations(5);

// DoD-relevant: roadmap §10.2 targets ≤1 second for a 100万-row filter.
static void BM_ComputeCsvRowOrder_Filter_LargeDocument(benchmark::State& state) {
    runFilterBenchmark(state, kLargeRowCount);
}
BENCHMARK(BM_ComputeCsvRowOrder_Filter_LargeDocument)->Unit(benchmark::kMillisecond)->Iterations(5);

// Baseline: does a modest CSV sort well within a reasonable time?
static void BM_ComputeCsvRowOrder_Sort_SmallDocument(benchmark::State& state) {
    runSortBenchmark(state, kRowCount);
}
BENCHMARK(BM_ComputeCsvRowOrder_Sort_SmallDocument)->Unit(benchmark::kMillisecond)->Iterations(5);

// DoD-relevant: roadmap §10.2 targets ≤3 seconds for a 100万-row sort.
static void BM_ComputeCsvRowOrder_Sort_LargeDocument(benchmark::State& state) {
    runSortBenchmark(state, kLargeRowCount);
}
BENCHMARK(BM_ComputeCsvRowOrder_Sort_LargeDocument)->Unit(benchmark::kMillisecond)->Iterations(5);
