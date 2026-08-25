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

// WI-14c: OK-only dialog shown when detectLogPatternRule() (format_detection.h)
// found no built-in log pattern confident enough to match the current
// document's leading lines ("Log: Enable (Auto-Detect)" command). Same
// shape as showSaveErrorDialog() - a single fixed message, no error enum
// (there is only one way this can happen, unlike SaveError's 5 distinct
// failure modes).
void showLogFormatNotDetectedDialog(HWND owner);

// WI-15d: OK-only dialog for "JSON整形" (command palette, json.format) when
// the current document isn't well-formed JSON (jsontree::parseJsonTree()
// returned nullopt) - same shape as showLogFormatNotDetectedDialog() (a
// single fixed message, no parameter).
void showJsonFormatInvalidDialog(HWND owner);

// WI-15d: OK-only success dialog for "JSON検証" (command palette,
// json.validate) when jsontree::validateJson() returned nullopt.
void showJsonValidDialog(HWND owner);

// WI-15d: OK-only error dialog for "JSON検証" when jsontree::validateJson()
// found a problem. `message` is the caller's own jsontree::JsonSyntaxError::
// message text, shown verbatim - the one dialog in this file whose content
// is dynamic, caller-supplied text rather than a fixed or enum-selected
// string (this module deliberately still doesn't depend on
// neomifes::jsontree itself - the caller decides what JsonSyntaxError means
// and passes only the resulting text). The caller is responsible for moving
// the cursor to the error position BEFORE calling this, if it wants to -
// this function only shows the dialog.
void showJsonValidationErrorDialog(HWND owner, std::u16string_view message);

// WI-15e: OK-only dialog for "JSONPathを評価" (command palette,
// json.jsonpath) when the current document isn't well-formed JSON
// (jsontree::parseJsonTree() returned nullopt) - same shape as
// showJsonFormatInvalidDialog(), a separate function rather than reusing
// that one because its message text is Format-specific ("整形できません").
void showJsonPathInvalidJsonDialog(HWND owner);

// WI-15e: OK-only error dialog for "JSONPathを評価" when jsontree::
// parseJsonPath() rejected the user's input. `expression` is the raw text
// the user typed, shown verbatim - same "caller-supplied dynamic text"
// shape as showJsonValidationErrorDialog(), but here the text is the
// invalid expression itself rather than a library-provided error message
// (parseJsonPath() itself carries no error-reason string, only nullopt).
void showJsonPathSyntaxErrorDialog(HWND owner, std::u16string_view expression);

// WI-15e: OK-only dialog for "JSONPathを評価" when jsontree::
// evaluateJsonPath() parsed successfully but matched zero nodes.
void showJsonPathNoMatchDialog(HWND owner);

// WI-15i: OK-only dialog for "XPathを評価" (command palette, xml.xpath) when
// the current document's root couldn't be resolved (xmltree::parseXmlTree()
// returned an XmlNodeKind::Error root - see xml_tree.h's own comment on why
// this, not std::nullopt, is XML's equivalent of "not well-formed"). Same
// shape as showJsonPathInvalidJsonDialog(), a separate function because its
// message text is XML-specific.
void showXPathInvalidXmlDialog(HWND owner);

// WI-15i: OK-only error dialog for "XPathを評価" when xmltree::parseXPath()
// rejected the user's input. Same shape as showJsonPathSyntaxErrorDialog().
void showXPathSyntaxErrorDialog(HWND owner, std::u16string_view expression);

// WI-15i: OK-only dialog for "XPathを評価" when xmltree::evaluateXPath()
// parsed successfully but matched zero nodes. Same shape as
// showJsonPathNoMatchDialog().
void showXPathNoMatchDialog(HWND owner);

// WI-11: Restore/Discard prompt shown once per recoverable autosave found at
// startup (app::scanForRecoverableAutoSaves()). Returns true if the user
// chose to restore. `owner` may be nullptr - this runs at startup, BEFORE
// the main window exists (TaskDialogIndirect tolerates an unowned/
// unparented dialog, same as any other modal MessageBox-family call would).
// TaskDialogIndirect failing outright returns false (don't restore) - the
// opposite fail-safe direction from showUnsavedChangesDialog()'s
// fail-to-Cancel: THERE, silently discarding unsaved work is the risk to
// guard against; HERE, silently resurrecting old content the user may not
// want is the more surprising outcome, so a dialog malfunction defaults to
// leaving the recovered content alone.
[[nodiscard]] bool showCrashRecoveryDialog(HWND owner, std::wstring_view fileName);

}  // namespace neomifes::app
