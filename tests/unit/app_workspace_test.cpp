#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "neomifes/app/editor_session.h"
#include "neomifes/app/workspace.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"

namespace fs = std::filesystem;

namespace {

using neomifes::app::DocumentFileState;
using neomifes::app::EditorSession;
using neomifes::app::Workspace;
using neomifes::core::InsertTextCommand;
using neomifes::document::Document;
using neomifes::document::LoadError;

// Same idiom as app_document_open_test.cpp's tempFileWith().
fs::path tempFileWith(const std::string& bytes) {
    fs::path p =
        fs::temp_directory_path() / (std::string("nmfs_workspace_") + std::to_string(std::rand()) + ".txt");
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

// Workspace::openFile() returns variant<size_t, LoadError> (WI-05) - this
// helper asserts success and unwraps the index, matching the codebase's
// established std::get_if<Success>(&result) convention (document_open.h).
std::size_t assertOpened(Workspace& workspace, const fs::path& path) {
    const auto             result = workspace.openFile(path);
    const std::size_t* const index  = std::get_if<std::size_t>(&result);
    if (index == nullptr) {
        ADD_FAILURE() << "openFile() unexpectedly failed for " << path;
        return 0;
    }
    return *index;
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

    const std::size_t index = assertOpened(workspace, file);

    EXPECT_EQ(index, 1U);
    EXPECT_EQ(workspace.sessionCount(), 2U);
    EXPECT_EQ(workspace.activeIndex(), 1U);
    EXPECT_EQ(workspace.active().document().toU16String(), u"hello");
    fs::remove(file);
}

TEST(WorkspaceTest, OpenFileReturnsExistingIndexWithoutReloadingIfAlreadyOpen) {
    Workspace          workspace;
    const fs::path     file       = tempFileWith("original");
    const std::size_t firstIndex = assertOpened(workspace, file);

    // Activate a different tab, then re-open the same file - should just
    // re-activate the existing tab rather than appending a duplicate.
    assertOpened(workspace, tempFileWith("unrelated"));
    ASSERT_EQ(workspace.sessionCount(), 3U);

    const std::size_t secondIndex = assertOpened(workspace, file);

    EXPECT_EQ(secondIndex, firstIndex);
    EXPECT_EQ(workspace.sessionCount(), 3U);  // no new session appended
    EXPECT_EQ(workspace.activeIndex(), firstIndex);
    fs::remove(file);
}

TEST(WorkspaceTest, OpenFileDedupsAcrossRelativeAndAbsolutePathSpelling) {
    Workspace          workspace;
    const fs::path     file          = tempFileWith("content");
    const std::size_t absoluteIndex = assertOpened(workspace, fs::absolute(file));

    // weakly_canonical() normalizes "a/./b" style spellings to the same
    // identity as the absolute path.
    const std::size_t canonicalIndex = assertOpened(workspace, fs::weakly_canonical(file));

    EXPECT_EQ(canonicalIndex, absoluteIndex);
    EXPECT_EQ(workspace.sessionCount(), 2U);
    fs::remove(file);
}

TEST(WorkspaceTest, OpenFileReturnsLoadErrorAndLeavesWorkspaceUntouchedForMissingFile) {
    Workspace      workspace;
    const fs::path missing = fs::temp_directory_path() / "nmfs_workspace_does_not_exist.txt";

    const auto result = workspace.openFile(missing);

    const LoadError* const error = std::get_if<LoadError>(&result);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, LoadError::NotFound);
    EXPECT_EQ(workspace.sessionCount(), 1U);
    EXPECT_EQ(workspace.activeIndex(), 0U);
}

TEST(WorkspaceTest, OpenBlankAppendsAndActivatesAnUntitledSession) {
    Workspace workspace;

    const std::size_t index = workspace.openBlank();

    EXPECT_EQ(index, 1U);
    EXPECT_EQ(workspace.sessionCount(), 2U);
    EXPECT_EQ(workspace.activeIndex(), 1U);
    EXPECT_TRUE(workspace.active().isUntitled());
}

TEST(WorkspaceTest, OpenBlankLeavesOtherSessionsUntouched) {
    Workspace      workspace;
    const fs::path file = tempFileWith("hello");
    assertOpened(workspace, file);
    workspace.active().document().insertText(workspace.active().document().length(), u" world");
    ASSERT_TRUE(workspace.active().isDirty());

    const std::size_t blankIndex = workspace.openBlank();

    EXPECT_EQ(blankIndex, 2U);
    EXPECT_EQ(workspace.sessionCount(), 3U);
    workspace.activate(1);
    EXPECT_TRUE(workspace.active().isDirty());
    EXPECT_EQ(workspace.active().document().toU16String(), u"hello world");
    fs::remove(file);
}

TEST(WorkspaceTest, AdoptSessionAppendsAndActivatesIt) {
    Workspace workspace;

    Document doc;
    doc.insertText(0, u"recovered content");
    doc.markDirty();
    auto recovered = std::make_unique<EditorSession>(std::move(doc), DocumentFileState{},
                                                      std::optional<fs::path>("C:/recovered.txt"));

    const std::size_t index = workspace.adoptSession(std::move(recovered));

    EXPECT_EQ(index, 1U);
    EXPECT_EQ(workspace.sessionCount(), 2U);
    EXPECT_EQ(workspace.activeIndex(), 1U);
    EXPECT_EQ(workspace.active().document().toU16String(), u"recovered content");
    EXPECT_TRUE(workspace.active().isDirty());
    EXPECT_EQ(workspace.active().path(), fs::path("C:/recovered.txt"));
}

TEST(WorkspaceTest, AdoptSessionLeavesOtherSessionsUntouched) {
    Workspace      workspace;
    const fs::path file = tempFileWith("hello");
    assertOpened(workspace, file);
    workspace.active().document().insertText(workspace.active().document().length(), u" world");
    ASSERT_TRUE(workspace.active().isDirty());

    Document recoveredDoc;
    recoveredDoc.insertText(0, u"recovered");
    recoveredDoc.markDirty();
    auto recovered = std::make_unique<EditorSession>(std::move(recoveredDoc), DocumentFileState{},
                                                      std::optional<fs::path>("C:/other.txt"));
    const std::size_t recoveredIndex = workspace.adoptSession(std::move(recovered));

    EXPECT_EQ(recoveredIndex, 2U);
    EXPECT_EQ(workspace.sessionCount(), 3U);
    workspace.activate(1);
    EXPECT_TRUE(workspace.active().isDirty());
    EXPECT_EQ(workspace.active().document().toU16String(), u"hello world");
    fs::remove(file);
}

TEST(WorkspaceTest, CloseSessionRemovesNonActiveSessionAndKeepsActiveIndexValid) {
    Workspace      workspace;
    const fs::path fileA = tempFileWith("a");
    const fs::path fileB = tempFileWith("b");
    assertOpened(workspace, fileA);  // index 1
    assertOpened(workspace, fileB);  // index 2, active
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
    assertOpened(workspace, tempFileWith("x"));
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
    assertOpened(workspace, fileA);  // index 1
    assertOpened(workspace, fileB);  // index 2, active

    const bool closed = workspace.closeSession(1);  // fileA, before the active index

    EXPECT_TRUE(closed);
    EXPECT_EQ(workspace.activeIndex(), 1U);  // shifted down from 2
    EXPECT_EQ(workspace.active().document().toU16String(), u"b");
    fs::remove(fileA);
    fs::remove(fileB);
}

TEST(WorkspaceTest, ActivateSwitchesActiveSession) {
    Workspace workspace;
    assertOpened(workspace, tempFileWith("x"));

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
    assertOpened(workspace, tempFileWith("x"));
    workspace.sessionAt(0).document().insertText(0, u"edit");

    EXPECT_TRUE(workspace.hasUnsavedChanges());
}

TEST(WorkspaceTest, UndoHistoryIsIndependentPerSession) {
    Workspace workspace;
    workspace.active().dispatcher().dispatch(std::make_unique<InsertTextCommand>(0, u"session0 text"));
    static_cast<void>(workspace.openBlank());
    workspace.active().dispatcher().dispatch(std::make_unique<InsertTextCommand>(0, u"session1 text"));
    ASSERT_TRUE(workspace.active().isDirty());

    // Undoing the newly-opened blank session must not touch session 0's
    // document at all - each EditorSession owns its own CommandDispatcher/
    // UndoStack (WI-04), so tab isolation is structural, not something this
    // WI needs to implement - this test pins that guarantee.
    EXPECT_TRUE(workspace.active().dispatcher().undo());

    EXPECT_EQ(workspace.active().document().toU16String(), u"");
    workspace.activate(0);
    EXPECT_EQ(workspace.active().document().toU16String(), u"session0 text");
}

}  // namespace
