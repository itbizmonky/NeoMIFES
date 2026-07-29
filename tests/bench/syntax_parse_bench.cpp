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

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "neomifes/syntax/incremental_parser.h"
#include "neomifes/syntax/syntax.h"

using neomifes::syntax::applyTokenPatch;
using neomifes::syntax::IncrementalParser;
using neomifes::syntax::Language;
using neomifes::syntax::parseCpp;
using neomifes::syntax::ReparseEdit;
using neomifes::syntax::Token;

namespace {

constexpr int kLineCount      = 50'000;
// Phase 7m: 10x kLineCount, used only by BM_IncrementalReparse_SingleCharEdit_
// LargeDocument to demonstrate that the ts_tree_get_changed_ranges()-based
// token splice costs roughly the same regardless of document size (an
// asymptotic win, not just a constant-factor one) - see that benchmark.
constexpr int kLargeLineCount = 500'000;

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
std::u16string makeSyntheticCppSource(int lineCount) {
    std::u16string content;
    content.reserve(static_cast<std::size_t>(lineCount) * 40);
    for (int i = 0; i < lineCount; ++i) {
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
    const std::u16string source = makeSyntheticCppSource(kLineCount);

    for (auto _ : state) {
        benchmark::DoNotOptimize(parseCpp(source));
    }
    state.SetItemsProcessed(state.iterations() * kLineCount);
    state.counters["source_KiB"] =
        static_cast<double>(source.size() * sizeof(char16_t)) / 1024.0;
}
BENCHMARK(BM_ParseCpp_Synthetic)->Unit(benchmark::kMillisecond)->Iterations(5);

namespace {

[[nodiscard]] std::uint32_t rowAt(std::u16string_view text, std::size_t pos) {
    std::uint32_t row = 0;
    for (std::size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i] == u'\n') {
            ++row;
        }
    }
    return row;
}

[[nodiscard]] std::uint32_t columnAt(std::u16string_view text, std::size_t pos) {
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i] == u'\n') {
            lineStart = i + 1;
        }
    }
    return static_cast<std::uint32_t>((pos - lineStart) * 2);
}

}  // namespace

// Phase 7k: roadmap sec.7.11's "1 文字入力後の増分解析: <= 50ms" target.
// Toggles a single non-newline character (a digit, '0'<->'1') at a fixed
// position past the document's halfway point, back and forth every
// iteration - both directions describe the identical edit shape (one
// UTF-16 code unit replaced by one, same row/column), so a single retained
// IncrementalParser can be reused indefinitely without the document ever
// drifting in size or needing a per-iteration reset (which would defeat
// the point: reparseDelta() only takes the fast incremental path when a
// tree is already retained from a previous call). Shared by both
// benchmarks below (Phase 7m) - `lineCount` is the only difference between
// them, and that difference is the entire point of the large-document
// variant.
//
// Phase 7q: times reparseDelta() PLUS applyTokenPatch() together, not just
// reparseDelta() alone - render::SyntaxWorker::workerLoop() pays for both
// on every call (see syntax_worker.cpp), so timing only the first would
// understate the real per-keystroke cost this benchmark exists to measure
// against the roadmap DoD.
static void runIncrementalReparseSingleCharEditBenchmark(benchmark::State& state, int lineCount) {
    const std::u16string source = makeSyntheticCppSource(lineCount);
    IncrementalParser    parser(Language::Cpp);
    // Seeds the baseline tree AND the persisted token list, not timed.
    std::vector<Token> tokens = applyTokenPatch({}, parser.reparseDelta(source, {}));

    std::size_t editPos = source.size() / 2;
    if (source[editPos] == u'\n') {
        ++editPos;  // never edit the newline itself - keeps row/col symmetric below
    }
    std::u16string variant = source;
    variant[editPos]       = (variant[editPos] == u'0') ? u'1' : u'0';

    const std::uint32_t row    = rowAt(source, editPos);
    const std::uint32_t column = columnAt(source, editPos);
    const auto           byteAt = static_cast<std::uint32_t>(editPos * 2);
    const ReparseEdit     edit{
        .startByte    = byteAt,
        .oldEndByte   = byteAt + 2,
        .newEndByte   = byteAt + 2,
        .startRow     = row,
        .startColumn  = column,
        .oldEndRow    = row,
        .oldEndColumn = column + 2,
        .newEndRow    = row,
        .newEndColumn = column + 2,
    };

    bool useVariant = false;
    for (auto _ : state) {
        const std::u16string_view text = useVariant ? std::u16string_view(variant) : std::u16string_view(source);
        const auto patch = parser.reparseDelta(text, std::array{edit});
        tokens            = applyTokenPatch(std::move(tokens), patch);
        benchmark::DoNotOptimize(tokens);
        useVariant = !useVariant;
    }
    state.counters["source_KiB"] =
        static_cast<double>(source.size() * sizeof(char16_t)) / 1024.0;
}

static void BM_IncrementalReparse_SingleCharEdit(benchmark::State& state) {
    runIncrementalReparseSingleCharEditBenchmark(state, kLineCount);
}
BENCHMARK(BM_IncrementalReparse_SingleCharEdit)->Unit(benchmark::kMillisecond)->Iterations(20);

// Phase 7m: same edit shape as above but on a 10x larger document
// (kLargeLineCount) - originally added hoping to show a roughly CONSTANT
// cost regardless of document size (an asymptotic win). The actual measured
// result: this costs roughly 10x the 50,000-line benchmark above, i.e.
// genuinely proportional to document size, not flat. See
// incremental_parser.h's header comment for why (reparse() still allocates
// and shifts a token vector sized to the whole document on every call) -
// the win Phase 7m actually delivers is a substantial constant-factor
// speedup (avoiding tree-sitter API calls for unaffected regions), not the
// hoped-for asymptotic one. Kept as a benchmark specifically BECAUSE it
// disproved that optimistic assumption with a real measurement rather than
// leaving it unverified (CLAUDE.md rule 10) - removing it would hide this
// finding, not just the code that produced it.
static void BM_IncrementalReparse_SingleCharEdit_LargeDocument(benchmark::State& state) {
    runIncrementalReparseSingleCharEditBenchmark(state, kLargeLineCount);
}
BENCHMARK(BM_IncrementalReparse_SingleCharEdit_LargeDocument)->Unit(benchmark::kMillisecond)->Iterations(20);
