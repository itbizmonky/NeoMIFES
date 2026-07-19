#include <gtest/gtest.h>

#include <vector>

#include "neomifes/ui/command_palette_filter.h"

namespace {

using neomifes::ui::CommandDescriptor;
using neomifes::ui::filterAndRankCommands;

// .action is intentionally left as its default (no-op) - filterAndRankCommands()
// only reads id/title/keybindingLabel, never invokes action. Every field is
// still explicitly listed (.action = nullptr) because clang-cl's
// -Wmissing-designated-field-initializers (ubsan preset) rejects a
// designated-initializer that omits any struct field, unlike MSVC.
std::vector<CommandDescriptor> sampleCommands() {
    return {
        CommandDescriptor{
            .id = u"find.show", .title = u"Find", .keybindingLabel = u"Ctrl+F", .action = nullptr},
        CommandDescriptor{.id              = u"find.replace",
                          .title           = u"Find and Replace",
                          .keybindingLabel = u"Ctrl+H",
                          .action          = nullptr},
        CommandDescriptor{
            .id = u"edit.undo", .title = u"Undo", .keybindingLabel = u"Ctrl+Z", .action = nullptr},
        CommandDescriptor{
            .id = u"edit.redo", .title = u"Redo", .keybindingLabel = u"Ctrl+Y", .action = nullptr},
    };
}

TEST(CommandPaletteFilterTest, EmptyQueryReturnsAllInOriginalOrder) {
    const auto commands = sampleCommands();
    const auto result   = filterAndRankCommands(u"", commands);
    ASSERT_EQ(result.size(), commands.size());
    EXPECT_EQ(result, (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(CommandPaletteFilterTest, NonMatchingQueryReturnsEmpty) {
    const auto commands = sampleCommands();
    EXPECT_TRUE(filterAndRankCommands(u"zzz", commands).empty());
}

TEST(CommandPaletteFilterTest, TiedScoresKeepOriginalOrder) {
    const auto commands = sampleCommands();
    const auto result   = filterAndRankCommands(u"find", commands);
    // "Find" (index 0) and "Find and Replace" (index 1) both match "find"
    // as the identical leading four characters, so they score equally -
    // std::stable_sort must keep them in original list order.
    ASSERT_GE(result.size(), 2U);
    EXPECT_EQ(result[0], 0U);
    EXPECT_EQ(result[1], 1U);
}

TEST(CommandPaletteFilterTest, OnlyMatchingCommandsAreReturned) {
    const auto commands = sampleCommands();
    const auto result   = filterAndRankCommands(u"undo", commands);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result.front(), 2U);
}

TEST(CommandPaletteFilterTest, EmptyCommandListReturnsEmpty) {
    const std::vector<CommandDescriptor> empty;
    EXPECT_TRUE(filterAndRankCommands(u"find", empty).empty());
    EXPECT_TRUE(filterAndRankCommands(u"", empty).empty());
}

}  // namespace
