#include "neomifes/platform/perf_clock.h"

#include <windows.h>

#include <atomic>

namespace neomifes::platform {

namespace {

// QPC frequency is stable for the lifetime of the OS session (guaranteed by Win7+).
// Cache it in a magic-static to keep now() branch-free after first call.
LARGE_INTEGER qpcFrequency() noexcept {
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{};
        ::QueryPerformanceFrequency(&f);  // never fails on Win7+
        return f;
    }();
    return freq;
}

std::atomic<std::int64_t> g_processStartCounter{0};

}  // namespace

PerfClock::time_point PerfClock::now() noexcept {
    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);
    const auto freq = qpcFrequency().QuadPart;
    // ns = counter * 1e9 / freq. Split to avoid 64-bit overflow for very large counters.
    const auto whole = counter.QuadPart / freq;
    const auto frac  = counter.QuadPart % freq;
    const auto ns    = whole * 1'000'000'000LL + (frac * 1'000'000'000LL) / freq;
    return time_point{duration{ns}};
}

void PerfClock::markProcessStart() noexcept {
    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);
    g_processStartCounter.store(counter.QuadPart, std::memory_order_release);
}

std::int64_t PerfClock::nanosSinceProcessStart() noexcept {
    const auto start = g_processStartCounter.load(std::memory_order_acquire);
    if (start == 0) {
        return 0;
    }
    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);
    const auto delta = counter.QuadPart - start;
    const auto freq  = qpcFrequency().QuadPart;
    const auto whole = delta / freq;
    const auto frac  = delta % freq;
    return whole * 1'000'000'000LL + (frac * 1'000'000'000LL) / freq;
}

}  // namespace neomifes::platform
