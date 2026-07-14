#include "neomifes/platform/process_metrics.h"

#include <windows.h>
#include <psapi.h>

namespace neomifes::platform {

MemorySnapshot currentProcessMemory() noexcept {
    MemorySnapshot snapshot{};

    PROCESS_MEMORY_COUNTERS_EX2 pmc{};
    pmc.cb = sizeof(pmc);
    // PROCESS_MEMORY_COUNTERS_EX2 was added in Windows 10 2004 (build 19041) and
    // exposes PrivateWorkingSetSize directly. GetProcessMemoryInfo populates as
    // many fields as the requested cb permits, so this call is safe on earlier
    // headers as long as PSAPI_VERSION >= 2.
    if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                               reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                               sizeof(pmc))) {
        snapshot.workingSetBytes         = pmc.WorkingSetSize;
        snapshot.peakWorkingSetBytes     = pmc.PeakWorkingSetSize;
        snapshot.privateBytes            = pmc.PrivateUsage;
        snapshot.privateWorkingSetBytes  = pmc.PrivateWorkingSetSize;
        snapshot.pagefileBytes           = pmc.PagefileUsage;
        snapshot.peakPagefileBytes       = pmc.PeakPagefileUsage;
    }
    return snapshot;
}

}  // namespace neomifes::platform
