#pragma once

// Frame timing profiling — records a per-frame duration series during a
// synthetic scroll and can serialize summary stats + TextLayoutCache
// counters to a JSON file for the --measure-frame PoC measurement
// (Phase 3c, ADR-011). Sibling to StartupProfile rather than an extension of
// it: the fields here (duration series/percentiles) are unrelated to
// StartupProfile's single timestamps, and startup_measure_test.cpp already
// greps that JSON schema by field name - extending it risks a schema
// collision.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace neomifes::render {
struct TextLayoutCacheStats;
}  // namespace neomifes::render

namespace neomifes::app {

struct FrameProfile {
    std::uint32_t frameCount         = 0;
    std::uint64_t syntheticLineCount = 0;  // 0 if --open was used instead

    std::int64_t minFrameNs = 0;
    std::int64_t maxFrameNs = 0;
    std::int64_t avgFrameNs = 0;
    std::int64_t p50FrameNs = 0;
    std::int64_t p95FrameNs = 0;

    std::uint64_t layoutCacheHits   = 0;
    std::uint64_t layoutCacheMisses = 0;

    // Computes summary stats (min/max/avg/p50/p95) from a raw per-frame
    // duration series. Sorts a local copy - the series is small (a few
    // hundred elements for a --measure-frame run), so a plain sort is not a
    // hot path worth optimizing with nth_element.
    [[nodiscard]] static FrameProfile fromDurations(
        std::vector<std::int64_t> frameDurationsNs, std::uint64_t syntheticLineCount,
        const render::TextLayoutCacheStats& cacheStats);

    // Writes a minimal, hand-rolled JSON document (same rationale as
    // StartupProfile::writeJson - no JSON library dependency for a PoC
    // measurement path).
    [[nodiscard]] bool writeJson(const std::filesystem::path& out) const;
};

}  // namespace neomifes::app
