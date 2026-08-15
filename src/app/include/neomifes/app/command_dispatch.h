#pragma once

// command_dispatch (WI-07 step2, WI-10) - bridges Win32's accelerator-table
// mechanism to this codebase's existing command logic.
//
// buildAcceleratorTable() (command_dispatch.cpp, self-contained) builds the
// HACCEL main.cpp hands to TranslateAcceleratorW in the message loop, from
// a live core::KeyBindings (WI-10 - see keybinding_dispatch.h's
// buildAcceleratorRows(), the pure-logic function this wraps). dispatchCommand()
// (defined in normal_mode_wiring.cpp, NOT command_dispatch.cpp - see that
// function's own comment for why) is the single switch-statement choke
// point every resulting WM_COMMAND is routed through via wireNormalMode()'s
// cfg.onCommand lambda.
//
// Deliberately narrow scope - only Save/SaveAs/Open/New/tab-switch/
// tab-close/Copy/Cut/Paste/Undo/Redo are handled here. Find/Grep/Command
// Palette/Outline/Goto Line/Bookmark/Tag Jump are NOT: those toggle-keys
// interact with per-widget-focused key handling in ways that made them
// unsafe to promote to a GLOBAL accelerator table (see command_dispatch.cpp
// for the concrete conflict this was found to cause) - they remain on
// normal_mode_wiring.cpp's existing, proven handle*Key() dispatch chain,
// unchanged by this step. Copy/Cut/Paste/Undo/Redo are likewise NOT in the
// accelerator table itself (native comctl32 WC_EDIT controls - the overlay
// widgets' own text fields - claim Ctrl+C/X/V/Z themselves; a global
// accelerator entry would intercept before the focused control ever saw the
// keystroke) but their command BODIES still live here, reached via an
// explicit dispatchCommand() call from handleKeyDownEvent() instead of
// duplicating the logic inline. WI-10 does not revisit any of this - WHICH
// CommandIds are HACCEL-eligible (kAcceleratorEligibleCommands,
// keybinding_dispatch.h) remains this same fixed set; only WHICH CHORD
// triggers each one becomes user-configurable.

#include <filesystem>
#include <iterator>
#include <optional>
#include <windows.h>

#include "neomifes/app/keybinding_dispatch.h"
#include "neomifes/app/menu_bar.h"
#include "neomifes/app/workspace.h"
#include "neomifes/core/autosave_index.h"
#include "neomifes/core/key_bindings.h"
#include "neomifes/core/recent_files.h"
#include "neomifes/core/settings.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/ui/command_ids.h"
#include "neomifes/ui/find_bar.h"

namespace neomifes::app {

// WI-11: the 3 refs every autosave-related call site needs together
// (app::performAutoSave()/app::clearAutoSave()'s own parameter list) -
// bundled into one struct (rather than 3 more individual
// CommandDispatchContext fields) since they're always used as a unit.
// autosaveDir/indexPath are optional (nullopt if resolveAppDataDir()
// failed at startup - same graceful-degradation convention settingsPath/
// keyBindingsPath already use), in which case every autosave/clear call
// through this context is a silent no-op (main.cpp only ever reaches this
// state if the whole %APPDATA%\NeoMIFES directory itself is unwritable).
struct AutosaveContext {
    std::optional<std::filesystem::path> autosaveDir;
    core::AutosaveIndex&                  index;
    std::optional<std::filesystem::path>  indexPath;
};

// Built at startup and again whenever keybindings.json is reloaded or a
// preset is switched (main.cpp's `accelTable` local is reassigned with the
// result each time - HandleGuard::operator=(HandleGuard&&) destroys the old
// HACCEL first, so this is a safe, leak-free live swap, see
// normal_mode_wiring.cpp's keybindings.* palette commands). A thin
// CreateAcceleratorTableW wrapper around buildAcceleratorRows(keyBindings)
// (keybinding_dispatch.h) - all the actual row-selection logic (which
// chords, conflict resolution) lives there as pure, unit-testable data/
// logic; this function's only job is the Win32 call itself. nullptr (falsy
// AcceleratorTableHandle) if CreateAcceleratorTableW fails - runMessageLoop()
// treats that as "no accelerators", same non-fatal-degradation convention
// findBar.create()'s own failure gets (see wireNormalMode()'s header
// comment).
[[nodiscard]] platform::AcceleratorTableHandle buildAcceleratorTable(const core::KeyBindings& keyBindings);

// Every reference dispatchCommand() needs. A struct (not 4 individual
// parameters) since wireNormalMode() constructs this once, inside
// cfg.onCommand, and passes it straight through.
struct CommandDispatchContext {
    HWND                    hwnd;
    Workspace&              workspace;
    render::RenderPipeline& renderPipeline;
    ui::FindBar&            findBar;
    // WI-11: recorded into after Save/SaveAs/Open succeed
    // (dispatchSaveCommand()/dispatchOpenCommand()), read from by the
    // "最近使ったファイル" menu.
    core::RecentFiles& recentFiles;
    // WI-11: passed by value (an HMENU pair, cheap to copy) - see
    // wireNormalMode()'s own header comment on why this isn't a reference.
    MenuBarHandles menuHandles;
    AutosaveContext& autosave;
    // WI-11: dispatchSaveCommand() reads settings.createBackupOnSave to pass
    // through to performSave()/document::saveFile()'s keepBackup parameter.
    const core::Settings& settings;
};

// Handles: Save, SaveAs, Open, New, TabNext, TabPrevious, TabClose,
// TabSwitch1..TabSwitch9, Copy, Cut, Paste, Undo, Redo,
// ToggleOverwriteMode. No-op for CommandId::None or any id not in that
// list - see this header's own top comment for which commands are
// deliberately NOT handled here.
void dispatchCommand(ui::CommandId id, const CommandDispatchContext& ctx);

}  // namespace neomifes::app
