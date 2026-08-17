// WI-14b: measures LogModel::build(snapshot, rule)'s piece-streaming core
// (this WI's 設計方針1) directly - the O(document length) single linear
// pass that replaced the old per-line Document::lineText() re-snapshot
// cost. Modeled directly on syntax_parse_bench.cpp's
// makeSyntheticCppSource() (reserve()+bounded loop of += appends, no real
// file I/O): the goal here is proving the complexity class is O(N), not an
// end-to-end 10GB acceptance check (that's WI-13's tools/ scripts' job -
// see this WI's plan for why the two have different purposes).

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "neomifes/document/document.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"

using neomifes::document::Document;
using neomifes::logmode::builtInLogPatterns;
using neomifes::logmode::LogModel;
using neomifes::logmode::LogPatternRule;

namespace {

constexpr int kLineCount      = 50'000;
// Phase 7m/7t established the "10x line count checks whether a cost scales
// with document size or stays flat" pattern for this codebase's benchmarks
// - reused here for the same reason.
constexpr int kLargeLineCount = 500'000;

void appendNumber(std::u16string& out, int value) {
    const std::string digits = std::to_string(value);
    for (const char c : digits) {
        out += static_cast<char16_t>(c);
    }
}

// A synthetic Apache/Nginx Common Log Format corpus, one line matching
// log_pattern.cpp's apache_nginx_clf regex per iteration (varying host/
// path/size so the corpus isn't degenerate-repetitive).
std::u16string makeSyntheticApacheClfSource(int lineCount) {
    std::u16string content;
    content.reserve(static_cast<std::size_t>(lineCount) * 80);
    for (int i = 0; i < lineCount; ++i) {
        content += u"127.0.0.";
        appendNumber(content, i % 256);
        content += u" - - [10/Oct/2000:13:55:36 -0700] \"GET /page";
        appendNumber(content, i);
        content += u" HTTP/1.0\" 200 ";
        appendNumber(content, 100 + (i % 900));
        content += u"\n";
    }
    return content;
}

[[nodiscard]] const LogPatternRule& apacheClfRule() {
    for (const LogPatternRule& rule : builtInLogPatterns()) {
        if (rule.id == u"apache_nginx_clf") {
            return rule;
        }
    }
    return builtInLogPatterns().front();  // unreachable - apache_nginx_clf always present
}

void runLogModelBuildBenchmark(benchmark::State& state, int lineCount) {
    const std::u16string  source = makeSyntheticApacheClfSource(lineCount);
    Document               doc;
    doc.insertText(0, source);
    const auto             snapshot = doc.snapshot();
    const LogPatternRule&  rule     = apacheClfRule();

    for (auto _ : state) {
        auto result = LogModel::build(*snapshot, rule);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * lineCount);
    state.counters["source_KiB"] = static_cast<double>(source.size() * sizeof(char16_t)) / 1024.0;
}

}  // namespace

// Baseline: does a modest document build well within a reasonable time?
static void BM_LogModelBuild_ApacheClf_SmallDocument(benchmark::State& state) {
    runLogModelBuildBenchmark(state, kLineCount);
}
BENCHMARK(BM_LogModelBuild_ApacheClf_SmallDocument)->Unit(benchmark::kMillisecond)->Iterations(5);

// The DoD-relevant benchmark: 10x the document should cost roughly 10x, not
// worse - confirming the piece-streaming rewrite (WI-14b 設計方針1) is a
// single O(document length) pass rather than the old O(lines * pieces)
// per-line Document::lineText() re-snapshot cost.
static void BM_LogModelBuild_ApacheClf_LargeDocument(benchmark::State& state) {
    runLogModelBuildBenchmark(state, kLargeLineCount);
}
BENCHMARK(BM_LogModelBuild_ApacheClf_LargeDocument)->Unit(benchmark::kMillisecond)->Iterations(5);
