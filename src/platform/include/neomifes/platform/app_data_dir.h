#pragma once

// Resolves this app's per-user roaming AppData directory (Phase 5c5, first
// consumer: core::SearchHistory's persisted search_history.json). Kept in
// src/platform/ alongside file_mapping.h/clipboard.h - the other thin
// Win32-facade headers - rather than inside core::SearchHistory itself, so
// SearchHistory stays Win32-API-free and headlessly testable against an
// arbitrary caller-supplied path (same "inject the path, don't resolve it
// internally" split file_loader.h already uses between FileLoader and
// OriginalBuffer).

#include <windows.h>

#include <filesystem>
#include <optional>

namespace neomifes::platform {

// Resolves "%APPDATA%\NeoMIFES", creating the directory if it doesn't exist
// yet. Returns nullopt if Windows can't resolve the roaming AppData folder
// itself (SHGetKnownFolderPath failure - extremely rare, e.g. a corrupt user
// profile) or the directory can't be created (e.g. permission denied) -
// callers should fall back to an in-memory-only default rather than treating
// this as fatal, since losing search history persistence is not worth
// blocking the editor over.
//
// Not noexcept: std::filesystem::path construction/create_directories()
// allocate; a genuine std::bad_alloc is allowed to propagate rather than
// being converted to std::terminate - same convention as OriginalBuffer::
// view()'s documented rationale (original_buffer.h).
[[nodiscard]] std::optional<std::filesystem::path> resolveAppDataDir();

}  // namespace neomifes::platform
