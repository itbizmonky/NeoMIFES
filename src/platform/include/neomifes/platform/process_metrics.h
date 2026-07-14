#pragma once

// Windows process memory metrics.
// Used for the Phase 1 memory PoC (20MB target on first-window-shown, per user decision).

#include <cstdint>

namespace neomifes::platform {

struct MemorySnapshot {
    // All values in bytes. Reported by GetProcessMemoryInfo (PSAPI).
    std::uint64_t workingSetBytes            = 0;  // Resident set (physical RAM in use)
    std::uint64_t peakWorkingSetBytes        = 0;
    std::uint64_t privateBytes               = 0;  // Committed private memory
    std::uint64_t privateWorkingSetBytes     = 0;  // Working set excluding shared pages
    std::uint64_t pagefileBytes              = 0;
    std::uint64_t peakPagefileBytes          = 0;
};

// Returns a snapshot of the current process memory usage.
[[nodiscard]] MemorySnapshot currentProcessMemory() noexcept;

}  // namespace neomifes::platform
