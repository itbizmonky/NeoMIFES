#include "neomifes/ui/plugin_command_registry.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace neomifes::ui {
namespace {

CommandDescriptor makeDescriptor(std::u16string_view id, std::u16string_view title) {
    return CommandDescriptor{
        .id              = std::u16string(id),
        .title           = std::u16string(title),
        .keybindingLabel = u"",
        .commandId       = CommandId::None,
        .action          = []() {},
    };
}

TEST(PluginCommandRegistryTest, InitiallyEmpty) {
    const PluginCommandRegistry registry;
    EXPECT_TRUE(registry.commands().empty());
}

TEST(PluginCommandRegistryTest, RegisterCommandAppendsToCommands) {
    PluginCommandRegistry registry;
    registry.registerCommand(makeDescriptor(u"sample.greet", u"Sample Greeting"));
    ASSERT_EQ(registry.commands().size(), 1U);
    EXPECT_EQ(registry.commands()[0].id, u"sample.greet");
    EXPECT_EQ(registry.commands()[0].title, u"Sample Greeting");
}

TEST(PluginCommandRegistryTest, UnregisterCommandRemovesMatchingId) {
    PluginCommandRegistry registry;
    registry.registerCommand(makeDescriptor(u"sample.greet", u"Sample Greeting"));
    registry.registerCommand(makeDescriptor(u"sample.crash", u"Sample Crash"));
    registry.unregisterCommand(u"sample.greet");
    ASSERT_EQ(registry.commands().size(), 1U);
    EXPECT_EQ(registry.commands()[0].id, u"sample.crash");
}

TEST(PluginCommandRegistryTest, UnregisterCommandWithUnknownIdIsANoOp) {
    PluginCommandRegistry registry;
    registry.registerCommand(makeDescriptor(u"sample.greet", u"Sample Greeting"));
    registry.unregisterCommand(u"sample.unknown");
    EXPECT_EQ(registry.commands().size(), 1U);
}

TEST(PluginCommandRegistryTest, RegisterCommandAllowsDuplicateIds) {
    PluginCommandRegistry registry;
    registry.registerCommand(makeDescriptor(u"sample.greet", u"First"));
    registry.registerCommand(makeDescriptor(u"sample.greet", u"Second"));
    ASSERT_EQ(registry.commands().size(), 2U);
    EXPECT_EQ(registry.commands()[0].title, u"First");
    EXPECT_EQ(registry.commands()[1].title, u"Second");
}

TEST(PluginCommandRegistryTest, UnregisterCommandRemovesAllMatchingIds) {
    PluginCommandRegistry registry;
    registry.registerCommand(makeDescriptor(u"sample.greet", u"First"));
    registry.registerCommand(makeDescriptor(u"sample.greet", u"Second"));
    registry.unregisterCommand(u"sample.greet");
    EXPECT_TRUE(registry.commands().empty());
}

}  // namespace
}  // namespace neomifes::ui
