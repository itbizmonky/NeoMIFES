#pragma once

// message_dialogs - TaskDialogIndirect-based confirmation/error dialogs
// (WI-02, build_plan.md §5). First TaskDialogIndirect usage in this
// codebase - see message_dialogs.cpp for the Common Controls v6 manifest
// requirement this relies on (embedded via main.cpp's linker pragma).
// COMCTL32, not COM - deliberately kept in a separate file from
// file_dialogs.h so that one never needs <wrl/client.h>/CoInitializeEx and
// this one never needs <shobjidl.h>.
//
// All three functions show genuinely untestable native modal Win32 UI -
// see file_dialogs.h's own note on this; correctness is verified by
// manual/real-app testing.

#include <windows.h>

#include <string_view>

#include "neomifes/document/file_loader.h"
#include "neomifes/document/file_saver.h"

namespace neomifes::app {

enum class UnsavedChangesChoice { Save, DontSave, Cancel };

// The 3-way "unsaved changes" prompt shown before a destructive operation
// (Ctrl+N/Ctrl+O/drag-drop-open/window close) when Document::isDirty().
// `documentName` is shown in the dialog text (the current file's name, or
// an "Untitled" placeholder the caller supplies for a new document).
// TaskDialogIndirect failing outright (should not happen with the v6
// manifest in place, but defensively) returns Cancel - never Save or
// DontSave, so a dialog malfunction can never silently discard unsaved
// work or silently overwrite a file.
[[nodiscard]] UnsavedChangesChoice showUnsavedChangesDialog(HWND owner, std::wstring_view documentName);

// OK-only error dialog for a failed document::saveFile() call.
void showSaveErrorDialog(HWND owner, document::SaveError error);

// OK-only error dialog for a failed Ctrl+O/drag-drop-open document::LoadError.
void showOpenErrorDialog(HWND owner, document::LoadError error);

}  // namespace neomifes::app
