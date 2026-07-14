#pragma once

// High-resolution monotonic clock backed by QueryPerformanceCounter.
// Used by StartupProfile (Phase 1) and later by Rendering (frame budget accounting).

#include <chrono>
#include <cstdint>

namespace neomifes::platform {

class PerfClock {
public:
    using rep        = std::int64_t;
    using period     = std::nano;
    using duration   = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<PerfClock>;

    static constexpr bool is_steady = true;

    [[nodiscard]] static time_point now() noexcept;

    // Convenience: nanoseconds since a fixed origin (WinMain start).
    // The origin is set by markProcessStart() and must be called once during startup.
    static void   markProcessStart() noexcept;
    [[nodiscard]] static std::int64_t nanosSinceProcessStart() noexcept;
};

}  // namespace neomifes::platform
