// File-save benchmark for document::saveFile() (WI-01, build_plan.md §5).
// Target: peak transient memory during a save must not scale proportionally
// with file size (bounded-chunk streaming, see file_saver.cpp's
// kLinesPerChunk/kMaxChunkCodeUnits) - the same "10GB対応の生命線" principle
// document_load_bench.cpp's Working Set target enforces on the load side.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "neomifes/document/document.h"
#include "neomifes/document/file_saver.h"
#include "neomifes/encoding/encoding.h"
#include "neomifes/platform/process_metrics.h"

namespace {

using neomifes::document::Document;
using neomifes::document::saveFile;
using neomifes::document::SaveError;
using neomifes::encoding::Encoding;
using neomifes::encoding::LineEnding;

// Builds a Document containing `totalCodeUnits` code units of repeating
// ASCII text (Utf8-encodes 1:1 to bytes, so the saved file's byte size
// matches `totalCodeUnits`). Built via a handful of large insertText()
// calls on a reused chunk buffer, rather than materializing one
// std::u16string the size of the whole target - keeps fixture construction
// itself from dominating the memory this benchmark characterizes.
Document buildSyntheticDocument(std::uint64_t totalCodeUnits) {
    constexpr std::u16string_view kLine = u"The quick brown fox jumps over the lazy dog. 0123456789\n";
    constexpr std::uint64_t       kChunkCodeUnits = 1024 * 1024;  // 1M CU per insertText() call

    std::u16string chunk;
    chunk.reserve(kChunkCodeUnits + kLine.size());
    while (chunk.size() < kChunkCodeUnits) {
        chunk.append(kLine);
    }
    chunk.resize(kChunkCodeUnits);

    Document      doc;
    std::uint64_t written = 0;
    while (written + kChunkCodeUnits <= totalCodeUnits) {
        doc.insertText(doc.length(), chunk);
        written += kChunkCodeUnits;
    }
    if (written < totalCodeUnits) {
        doc.insertText(doc.length(), std::u16string_view(chunk).substr(0, totalCodeUnits - written));
    }
    return doc;
}

// Not a production code path, so throwing on an unexpected saveFile()
// failure (rather than std::expected plumbing) is acceptable here per
// CLAUDE.md's "recoverable errors only" scoping - same convention
// document_load_bench.cpp's generateMockFile() already uses.
void saveOrThrow(Document& doc, const std::filesystem::path& path) {
    if (const auto err = saveFile(doc, path, Encoding::Utf8, LineEnding::Lf, /*writeBom=*/false)) {
        throw std::runtime_error("document_save_bench: saveFile() unexpectedly failed");
    }
}

// Saves `doc` to `path` once and reports the resulting PEAK-working-set
// delta as a custom counter - NOT a before/after resting delta. A resting
// delta would miss a large TRANSIENT allocation freed again before the
// "after" snapshot, which is exactly the regression this benchmark exists
// to catch: a regression back to "materialize the whole document before
// writing" would still show a small resting delta once that temporary
// buffer is freed, but would spike PEAK memory proportionally to file
// size. peakWorkingSetBytes is monotonically non-decreasing for the
// process's life, so a before/after delta of it correctly captures that
// spike regardless of what happens to memory afterward.
void reportPeakWorkingSetDelta(benchmark::State& state, Document& doc, const std::filesystem::path& path) {
    const auto before = neomifes::platform::currentProcessMemory();
    saveOrThrow(doc, path);
    const auto after = neomifes::platform::currentProcessMemory();
    state.counters["peak_working_set_delta_MiB"] =
        static_cast<double>(after.peakWorkingSetBytes - before.peakWorkingSetBytes) / (1024.0 * 1024.0);
}

void saveFileBenchBody(benchmark::State& state, std::uint64_t totalCodeUnits, const std::filesystem::path& path) {
    Document doc = buildSyntheticDocument(totalCodeUnits);

    reportPeakWorkingSetDelta(state, doc, path);

    for (auto _ : state) {
        saveOrThrow(doc, path);
    }

    state.counters["file_MiB"] = static_cast<double>(totalCodeUnits) / (1024.0 * 1024.0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}  // namespace

static void BM_SaveFile_100MB(benchmark::State& state) {
    saveFileBenchBody(state, 100ull * 1024 * 1024,
                       std::filesystem::temp_directory_path() / "neomifes_bench_save_100mb.txt");
}
BENCHMARK(BM_SaveFile_100MB)->Unit(benchmark::kMillisecond)->Iterations(3);
