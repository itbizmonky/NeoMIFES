#pragma once

// Phase 8c (ADR-017): process-wide plugin sandboxing via a Windows Job
// Object - master_roadmap.md sec.17.1 "レベル2". The ONLY limit enabled is
// JOB_OBJECT_LIMIT_ACTIVE_PROCESS=1: given today's IN-PROCESS plugin
// architecture (LoadLibraryW into the host's own address space, ADR-015),
// a memory/CPU-time limit would cap the ENTIRE host process, not "the
// plugin" - there is no way to meter them separately without process
// separation (ADR-015's rejected "選択肢3"). See ADR-017 for the full
// rejection reasoning (including why JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE is
// also deliberately not set).
//
// NOT a substitute for sec.17.1 "レベル3" (AppContainer) - a malicious
// plugin sharing the host's address space can still do essentially
// anything except spawn a child process. ADR-015/016's "not a security
// boundary" disclaimers still apply beyond this one narrowed gap.

#include "neomifes/plugin/plugin_error.h"

#include <windows.h>

#include <optional>

namespace neomifes::plugin {

// Idempotent, process-wide, exactly-once (C++11 magic-static): the first
// call from any thread/any caller performs CreateJobObjectW +
// SetInformationJobObject + AssignProcessToJobObject exactly once and
// caches the outcome; every later call returns that cached result at
// negligible cost - safe to call repeatedly purely as a status query.
//
// Deliberately NOT called automatically by PluginHost::load() - see
// ADR-017 ("load()へ自動フックしない理由"): this repo's ~40 test files
// share one process (tests/unit/CMakeLists.txt's neomifes_unit_tests.exe),
// and AssignProcessToJobObject is a one-way, permanent operation for the
// life of the process - an unrelated failing-path unit test calling
// load() must not permanently strip that whole test binary's ability to
// spawn child processes. The eventual real caller is src/app/main.cpp
// (still unwired - matches ADR-015/016's "main.cpp untouched" precedent).
//
// Failure is NON-FATAL: plugin loading must still proceed even if this
// fails (safe degradation, matching outline.cpp's empty-SymbolTable
// precedent) - but the caller MUST surface/log the returned error, never
// silently discard it (CLAUDE.md rule 3).
[[nodiscard]] PluginExpected<void> ensureProcessSandboxed() noexcept;

// Diagnostic/test-only re-query of the live job object's own limits via
// QueryInformationJobObject - verifies what the OS actually recorded
// rather than only trusting SetInformationJobObject's boolean return
// (mirrors ADR-015's own empirical-verification precedent for the SEH
// trampoline). std::nullopt if ensureProcessSandboxed() was never called,
// or was called and failed (no job handle is held in that case).
[[nodiscard]] std::optional<JOBOBJECT_BASIC_LIMIT_INFORMATION> queryActiveJobLimits() noexcept;

}  // namespace neomifes::plugin
