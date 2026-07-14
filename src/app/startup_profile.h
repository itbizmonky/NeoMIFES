#pragma once

// Startup profiling — records phase markers during launch and can serialize the
// result to a JSON file for PoC measurements (--measure-startup <file>).

#include <cstdint>
#include <filesystem>
#include <string>

namespace neomifes::app {

struct StartupProfile {
    std::int64_t winMainEnterNs      = 0;
    std::int64_t windowCreatedNs     = 0;
    std::int64_t firstPaintNs        = 0;
    std::int64_t measuredExitNs      = 0;

    std::uint64_t workingSetBytesAtFirstPaint         = 0;
    std::uint64_t privateWorkingSetBytesAtFirstPaint  = 0;

    // Writes a minimal, hand-rolled JSON document. We avoid pulling a JSON
    // library into the Phase 1 boot path — every dependency costs startup time.
    [[nodiscard]] bool writeJson(const std::filesystem::path& out) const;
};

}  // namespace neomifes::app
