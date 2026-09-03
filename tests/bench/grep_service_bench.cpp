// CI-sized benchmark for GrepService::findAll() (multi-file search), added
// to close search_grep_multi_gb_performance_gap.md's residual completion
// criterion - search_find_all_bench.cpp already establishes a baseline for
// the single-file SearchService::findAll() path, but nothing measured the
// multi-file GrepService path on a recurring basis. WI-23's actual fix
// (detectLineEndingBounded()'s redundant decode inside loadUtf8File(), NOT
// the originally-hypothesized OriginalBuffer::scanUtf8()) was found via a
// larger one-off diagnostic probe (2,000 files, ~293MB) that was never
// committed; this benchmark is a permanent CI-time artifact instead, so its
// scale is deliberately capped well below that to stay inexpensive to run
// on every build.

#include <benchmark/benchmark.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include "neomifes/search/grep_service.h"

using neomifes::search::GrepQuery;
using neomifes::search::GrepService;
using neomifes::search::Query;

namespace {

constexpr int kFileCount = 500;
constexpr int kLinesPerFile = 800;  // ~50KB/file -> ~25MB total across kFileCount files

// Every kMatchLineInterval-th line contains "ERROR" so findAll() exercises
// realistic sparse-match scanning (same shape as search_find_all_bench.cpp's
// synthetic document), not a pathological "match every line" case.
constexpr int kMatchLineInterval = 50;

void writeSyntheticFile(const std::filesystem::path& path, int fileIndex) {
    std::string content;
    content.reserve(static_cast<std::size_t>(kLinesPerFile) * 64);
    for (int i = 0; i < kLinesPerFile; ++i) {
        content += "The quick brown fox jumps over the lazy dog, id=";
        content += std::to_string((fileIndex * kLinesPerFile) + i);
        if (i % kMatchLineInterval == 0) {
            content += " ERROR something went wrong";
        }
        content += "\n";
    }

    std::FILE* fp = nullptr;
    if (::_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == nullptr) {
        throw std::runtime_error("writeSyntheticFile: failed to open output file");
    }
    std::fwrite(content.data(), 1, content.size(), fp);
    std::fclose(fp);
}

// Generates kFileCount synthetic files once (outside the timed loop -
// GrepService::findAll() re-walks/re-reads them on every iteration, the
// multi-file analogue of how search_find_all_bench.cpp builds its synthetic
// Document once and re-scans it per iteration) and removes them on
// destruction.
struct ScratchDir {
    std::filesystem::path root;

    ScratchDir()
        : root(std::filesystem::temp_directory_path() / "neomifes_bench_grep_service") {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
        for (int i = 0; i < kFileCount; ++i) {
            writeSyntheticFile(root / (std::string("file_") + std::to_string(i) + ".txt"), i);
        }
    }

    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

}  // namespace

static void BM_GrepService_FindAll_LiteralSparseMatch(benchmark::State& state) {
    const ScratchDir scratch;
    const GrepQuery query{
        .roots = {scratch.root},
        .query = Query{.pattern = u"ERROR", .caseSensitive = true},
    };

    for (auto _ : state) {
        benchmark::DoNotOptimize(GrepService::findAll(query));
    }
    state.SetItemsProcessed(state.iterations() * kFileCount);
}
BENCHMARK(BM_GrepService_FindAll_LiteralSparseMatch)->Unit(benchmark::kMillisecond)->Iterations(5);

static void BM_GrepService_FindAll_NoMatch(benchmark::State& state) {
    const ScratchDir scratch;
    const GrepQuery query{
        .roots = {scratch.root},
        .query = Query{.pattern = u"this pattern never appears anywhere", .caseSensitive = true},
    };

    for (auto _ : state) {
        benchmark::DoNotOptimize(GrepService::findAll(query));
    }
    state.SetItemsProcessed(state.iterations() * kFileCount);
}
BENCHMARK(BM_GrepService_FindAll_NoMatch)->Unit(benchmark::kMillisecond)->Iterations(5);
