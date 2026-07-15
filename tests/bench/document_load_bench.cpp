// File-load benchmarks for the mmap + Lazy Decode OriginalBuffer (Phase 2b3).
// Targets from docs/issues/lazy_decode_mmap.md: 1GiB open <= 2s, Working Set
// increment <= 30MB on open (i.e. opening should NOT materialize the whole
// file as UTF-16 up front).
//
// BM_LoadFile_100MB always runs (CI-sized: fast enough for the bench smoke
// step). BM_LoadFile_1GB is opt-in via NEOMIFES_BENCH_1GB=1, since a 1GiB
// write+read on every CI run would be wasted cost for a target that's a
// local-manual verification per the issue doc, not a CI gate.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/platform/process_metrics.h"

namespace {

// Writes `totalBytes` of repeating pseudo-text to `path` in ~1MiB chunks (a
// single materialized string of the full size would itself double the memory
// this benchmark is trying to characterize). Not a production code path, so a
// thrown exception on I/O failure (rather than std::expected plumbing) is
// acceptable here per CLAUDE.md's "recoverable errors only" scoping.
void generateMockFile(const std::filesystem::path& path, std::uint64_t totalBytes) {
    constexpr std::string_view kLine =
        "The quick brown fox jumps over the lazy dog. 0123456789\n";
    constexpr std::size_t kChunkBytes = 1024 * 1024;

    std::string chunk;
    chunk.reserve(kChunkBytes + kLine.size());
    while (chunk.size() < kChunkBytes) {
        chunk.append(kLine);
    }
    chunk.resize(kChunkBytes);

    std::FILE* fp = nullptr;
    if (::_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == nullptr) {
        throw std::runtime_error("generateMockFile: failed to open output file");
    }

    std::uint64_t written = 0;
    while (written + kChunkBytes <= totalBytes) {
        std::fwrite(chunk.data(), 1, chunk.size(), fp);
        written += kChunkBytes;
    }
    if (written < totalBytes) {
        const auto remain = static_cast<std::size_t>(totalBytes - written);
        std::fwrite(chunk.data(), 1, remain, fp);
    }
    std::fclose(fp);
}

// Loads `path` once and reports the resulting Working Set delta as custom
// counters. Deliberately NOT inside the timed loop below: once the OS page
// cache and this OriginalBuffer's decode cache are warm, a second load of the
// same file no longer reflects the cold-open memory cost we actually care
// about.
//
// Two numbers are reported because they answer different questions:
//   - working_set_delta_MiB (total): necessarily tracks file size for ANY
//     loading strategy, mmap'd or not - scanUtf8 has to touch every byte once
//     to validate UTF-8 and build the checkpoint index, and touched mmap
//     pages count toward the process's total working set even though they're
//     shared/reclaimable OS file-cache pages, not private allocations.
//   - private_working_set_delta_MiB: excludes those shared mapped pages, so
//     it reflects what Lazy Decode actually promises - opening a file does
//     NOT allocate a private UTF-16 copy of the whole thing up front. This is
//     the number the <=30MB target in lazy_decode_mmap.md is meaningful
//     against; the total figure would fail that target for ANY implementation
//     that reads a 100MB+ file, so it's reported for context, not as the
//     pass/fail metric.
void reportWorkingSetDelta(benchmark::State& state, const std::filesystem::path& path,
                            std::uint64_t maxBytes) {
    const auto before = neomifes::platform::currentProcessMemory();
    auto loaded = neomifes::document::loadUtf8File(path, maxBytes);
    auto& result = std::get<neomifes::document::LoadResult>(loaded);
    benchmark::DoNotOptimize(result.document->length());
    const auto after = neomifes::platform::currentProcessMemory();
    state.counters["working_set_delta_MiB"] =
        static_cast<double>(after.workingSetBytes - before.workingSetBytes) / (1024.0 * 1024.0);
    state.counters["private_working_set_delta_MiB"] =
        static_cast<double>(after.privateWorkingSetBytes - before.privateWorkingSetBytes)
        / (1024.0 * 1024.0);
}

void loadFileBenchBody(benchmark::State& state, std::uint64_t fileBytes,
                        const std::filesystem::path& path) {
    constexpr std::uint64_t kMaxBytes = 2ull * 1024 * 1024 * 1024;  // headroom past loadUtf8File's 512MiB default cap
    generateMockFile(path, fileBytes);

    reportWorkingSetDelta(state, path, kMaxBytes);

    for (auto _ : state) {
        auto loaded = neomifes::document::loadUtf8File(path, kMaxBytes);
        auto& result = std::get<neomifes::document::LoadResult>(loaded);
        benchmark::DoNotOptimize(result.document->length());
    }

    state.counters["file_MiB"] = static_cast<double>(fileBytes) / (1024.0 * 1024.0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}  // namespace

static void BM_LoadFile_100MB(benchmark::State& state) {
    loadFileBenchBody(state, 100ull * 1024 * 1024,
                       std::filesystem::temp_directory_path() / "neomifes_bench_load_100mb.txt");
}
BENCHMARK(BM_LoadFile_100MB)->Unit(benchmark::kMillisecond)->Iterations(3);

static void BM_LoadFile_1GB(benchmark::State& state) {
    loadFileBenchBody(state, 1024ull * 1024 * 1024,
                       std::filesystem::temp_directory_path() / "neomifes_bench_load_1gb.txt");
}

// Opt-in registration: RegisterBenchmark (rather than the BENCHMARK macro,
// which always registers) lets us skip generating/reading a 1GiB file on
// every CI run. This static-init bool is const after initialization and
// touched nowhere else - a one-shot registration gate, not the kind of
// mutable global state CLAUDE.md's rule against global state targets.
const bool kLoadFile1GBRegistered = [] {
    const char* flag = std::getenv("NEOMIFES_BENCH_1GB");
    if (flag != nullptr && std::string_view(flag) == "1") {
        benchmark::RegisterBenchmark("BM_LoadFile_1GB", BM_LoadFile_1GB)
            ->Unit(benchmark::kMillisecond)
            ->Iterations(2);
    }
    return true;
}();
