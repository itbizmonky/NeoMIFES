#include "neomifes/app/command_dispatch.h"

#include <gtest/gtest.h>

#include <set>
#include <utility>

// WI-10: verifies buildAcceleratorRows(KeyBindings::forPreset(u"neomifes"))
// (keybinding_dispatch.h) directly - no CreateAcceleratorTableW/
// CopyAcceleratorTableW round-trip needed, same "pure data/logic is
// unit-testable without the Win32 machinery around it" convention the old
// (WI-07, now removed) constexpr kAcceleratorTable followed. dispatchCommand()
// itself is NOT tested here - it needs a live Workspace/RenderPipeline/HWND,
// and normal_mode_wiring.cpp's own ~46 similarly Win32-integrated functions
// have never been unit-tested either (see that file's header comment) -
// correctness there is verified by dogfooding, per this project's
// established convention.

namespace neomifes::app {
namespace {

using neomifes::core::KeyBindings;

TEST(CommandDispatchTest, AcceleratorRowsHaveNoFVirtKeyCollisions) {
    const auto rows = buildAcceleratorRows(KeyBindings::forPreset(u"neomifes"));
    std::set<std::pair<BYTE, WORD>> seen;
    for (const ACCEL& entry : rows) {
        const auto key = std::make_pair(entry.fVirt, entry.key);
        EXPECT_TRUE(seen.insert(key).second)
            << "duplicate (fVirt=" << static_cast<int>(entry.fVirt) << ", key=" << entry.key << ")";
    }
}

// Every accelerator in the neomifes preset is Ctrl+<something> (FVIRTKEY|
// FCONTROL, optionally also FSHIFT) - a sanity check that no entry was
// accidentally left unmodified, which would fire on ordinary typing instead
// of a deliberate shortcut.
TEST(CommandDispatchTest, EveryRowIsVirtualKeyPlusControl) {
    const auto rows = buildAcceleratorRows(KeyBindings::forPreset(u"neomifes"));
    ASSERT_FALSE(rows.empty());
    for (const ACCEL& entry : rows) {
        EXPECT_TRUE((entry.fVirt & FVIRTKEY) != 0);
        EXPECT_TRUE((entry.fVirt & FCONTROL) != 0);
    }
}

// Pins down exactly which commands are reachable via the global accelerator
// table under the neomifes default preset - see keybinding_dispatch.h's top
// comment for why Find/Grep/Command Palette/Outline/Goto Line/Bookmark/Tag
// Jump/Copy/Cut/Paste/Undo/Redo are deliberately absent from this set
// despite dispatchCommand() itself handling some of them (Copy/Cut/Paste/
// Undo/Redo reach dispatchCommand() via an explicit handleKeyDownEvent()
// check instead, never via HACCEL).
TEST(CommandDispatchTest, CoversExactlyTheDocumentedCommandSet) {
    const auto rows = buildAcceleratorRows(KeyBindings::forPreset(u"neomifes"));
    std::set<ui::CommandId> actual;
    for (const ACCEL& entry : rows) {
        actual.insert(static_cast<ui::CommandId>(entry.cmd));
    }
    const std::set<ui::CommandId> expected = {
        ui::CommandId::Save,       ui::CommandId::SaveAs,      ui::CommandId::Open,
        ui::CommandId::New,        ui::CommandId::NewWindow,   ui::CommandId::TabClose,
        ui::CommandId::TabNext,    ui::CommandId::TabPrevious, ui::CommandId::TabSwitch1,
        ui::CommandId::TabSwitch2, ui::CommandId::TabSwitch3,  ui::CommandId::TabSwitch4,
        ui::CommandId::TabSwitch5, ui::CommandId::TabSwitch6,  ui::CommandId::TabSwitch7,
        ui::CommandId::TabSwitch8, ui::CommandId::TabSwitch9,
    };
    EXPECT_EQ(actual, expected);
}

// buildAcceleratorTable() itself (the CreateAcceleratorTableW wrapper) is
// deliberately NOT tested here - it is defined in command_dispatch.cpp,
// which (like normal_mode_wiring.cpp) compiles directly into the NeoMIFES
// executable target, not a library tests/unit/ links (see
// src/app/CMakeLists.txt). Testing buildAcceleratorRows() above is what
// actually matters; CreateAcceleratorTableW succeeding on a well-formed
// ACCEL array is Win32's own contract, not this codebase's logic.

}  // namespace
}  // namespace neomifes::app
