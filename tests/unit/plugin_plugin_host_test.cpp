#include "neomifes/plugin/plugin_host.h"

#include <gtest/gtest.h>

namespace neomifes::plugin {
namespace {

TEST(IsApiVersionCompatibleTest, AcceptsExactCurrentVersion) {
    EXPECT_TRUE(isApiVersionCompatible(NEOMIFES_PLUGIN_API_VERSION));
}

TEST(IsApiVersionCompatibleTest, RejectsOlderVersion) {
    EXPECT_FALSE(isApiVersionCompatible(NEOMIFES_PLUGIN_API_VERSION - 1));
}

TEST(IsApiVersionCompatibleTest, RejectsNewerVersion) {
    EXPECT_FALSE(isApiVersionCompatible(NEOMIFES_PLUGIN_API_VERSION + 1));
}

TEST(IsApiVersionCompatibleTest, RejectsZero) {
    EXPECT_FALSE(isApiVersionCompatible(0));
}

TEST(PluginHostTest, DefaultConstructedHostIsNotLoaded) {
    PluginHost host;
    EXPECT_FALSE(host.isLoaded());
    EXPECT_EQ(host.contextUserData(), nullptr);
}

TEST(PluginHostTest, UnloadOnNeverLoadedHostReturnsNotLoadedError) {
    PluginHost host;
    const auto result = host.unload();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, PluginErrorCode::NotLoaded);
}

TEST(PluginHostTest, LoadOfNonexistentPathFailsWithLoadLibraryFailed) {
    PluginHost host;
    const auto result = host.load(L"Z:\\definitely\\does\\not\\exist.dll");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, PluginErrorCode::LoadLibraryFailed);
    EXPECT_FALSE(host.isLoaded());
}

}  // namespace
}  // namespace neomifes::plugin
