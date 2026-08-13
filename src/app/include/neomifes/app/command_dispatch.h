#pragma once

// command_dispatch (WI-07 step2) - bridges Win32's accelerator-table
// mechanism to this codebase's existing command logic.
//
// buildAcceleratorTable() (command_dispatch.cpp, self-contained) builds the
// HACCEL main.cpp hands to TranslateAcceleratorW in the message loop.
// dispatchCommand() (defined in normal_mode_wiring.cpp, NOT
// command_dispatch.cpp - see that function's own comment for why) is the
// single switch-statement choke point every resulting WM_COMMAND is routed
// through via wireNormalMode()'s cfg.onCommand lambda.
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
// duplicating the logic inline.

#include <iterator>
#include <windows.h>

#include "neomifes/app/workspace.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/ui/command_ids.h"
#include "neomifes/ui/find_bar.h"

namespace neomifes::app {

// The raw ACCEL source table buildAcceleratorTable() below wraps via
// CreateAcceleratorTableW. Exposed here (header-only constexpr, not just
// inside command_dispatch.cpp) so tests/unit/app_command_dispatch_test.cpp
// can verify it directly (no (fVirt,key) collisions, expected command
// coverage) without a Win32 round-trip through CreateAcceleratorTableW/
// CopyAcceleratorTableW, and without needing command_dispatch.cpp linked
// into the test binary - same "expose the pure data/logic via a header so
// it's unit-testable without the Win32 machinery around it" convention
// tab_index_math.h/viewport_math.h already follow. See command_dispatch.cpp
// for why membership is deliberately narrow.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays)
inline constexpr ACCEL kAcceleratorTable[] = {
    {FVIRTKEY | FCONTROL, 'S', static_cast<WORD>(ui::CommandId::Save)},
    {FVIRTKEY | FCONTROL | FSHIFT, 'S', static_cast<WORD>(ui::CommandId::SaveAs)},
    {FVIRTKEY | FCONTROL, 'O', static_cast<WORD>(ui::CommandId::Open)},
    {FVIRTKEY | FCONTROL, 'N', static_cast<WORD>(ui::CommandId::New)},
    {FVIRTKEY | FCONTROL, 'W', static_cast<WORD>(ui::CommandId::TabClose)},
    // Ctrl+Tab/Ctrl+Shift+Tab (primary bindings) plus Ctrl+PgDn/Ctrl+PgUp
    // (alternate bindings preserved from the pre-HACCEL if-chain, see
    // normal_mode_wiring.cpp's former handleTabSwitchKey()) - two rows
    // mapping to the same CommandId is ordinary Win32 usage, not a
    // collision (a collision is two rows sharing the same (fVirt,key)).
    {FVIRTKEY | FCONTROL, VK_TAB, static_cast<WORD>(ui::CommandId::TabNext)},
    {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, static_cast<WORD>(ui::CommandId::TabPrevious)},
    {FVIRTKEY | FCONTROL, VK_NEXT, static_cast<WORD>(ui::CommandId::TabNext)},
    {FVIRTKEY | FCONTROL, VK_PRIOR, static_cast<WORD>(ui::CommandId::TabPrevious)},
    {FVIRTKEY | FCONTROL, '1', static_cast<WORD>(ui::CommandId::TabSwitch1)},
    {FVIRTKEY | FCONTROL, '2', static_cast<WORD>(ui::CommandId::TabSwitch2)},
    {FVIRTKEY | FCONTROL, '3', static_cast<WORD>(ui::CommandId::TabSwitch3)},
    {FVIRTKEY | FCONTROL, '4', static_cast<WORD>(ui::CommandId::TabSwitch4)},
    {FVIRTKEY | FCONTROL, '5', static_cast<WORD>(ui::CommandId::TabSwitch5)},
    {FVIRTKEY | FCONTROL, '6', static_cast<WORD>(ui::CommandId::TabSwitch6)},
    {FVIRTKEY | FCONTROL, '7', static_cast<WORD>(ui::CommandId::TabSwitch7)},
    {FVIRTKEY | FCONTROL, '8', static_cast<WORD>(ui::CommandId::TabSwitch8)},
    {FVIRTKEY | FCONTROL, '9', static_cast<WORD>(ui::CommandId::TabSwitch9)},
};

// Built once by wireNormalMode() and handed to main.cpp's runMessageLoop()
// for the window's lifetime. nullptr (falsy AcceleratorTableHandle) if
// CreateAcceleratorTableW fails - runMessageLoop() treats that as "no
// accelerators", same non-fatal-degradation convention findBar.create()'s
// own failure gets (see wireNormalMode()'s header comment).
[[nodiscard]] platform::AcceleratorTableHandle buildAcceleratorTable();

// Every reference dispatchCommand() needs. A struct (not 4 individual
// parameters) since wireNormalMode() constructs this once, inside
// cfg.onCommand, and passes it straight through.
struct CommandDispatchContext {
    HWND                    hwnd;
    Workspace&              workspace;
    render::RenderPipeline& renderPipeline;
    ui::FindBar&            findBar;
};

// Handles: Save, SaveAs, Open, New, TabNext, TabPrevious, TabClose,
// TabSwitch1..TabSwitch9, Copy, Cut, Paste, Undo, Redo. No-op for
// CommandId::None or any id not in that list - see this header's own
// top comment for which commands are deliberately NOT handled here.
void dispatchCommand(ui::CommandId id, const CommandDispatchContext& ctx);

}  // namespace neomifes::app
