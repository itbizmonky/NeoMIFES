#include <gtest/gtest.h>

#include <set>
#include <string_view>

#include "neomifes/ui/command_id_name.h"

namespace {

using neomifes::ui::CommandId;
using neomifes::ui::commandIdFromString;
using neomifes::ui::commandIdToString;
using neomifes::ui::kAllRemappableCommandIds;

TEST(CommandIdNameTest, EveryRemappableCommandIdMapsToANonEmptyString) {
    for (const CommandId id : kAllRemappableCommandIds) {
        EXPECT_FALSE(commandIdToString(id).empty()) << "CommandId with no dotted-string mapping found";
    }
}

TEST(CommandIdNameTest, EveryRemappableCommandIdHasAUniqueString) {
    std::set<std::u16string_view> seen;
    for (const CommandId id : kAllRemappableCommandIds) {
        const auto [it, inserted] = seen.insert(commandIdToString(id));
        EXPECT_TRUE(inserted) << "Duplicate dotted-string mapping detected";
    }
    EXPECT_EQ(seen.size(), kAllRemappableCommandIds.size());
}

TEST(CommandIdNameTest, NoneAndAboutAreExcluded) {
    EXPECT_TRUE(commandIdToString(CommandId::None).empty());
    EXPECT_TRUE(commandIdToString(CommandId::About).empty());
}

// WI-21e: menu/palette-only, no keyboard path - see command_ids.h's own
// WordWrapToggle declaration comment for why.
TEST(CommandIdNameTest, ViewToggleCommandsAreExcluded) {
    EXPECT_TRUE(commandIdToString(CommandId::WordWrapToggle).empty());
    EXPECT_TRUE(commandIdToString(CommandId::LineNumbersToggle).empty());
    EXPECT_TRUE(commandIdToString(CommandId::ThemeCycle).empty());
}

TEST(CommandIdNameTest, RoundTripsForEveryRemappableCommandId) {
    for (const CommandId id : kAllRemappableCommandIds) {
        EXPECT_EQ(commandIdFromString(commandIdToString(id)), id);
    }
}

TEST(CommandIdNameTest, UnknownStringMapsToNone) {
    EXPECT_EQ(commandIdFromString(u"totally.unknown.command"), CommandId::None);
    EXPECT_EQ(commandIdFromString(u""), CommandId::None);
}

TEST(CommandIdNameTest, ExactValuesMatchExistingCommandDescriptorIdConvention) {
    // These 6 already exist as literal CommandDescriptor::id values in
    // normal_mode_wiring.cpp's buildCommandRegistry() - the dotted
    // convention this header extends to the other 28 CommandIds must match
    // them exactly, not merely be internally consistent.
    EXPECT_EQ(commandIdToString(CommandId::FindShow), u"find.show");
    EXPECT_EQ(commandIdToString(CommandId::FindReplace), u"find.replace");
    EXPECT_EQ(commandIdToString(CommandId::FindNext), u"find.next");
    EXPECT_EQ(commandIdToString(CommandId::FindPrevious), u"find.previous");
    EXPECT_EQ(commandIdToString(CommandId::Undo), u"edit.undo");
    EXPECT_EQ(commandIdToString(CommandId::Redo), u"edit.redo");
}

}  // namespace
