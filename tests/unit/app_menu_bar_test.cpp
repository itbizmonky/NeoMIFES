#include "neomifes/app/menu_bar.h"

#include <gtest/gtest.h>

#include <cwchar>
#include <set>

// WI-07 step3: verifies menu_bar.h's item-spec arrays directly (no
// CreateMenu/AppendMenuW round-trip needed - see that header's own comment
// on why the raw data is exposed for exactly this). buildMenuBar() itself is
// NOT tested here - it is defined in menu_bar.cpp, which (like
// command_dispatch.cpp) compiles directly into the NeoMIFES executable
// target, not a library tests/unit links - see
// app_command_dispatch_test.cpp's own header comment for the identical
// reasoning about buildAcceleratorTable().

namespace neomifes::app {
namespace {

using neomifes::ui::CommandId;

template <std::size_t N>
void collectIds(const std::array<MenuItemSpec, N>& items, std::set<CommandId>& seen) {
    for (const MenuItemSpec& item : items) {
        EXPECT_NE(item.commandId, CommandId::None) << "menu item with CommandId::None found";
        EXPECT_NE(item.label, nullptr);
        EXPECT_GT(std::wcslen(item.label), 0U) << "menu item has an empty label";
        EXPECT_TRUE(seen.insert(item.commandId).second)
            << "CommandId " << static_cast<int>(item.commandId) << " appears in more than one menu";
    }
}

TEST(MenuBarTest, NoMenuItemUsesCommandIdNoneOrAnEmptyLabel) {
    std::set<CommandId> seen;
    collectIds(kFileMenuItems, seen);
    collectIds(kEditMenuItems, seen);
    collectIds(kSearchMenuItems, seen);
    collectIds(kViewMenuItems, seen);
    collectIds(kToolsMenuItems, seen);
    collectIds(kHelpMenuItems, seen);
}

TEST(MenuBarTest, NoCommandIdAppearsInMoreThanOneMenu) {
    // collectIds() above already fails on a duplicate insert - this test
    // documents that guarantee as its own explicit assertion, separate from
    // the label/None checks, so a future regression here is unambiguous.
    std::set<CommandId> seen;
    collectIds(kFileMenuItems, seen);
    collectIds(kEditMenuItems, seen);
    collectIds(kSearchMenuItems, seen);
    collectIds(kViewMenuItems, seen);
    collectIds(kToolsMenuItems, seen);
    collectIds(kHelpMenuItems, seen);
    const std::size_t total = kFileMenuItems.size() + kEditMenuItems.size() + kSearchMenuItems.size() +
                              kViewMenuItems.size() + kToolsMenuItems.size() + kHelpMenuItems.size();
    EXPECT_EQ(seen.size(), total);
}

TEST(MenuBarTest, FileMenuOpensSavesAndCreatesInTheDocumentedOrder) {
    ASSERT_EQ(kFileMenuItems.size(), 4U);
    EXPECT_EQ(kFileMenuItems[0].commandId, CommandId::Open);
    EXPECT_EQ(kFileMenuItems[1].commandId, CommandId::Save);
    EXPECT_EQ(kFileMenuItems[2].commandId, CommandId::SaveAs);
    EXPECT_EQ(kFileMenuItems[3].commandId, CommandId::New);
}

TEST(MenuBarTest, EditMenuCoversUndoRedoAndClipboard) {
    ASSERT_EQ(kEditMenuItems.size(), 5U);
    EXPECT_EQ(kEditMenuItems[0].commandId, CommandId::Undo);
    EXPECT_EQ(kEditMenuItems[1].commandId, CommandId::Redo);
    EXPECT_EQ(kEditMenuItems[2].commandId, CommandId::Cut);
    EXPECT_EQ(kEditMenuItems[3].commandId, CommandId::Copy);
    EXPECT_EQ(kEditMenuItems[4].commandId, CommandId::Paste);
}

TEST(MenuBarTest, SearchMenuCoversFindReplaceAndGrep) {
    ASSERT_EQ(kSearchMenuItems.size(), 6U);
    EXPECT_EQ(kSearchMenuItems[0].commandId, CommandId::FindShow);
    EXPECT_EQ(kSearchMenuItems[1].commandId, CommandId::FindReplace);
    EXPECT_EQ(kSearchMenuItems[4].commandId, CommandId::GrepShow);
    EXPECT_EQ(kSearchMenuItems[5].commandId, CommandId::GotoLineShow);
}

TEST(MenuBarTest, ViewMenuTogglesOutline) {
    ASSERT_EQ(kViewMenuItems.size(), 1U);
    EXPECT_EQ(kViewMenuItems[0].commandId, CommandId::OutlineToggle);
}

TEST(MenuBarTest, ToolsMenuShowsCommandPalette) {
    ASSERT_EQ(kToolsMenuItems.size(), 1U);
    EXPECT_EQ(kToolsMenuItems[0].commandId, CommandId::CommandPaletteShow);
}

TEST(MenuBarTest, HelpMenuShowsAbout) {
    ASSERT_EQ(kHelpMenuItems.size(), 1U);
    EXPECT_EQ(kHelpMenuItems[0].commandId, CommandId::About);
}

}  // namespace
}  // namespace neomifes::app
