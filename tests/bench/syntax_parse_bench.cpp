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

using neomifes::syntax::IncrementalParser;
using neomifes::syntax::Language;
using neomifes::syntax::parseCpp;
using neomifes::syntax::ReparseEdit;

namespace {

constexpr int kLineCount      = 50'000;
// Phase 7m: 10x kLineCount, used by the *_LargeDocument benchmarks below to
// check whether a cost scales with document size or stays flat.
constexpr int kLargeLineCount = 500'000;
// Phase 7t: ~150 lines' worth of code units (average ~40 chars/line, see
// makeSyntheticCppSource()'s reserve() call below) - approximates the
// "visible viewport + one screenful of margin on each side" window
// RenderPipeline::computeDesiredTokenRange() actually requests in the real
// app. Not tuned/benchmarked itself (CLAUDE.md rule 10 - this is the
// starting-point margin size Phase 7t's plan documented as untuned).
constexpr std::size_t kNarrowWindowCodeUnits = 6'000;

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
// the point: reparseRange() only takes the fast incremental tree-sitter
// path when a tree is already retained from a previous call).
//
// Phase 7t: `windowCodeUnits` controls how much of the document is
// REQUESTED (0 = the whole document, matching Phase 7q-era behavior and
// kept as a comparison baseline; otherwise a window centered on the edit -
// see kNarrowWindowCodeUnits). This does NOT include
// render::SyntaxWorker::workerLoop()'s own BufferSnapshot::extract() cost
// (materializing the whole document's text every call) - reparseRange()
// itself still needs the FULL text passed to
// ts_parser_parse_string_encoding() regardless of the requested range, so
// this benchmark isolates "how much does narrowing the WALKED range help"
// from "does the parse call itself scale with document size", not the
// latter question by itself. See this file's benchmark registrations below
// for how the results are meant to be read together.
static void runReparseRangeSingleCharEditBenchmark(benchmark::State& state, int lineCount,
                                                    std::size_t windowCodeUnits) {
    const std::u16string source = makeSyntheticCppSource(lineCount);
    IncrementalParser    parser(Language::Cpp);
    // Seeds the baseline retained tree, not timed.
    (void)parser.reparseRange(source, {}, 0, static_cast<std::uint32_t>(source.size() * 2));

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

    std::uint32_t rangeStartByte = 0;
    std::uint32_t rangeEndByte   = static_cast<std::uint32_t>(source.size() * 2);
    if (windowCodeUnits > 0) {
        const std::size_t half        = windowCodeUnits / 2;
        const std::size_t windowStart = editPos > half ? editPos - half : 0;
        const std::size_t windowEnd   = std::min(source.size(), editPos + half);
        rangeStartByte                = static_cast<std::uint32_t>(windowStart * 2);
        rangeEndByte                  = static_cast<std::uint32_t>(windowEnd * 2);
    }

    bool useVariant = false;
    for (auto _ : state) {
        const std::u16string_view text = useVariant ? std::u16string_view(variant) : std::u16string_view(source);
        auto tokens = parser.reparseRange(text, std::array{edit}, rangeStartByte, rangeEndByte);
        benchmark::DoNotOptimize(tokens);
        useVariant = !useVariant;
    }
    state.counters["source_KiB"] =
        static_cast<double>(source.size() * sizeof(char16_t)) / 1024.0;
}

// The DoD-determining benchmark on a modest document: does a narrow-window
// request cost well under 50ms even before accounting for extract()/parse
// costs this file doesn't measure (see the function comment above)?
static void BM_ReparseRange_SingleCharEdit_SmallDocument_NarrowWindow(benchmark::State& state) {
    runReparseRangeSingleCharEditBenchmark(state, kLineCount, kNarrowWindowCodeUnits);
}
BENCHMARK(BM_ReparseRange_SingleCharEdit_SmallDocument_NarrowWindow)->Unit(benchmark::kMillisecond)->Iterations(20);

// Same narrow window, 10x the document (kLargeLineCount) - THE key
// benchmark for Phase 7t's DoD verdict: if this costs roughly the SAME as
// the small-document variant above, the walked-range cost genuinely stopped
// scaling with document size (Phase 7q's applyTokenPatch() bottleneck is
// gone). If it still scales with document size despite the identical
// narrow window, the remaining cost is coming from somewhere this
// benchmark's own text is already fully materialized past (i.e.
// ts_parser_parse_string_encoding()'s own per-call cost, or
// BufferSnapshot::extract() in the real worker) - see this file's top
// function comment.
static void BM_ReparseRange_SingleCharEdit_LargeDocument_NarrowWindow(benchmark::State& state) {
    runReparseRangeSingleCharEditBenchmark(state, kLargeLineCount, kNarrowWindowCodeUnits);
}
BENCHMARK(BM_ReparseRange_SingleCharEdit_LargeDocument_NarrowWindow)->Unit(benchmark::kMillisecond)->Iterations(20);

// Comparison baseline: requesting the WHOLE document as the range, on the
// large document - continuity with Phase 7q's own
// BM_IncrementalReparse_SingleCharEdit_LargeDocument (103ms/989ms at 50k/
// 500k lines, Release), and a sanity check that the narrow-window variant
// above is actually meaningfully faster than asking for everything on the
// SAME document size (if it isn't, the range-scoping itself isn't buying
// anything, independent of whatever the parse-call-cost question above
// resolves to).
static void BM_ReparseRange_SingleCharEdit_LargeDocument_FullDocument(benchmark::State& state) {
    runReparseRangeSingleCharEditBenchmark(state, kLargeLineCount, 0);
}
BENCHMARK(BM_ReparseRange_SingleCharEdit_LargeDocument_FullDocument)->Unit(benchmark::kMillisecond)->Iterations(20);
