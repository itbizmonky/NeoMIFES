#include "frame_profile.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

#include "neomifes/render/text_layout_cache.h"

namespace neomifes::app {

FrameProfile FrameProfile::fromDurations(std::vector<std::int64_t> frameDurationsNs,
                                         std::uint64_t syntheticLineCount,
                                         const render::TextLayoutCacheStats& cacheStats) {
    FrameProfile profile;
    profile.frameCount         = static_cast<std::uint32_t>(frameDurationsNs.size());
    profile.syntheticLineCount = syntheticLineCount;
    profile.layoutCacheHits    = cacheStats.hits;
    profile.layoutCacheMisses  = cacheStats.misses;

    if (frameDurationsNs.empty()) {
        return profile;
    }

    std::ranges::sort(frameDurationsNs);
    const auto n = frameDurationsNs.size();

    profile.minFrameNs = frameDurationsNs.front();
    profile.maxFrameNs = frameDurationsNs.back();

    const std::int64_t sum =
        std::accumulate(frameDurationsNs.begin(), frameDurationsNs.end(), std::int64_t{0});
    profile.avgFrameNs = sum / static_cast<std::int64_t>(n);

    // Nearest-rank percentile: fine for PoC reporting, not a statistics library.
    const auto p50Index = static_cast<std::size_t>(0.50 * static_cast<double>(n - 1));
    const auto p95Index = static_cast<std::size_t>(0.95 * static_cast<double>(n - 1));
    profile.p50FrameNs  = frameDurationsNs[p50Index];
    profile.p95FrameNs  = frameDurationsNs[p95Index];

    return profile;
}

bool FrameProfile::writeJson(const std::filesystem::path& out) const {
    std::FILE* fp = nullptr;
    if (::_wfopen_s(&fp, out.c_str(), L"wb") != 0 || fp == nullptr) {
        return false;
    }
    // fprintf/fclose return values cast to void - same rationale as
    // StartupProfile::writeJson (a partial write surfaces downstream as an
    // integration-test JSON-parse failure).
    (void)std::fprintf(fp,
        "{\n"
        "  \"frameCount\": %u,\n"
        "  \"syntheticLineCount\": %llu,\n"
        "  \"minFrameNs\": %lld,\n"
        "  \"maxFrameNs\": %lld,\n"
        "  \"avgFrameNs\": %lld,\n"
        "  \"p50FrameNs\": %lld,\n"
        "  \"p95FrameNs\": %lld,\n"
        "  \"layoutCacheHits\": %llu,\n"
        "  \"layoutCacheMisses\": %llu\n"
        "}\n",
        frameCount,
        static_cast<unsigned long long>(syntheticLineCount),
        static_cast<long long>(minFrameNs),
        static_cast<long long>(maxFrameNs),
        static_cast<long long>(avgFrameNs),
        static_cast<long long>(p50FrameNs),
        static_cast<long long>(p95FrameNs),
        static_cast<unsigned long long>(layoutCacheHits),
        static_cast<unsigned long long>(layoutCacheMisses));
    (void)std::fclose(fp);
    return true;
}

}  // namespace neomifes::app
