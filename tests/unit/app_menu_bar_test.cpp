#include "neomifes/app/menu_bar.h"

#include <gtest/gtest.h>

#include <cwchar>
#include <filesystem>
#include <set>
#include <string>

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

namespace fs = std::filesystem;

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

TEST(MenuBarTest, ViewMenuTogglesOutlineAndJsonTree) {
    ASSERT_EQ(kViewMenuItems.size(), 2U);
    EXPECT_EQ(kViewMenuItems[0].commandId, CommandId::OutlineToggle);
    EXPECT_EQ(kViewMenuItems[1].commandId, CommandId::JsonTreeToggle);
}

TEST(MenuBarTest, ToolsMenuShowsCommandPalette) {
    ASSERT_EQ(kToolsMenuItems.size(), 1U);
    EXPECT_EQ(kToolsMenuItems[0].commandId, CommandId::CommandPaletteShow);
}

TEST(MenuBarTest, HelpMenuShowsAbout) {
    ASSERT_EQ(kHelpMenuItems.size(), 1U);
    EXPECT_EQ(kHelpMenuItems[0].commandId, CommandId::About);
}

// WI-11: buildRecentFileMenuItems() - the pure/testable half of the
// recent-files submenu (menu_bar.cpp's populateRecentFilesSubmenu() is the
// untestable AppendMenuW-calling half, same split this file's own header
// comment documents for buildMenuBar() itself).

TEST(MenuBarTest, BuildRecentFileMenuItemsForAnEmptyListReturnsADisabledPlaceholder) {
    const core::RecentFiles empty;
    const auto              items = buildRecentFileMenuItems(empty);
    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items[0].id, 0);
    EXPECT_FALSE(items[0].label.empty());
}

TEST(MenuBarTest, BuildRecentFileMenuItemsAssignsIdsFromTheBaseInMruOrder) {
    // Backslash-style literals on both the input and expected-output side
    // (not forward slashes) so this assertion doesn't depend on whether
    // fs::path::wstring() normalizes separators - both sides already
    // match verbatim regardless of that implementation detail.
    core::RecentFiles recent;
    recent.record(fs::path(L"C:\\first.txt"));
    recent.record(fs::path(L"C:\\second.txt"));
    recent.record(fs::path(L"C:\\third.txt"));

    const auto items = buildRecentFileMenuItems(recent);
    ASSERT_EQ(items.size(), 3U);
    EXPECT_EQ(items[0].id, kRecentFileIdBase);
    EXPECT_EQ(items[1].id, kRecentFileIdBase + 1);
    EXPECT_EQ(items[2].id, kRecentFileIdBase + 2);
    EXPECT_EQ(items[0].label, L"C:\\third.txt");   // most recent first
    EXPECT_EQ(items[2].label, L"C:\\first.txt");
}

TEST(MenuBarTest, BuildRecentFileMenuItemsCapsAtTheMaxEvenIfRecentFilesHeldMore) {
    core::RecentFiles recent;
    for (int i = 0; i < 30; ++i) {
        recent.record(std::filesystem::path("C:/file" + std::to_string(i) + ".txt"));
    }
    const auto items = buildRecentFileMenuItems(recent);
    EXPECT_LE(items.size(), static_cast<std::size_t>(kMaxRecentFileMenuItems));
}

}  // namespace
}  // namespace neomifes::app
