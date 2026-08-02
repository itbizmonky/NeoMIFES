#include "neomifes/plugin/plugin_sandbox.h"

#include "neomifes/platform/handle_guard.h"

namespace neomifes::plugin {

namespace {

// platform::KernelHandle (HandleGuard<HANDLE, CloseHandleDeleter, nullptr>)
// is directly reusable with zero new deleter code: CreateJobObjectW
// returns NULL (not INVALID_HANDLE_VALUE) on failure, closed via plain
// CloseHandle - exactly KernelHandle's contract.
struct SandboxState {
    PluginExpected<void>   status;
    platform::KernelHandle jobHandle;  // kept alive for the process's
        // whole lifetime, purely so queryActiveJobLimits() can keep
        // re-querying it later - NOT required for the limit itself to
        // stay enforced (the kernel tracks process<->job association
        // independent of any handle we hold).
};

// Every failure path here reports the same PluginErrorCode (see
// plugin_error.h's comment on SandboxSetupFailed collapsing all three
// possible failing calls into one code) with no live job handle.
[[nodiscard]] SandboxState sandboxSetupFailure() noexcept {
    return SandboxState{.status = std::unexpected(PluginError{.code = PluginErrorCode::SandboxSetupFailed,
                                                               .win32Error = ::GetLastError()}),
                        .jobHandle = platform::KernelHandle{}};
}

SandboxState setUpSandbox() noexcept {
    platform::KernelHandle job(::CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        return sandboxSetupFailure();
    }

    JOBOBJECT_BASIC_LIMIT_INFORMATION limits{};
    limits.LimitFlags         = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.ActiveProcessLimit = 1;
    // Deliberately NOT JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE - see ADR-017:
    // this process self-assigns into its own job (no separate controller
    // process holds the handle, the classic use case for that flag), so
    // enabling it would only add a self-termination risk if this handle
    // were ever closed early, for no offsetting benefit here.
    if (!::SetInformationJobObject(job.get(), JobObjectBasicLimitInformation, &limits,
                                    sizeof(limits))) {
        return sandboxSetupFailure();
    }

    if (!::AssignProcessToJobObject(job.get(), ::GetCurrentProcess())) {
        // Real possibility: this process was already placed in a
        // non-nesting job by its environment (some CI/container/terminal
        // wrappers do this). Non-fatal - the caller decides what to do.
        return sandboxSetupFailure();
    }

    return SandboxState{.status = PluginExpected<void>{}, .jobHandle = std::move(job)};
}

// Single shared magic-static behind both public entry points, so
// ensureProcessSandboxed()/queryActiveJobLimits() never trigger a second,
// redundant CreateJobObjectW/AssignProcessToJobObject sequence.
const SandboxState& sandboxState() noexcept {
    static const SandboxState state = setUpSandbox();
    return state;
}

}  // namespace

PluginExpected<void> ensureProcessSandboxed() noexcept {
    return sandboxState().status;
}

std::optional<JOBOBJECT_BASIC_LIMIT_INFORMATION> queryActiveJobLimits() noexcept {
    const SandboxState& state = sandboxState();
    if (!state.jobHandle) {
        return std::nullopt;
    }
    JOBOBJECT_BASIC_LIMIT_INFORMATION limits{};
    if (!::QueryInformationJobObject(state.jobHandle.get(), JobObjectBasicLimitInformation, &limits,
                                      sizeof(limits), nullptr)) {
        return std::nullopt;
    }
    return limits;
}

}  // namespace neomifes::plugin
