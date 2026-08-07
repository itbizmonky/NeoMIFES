#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "neomifes/app/workspace.h"

namespace fs = std::filesystem;

namespace {

using neomifes::app::Workspace;

// Same idiom as app_document_open_test.cpp's tempFileWith().
fs::path tempFileWith(const std::string& bytes) {
    fs::path p =
        fs::temp_directory_path() / (std::string("nmfs_workspace_") + std::to_string(std::rand()) + ".txt");
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

TEST(WorkspaceTest, StartsWithOneUntitledActiveSession) {
    Workspace workspace;
    EXPECT_EQ(workspace.sessionCount(), 1U);
    EXPECT_EQ(workspace.activeIndex(), 0U);
    EXPECT_TRUE(workspace.active().isUntitled());
}

TEST(WorkspaceTest, OpenFileAppendsNewSessionAndActivatesIt) {
    Workspace      workspace;
    const fs::path file = tempFileWith("hello");

    const auto index = workspace.openFile(file);

    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 1U);
    EXPECT_EQ(workspace.sessionCount(), 2U);
    EXPECT_EQ(workspace.activeIndex(), 1U);
    EXPECT_EQ(workspace.active().document().toU16String(), u"hello");
    fs::remove(file);
}

TEST(WorkspaceTest, OpenFileReturnsExistingIndexWithoutReloadingIfAlreadyOpen) {
    Workspace      workspace;
    const fs::path file = tempFileWith("original");
    const auto     firstIndex = workspace.openFile(file);
    ASSERT_TRUE(firstIndex.has_value());

    // Activate a different tab, then re-open the same file - should just
    // re-activate the existing tab rather than appending a duplicate.
    ASSERT_TRUE(workspace.openFile(tempFileWith("unrelated")).has_value());
    ASSERT_EQ(workspace.sessionCount(), 3U);

    const auto secondIndex = workspace.openFile(file);

    ASSERT_TRUE(secondIndex.has_value());
    EXPECT_EQ(*secondIndex, *firstIndex);
    EXPECT_EQ(workspace.sessionCount(), 3U);  // no new session appended
    EXPECT_EQ(workspace.activeIndex(), *firstIndex);
    fs::remove(file);
}

TEST(WorkspaceTest, OpenFileDedupsAcrossRelativeAndAbsolutePathSpelling) {
    Workspace      workspace;
    const fs::path file         = tempFileWith("content");
    const auto     absoluteIndex = workspace.openFile(fs::absolute(file));
    ASSERT_TRUE(absoluteIndex.has_value());

    // weakly_canonical() normalizes "a/./b" style spellings to the same
    // identity as the absolute path.
    const auto canonicalIndex = workspace.openFile(fs::weakly_canonical(file));

    ASSERT_TRUE(canonicalIndex.has_value());
    EXPECT_EQ(*canonicalIndex, *absoluteIndex);
    EXPECT_EQ(workspace.sessionCount(), 2U);
    fs::remove(file);
}

TEST(WorkspaceTest, OpenFileReturnsNulloptAndLeavesWorkspaceUntouchedForMissingFile) {
    Workspace      workspace;
    const fs::path missing = fs::temp_directory_path() / "nmfs_workspace_does_not_exist.txt";

    const auto index = workspace.openFile(missing);

    EXPECT_FALSE(index.has_value());
    EXPECT_EQ(workspace.sessionCount(), 1U);
    EXPECT_EQ(workspace.activeIndex(), 0U);
}

TEST(WorkspaceTest, CloseSessionRemovesNonActiveSessionAndKeepsActiveIndexValid) {
    Workspace      workspace;
    const fs::path fileA = tempFileWith("a");
    const fs::path fileB = tempFileWith("b");
    ASSERT_TRUE(workspace.openFile(fileA).has_value());  // index 1
    ASSERT_TRUE(workspace.openFile(fileB).has_value());  // index 2, active
    workspace.activate(0);

    const bool closed = workspace.closeSession(1);  // fileA, not active

    EXPECT_TRUE(closed);
    EXPECT_EQ(workspace.sessionCount(), 2U);
    EXPECT_EQ(workspace.activeIndex(), 0U);
    fs::remove(fileA);
    fs::remove(fileB);
}

TEST(WorkspaceTest, CloseSessionRefusesWhenSessionIsDirty) {
    Workspace workspace;
    ASSERT_TRUE(workspace.openFile(tempFileWith("x")).has_value());
    workspace.active().document().insertText(0, u"edit");
    ASSERT_TRUE(workspace.active().isDirty());

    const bool closed = workspace.closeSession(1);

    EXPECT_FALSE(closed);
    EXPECT_EQ(workspace.sessionCount(), 2U);
}

TEST(WorkspaceTest, CloseSessionRefusesToCloseTheLastRemainingSession) {
    Workspace workspace;
    EXPECT_FALSE(workspace.closeSession(0));
    EXPECT_EQ(workspace.sessionCount(), 1U);
}

TEST(WorkspaceTest, CloseSessionShiftsActiveIndexWhenClosingBeforeIt) {
    Workspace      workspace;
    const fs::path fileA = tempFileWith("a");
    const fs::path fileB = tempFileWith("b");
    ASSERT_TRUE(workspace.openFile(fileA).has_value());  // index 1
    ASSERT_TRUE(workspace.openFile(fileB).has_value());  // index 2, active

    const bool closed = workspace.closeSession(1);  // fileA, before the active index

    EXPECT_TRUE(closed);
    EXPECT_EQ(workspace.activeIndex(), 1U);  // shifted down from 2
    EXPECT_EQ(workspace.active().document().toU16String(), u"b");
    fs::remove(fileA);
    fs::remove(fileB);
}

TEST(WorkspaceTest, ActivateSwitchesActiveSession) {
    Workspace workspace;
    ASSERT_TRUE(workspace.openFile(tempFileWith("x")).has_value());

    workspace.activate(0);

    EXPECT_EQ(workspace.activeIndex(), 0U);
}

TEST(WorkspaceTest, ActivateIgnoresOutOfRangeIndex) {
    Workspace workspace;

    workspace.activate(99);

    EXPECT_EQ(workspace.activeIndex(), 0U);
}

TEST(WorkspaceTest, HasUnsavedChangesFalseInitially) {
    const Workspace workspace;
    EXPECT_FALSE(workspace.hasUnsavedChanges());
}

TEST(WorkspaceTest, HasUnsavedChangesTrueIfAnySessionIsDirty) {
    Workspace workspace;
    ASSERT_TRUE(workspace.openFile(tempFileWith("x")).has_value());
    workspace.sessionAt(0).document().insertText(0, u"edit");

    EXPECT_TRUE(workspace.hasUnsavedChanges());
}

}  // namespace
