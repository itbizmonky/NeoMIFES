// First real-measurement data point for the requirements doc sec.5 NFR
// "検索: 数GBファイルでも高速" (search stays fast even on multi-GB files).
// Phase 5a is a synchronous, single-line-scan implementation with no SIMD/
// chunked-parallel optimization (see search_service.h's scope comment and
// the Phase 5a plan) - this benchmark exists to measure that baseline
// before any optimization work is considered (CLAUDE.md rule #10: optimize
// from measurements, not speculation), mirroring how
// core_undo_stack_bench.cpp established the Phase 4a UndoStack baseline.

#include <benchmark/benchmark.h>

#include <string>

#include "neomifes/document/document.h"
#include "neomifes/search/search_service.h"

using neomifes::document::Document;
using neomifes::search::Query;
using neomifes::search::SearchService;

namespace {

constexpr int kLineCount = 200'000;

// std::u16string has no operator+= for a narrow std::to_string() result;
// this avoids pulling in a full <codecvt>/iostream conversion just to embed
// an ASCII decimal number in the synthetic content below.
void appendNumber(std::u16string& out, int value) {
    const std::string digits = std::to_string(value);
    for (char c : digits) {
        out += static_cast<char16_t>(c);
    }
}

// Builds one large insertText() call rather than kLineCount small ones, so
// the benchmark measures findAll() cost, not Piece Table insertion cost.
// Every 1000th line contains "ERROR" so a literal/regex search over this
// document exercises realistic sparse-match scanning (log-search shaped,
// per requirements doc sec.8's log-analysis-mode motivation for Phase 5).
Document makeSyntheticDocument() {
    std::u16string content;
    content.reserve(static_cast<std::size_t>(kLineCount) * 64);
    for (int i = 0; i < kLineCount; ++i) {
        content += u"The quick brown fox jumps over the lazy dog, id=";
        appendNumber(content, i);
        if (i % 1000 == 0) {
            content += u" ERROR something went wrong";
        }
        content += u"\n";
    }

    Document doc;
    doc.insertText(0, content);
    return doc;
}

}  // namespace

static void BM_SearchService_FindAll_LiteralSparseMatch(benchmark::State& state) {
    const Document doc = makeSyntheticDocument();
    const Query query{.pattern = u"ERROR", .caseSensitive = true};

    for (auto _ : state) {
        benchmark::DoNotOptimize(SearchService::findAll(doc, query));
    }
    state.SetItemsProcessed(state.iterations() * kLineCount);
}
BENCHMARK(BM_SearchService_FindAll_LiteralSparseMatch);

static void BM_SearchService_FindAll_RegexSparseMatch(benchmark::State& state) {
    const Document doc = makeSyntheticDocument();
    const Query query{.pattern = u"id=\\d{3}\\b", .regex = true};

    for (auto _ : state) {
        benchmark::DoNotOptimize(SearchService::findAll(doc, query));
    }
    state.SetItemsProcessed(state.iterations() * kLineCount);
}
BENCHMARK(BM_SearchService_FindAll_RegexSparseMatch);

static void BM_SearchService_FindAll_NoMatch(benchmark::State& state) {
    const Document doc = makeSyntheticDocument();
    const Query query{.pattern = u"this pattern never appears anywhere", .caseSensitive = true};

    for (auto _ : state) {
        benchmark::DoNotOptimize(SearchService::findAll(doc, query));
    }
    state.SetItemsProcessed(state.iterations() * kLineCount);
}
BENCHMARK(BM_SearchService_FindAll_NoMatch);
