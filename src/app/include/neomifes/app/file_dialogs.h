#pragma once

// file_dialogs - IFileOpenDialog/IFileSaveDialog wrappers (WI-02,
// build_plan.md §5). First use of CoCreateInstance() in this codebase -
// unlike the existing D2D/DXGI/D3D11 COM usage (ADR-008), which is all
// factory-function-based and never needed CoInitializeEx, these two
// classes are activated via class-object COM activation and DO require it
// (see file_dialogs.cpp's ComInitGuard). Kept in a dedicated header/source
// (not neomifes::app_input) because that library is deliberately
// Win32/COM-independent so tests/unit/ can link it headlessly - see its
// own file header comment. This module has no such requirement and is
// added directly to the NeoMIFES executable's own sources instead.
//
// Both functions are genuinely untestable (native modal Win32/COM UI) -
// build_plan.md's own WI-02 scope note acknowledges this; correctness is
// verified by manual/real-app testing, not automated tests.

#include <windows.h>

#include <filesystem>
#include <optional>

namespace neomifes::app {

// Shows IFileOpenDialog (single file selection, "All Files (*.*)" only -
// NeoMIFES has no fixed notion of "supported" extensions, matching
// detectLanguage()'s own deliberately generous multi-extension list
// philosophy rather than restricting what can be opened). Returns nullopt
// if the user cancelled, or if dialog creation/CoInitializeEx/the COM call
// itself failed - both are treated identically here (nothing to proceed
// with either way); see message_dialogs.h's showOpenErrorDialog() for the
// separate, later failure mode of the FILE LOAD itself failing after a
// path was actually chosen.
[[nodiscard]] std::optional<std::filesystem::path> showOpenFileDialog(HWND owner);

// Shows IFileSaveDialog. `suggestedPath`, if set, pre-fills the dialog's
// starting folder and filename (Ctrl+Shift+S on an already-named
// document); if unset, the dialog opens to the OS default location with no
// filename (Ctrl+Shift+S on an untitled document). Returns nullopt on
// cancel or failure, same convention as showOpenFileDialog().
[[nodiscard]] std::optional<std::filesystem::path> showSaveFileDialog(
    HWND owner, const std::optional<std::filesystem::path>& suggestedPath);

}  // namespace neomifes::app
