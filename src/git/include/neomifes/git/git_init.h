#pragma once

// initializeLibgit2()/shutdownLibgit2() - process-wide libgit2 runtime
// lifecycle (WI-17a, Phase 11.1 core, ADR-022). libgit2 requires exactly one
// git_libgit2_init()/git_libgit2_shutdown() pair per process (its own
// documented contract - internally reference-counted, so nested init/
// shutdown calls are tolerated, but this project standardizes on calling it
// once each, from main.cpp, the same way this codebase already treats
// COM/Direct2D/DirectWrite device lifetime as a single owned resource rather
// than something every consumer initializes independently). Deliberately
// the ONLY public symbol in neomifes::git that touches libgit2's own runtime
// state directly - GitRepository (a later WI-17a commit) assumes this has
// already run by the time any of its methods are called, the same way this
// codebase's render:: types assume Direct2D/DirectWrite factories already
// exist rather than lazily initializing them per call.

namespace neomifes::git {

// Returns true on success. false means every other neomifes::git API for
// this process must be treated as unusable - callers should fail closed
// (e.g. skip wiring Git features into the UI) rather than call into
// GitRepository anyway.
[[nodiscard]] bool initializeLibgit2() noexcept;

// No-op if initializeLibgit2() was never called or already returned false -
// safe to call unconditionally from a shutdown path.
void shutdownLibgit2() noexcept;

}  // namespace neomifes::git
