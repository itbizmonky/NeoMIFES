#pragma once

// Win32 clipboard access, wrapping CF_UNICODETEXT read/write behind a small
// optional-based API (Phase 4b6c). Kept in src/platform/ alongside
// handle_guard.h/file_mapping.h - the other thin RAII/Win32-facade headers -
// rather than in src/app/editor_input.cpp, which is deliberately Win32-API-
// free so it stays headlessly testable (see editor_input.h's file header).

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace neomifes::platform {

// Replaces the clipboard's CF_UNICODETEXT contents with `text`. `owner` is
// passed to OpenClipboard (conventionally the window requesting the
// operation). Returns false if the clipboard could not be opened or written
// to (e.g. another process holds it) - callers should treat this as "the
// copy silently didn't happen" rather than a fatal error.
[[nodiscard]] bool setClipboardText(HWND owner, std::u16string_view text) noexcept;

// Reads the clipboard's CF_UNICODETEXT contents, or nullopt if the
// clipboard couldn't be opened or holds no text data.
[[nodiscard]] std::optional<std::u16string> getClipboardText(HWND owner);

}  // namespace neomifes::platform
