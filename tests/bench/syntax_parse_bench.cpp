// First real-measurement data point for master_roadmap.md sec.7.11's "100 万
// 行 C++ ファイルの初回全解析: <= 5 秒" target. Phase 7a is a synchronous,
// single-shot parse with no incremental reparse or worker thread (see
// syntax.h's scope comment) - this benchmark exists to measure that baseline
// before any async/incremental work is considered (CLAUDE.md rule #10:
// optimize from measurements, not speculation), mirroring how
// search_find_all_bench.cpp established the Phase 5a SearchService baseline.
//
// kLineCount is scaled down from the 1M-line target the same way
// document_load_bench.cpp scales its 10GB target down to a 100MB file - the
// per-line cost measured here extrapolates linearly to the full target
// (tree-sitter's parse cost is proportional to input size, not file-open
// cost dominated by a fixed overhead).

#include <benchmark/benchmark.h>

#include <string>

#include "neomifes/syntax/syntax.h"

using neomifes::syntax::parseCpp;

namespace {

constexpr int kLineCount = 50'000;

void appendNumber(std::u16string& out, int value) {
    const std::string digits = std::to_string(value);
    for (const char c : digits) {
        out += static_cast<char16_t>(c);
    }
}

// A repeating class definition with fields, a method body, and occasional
// comments/preprocessor lines - not a single statement shape repeated
// verbatim, so the benchmark exercises a representative mix of the leaf
// classification paths in syntax.cpp (named leaves, anonymous keywords,
// punctuation, string/char literals, preprocessor directives).
std::u16string makeSyntheticCppSource() {
    std::u16string content;
    content.reserve(static_cast<std::size_t>(kLineCount) * 40);
    for (int i = 0; i < kLineCount; ++i) {
        content += u"class Widget";
        appendNumber(content, i);
        content += u" {\n public:\n  int value = ";
        appendNumber(content, i);
        content += u";\n  const char* name = \"widget\";\n  void tick() { value += 1; }\n";
        if (i % 500 == 0) {
            content += u"  // periodic checkpoint comment\n";
        }
        content += u"};\n";
    }
    return content;
}

}  // namespace

static void BM_ParseCpp_Synthetic(benchmark::State& state) {
    const std::u16string source = makeSyntheticCppSource();

    for (auto _ : state) {
        benchmark::DoNotOptimize(parseCpp(source));
    }
    state.SetItemsProcessed(state.iterations() * kLineCount);
    state.counters["source_KiB"] =
        static_cast<double>(source.size() * sizeof(char16_t)) / 1024.0;
}
BENCHMARK(BM_ParseCpp_Synthetic)->Unit(benchmark::kMillisecond)->Iterations(5);
