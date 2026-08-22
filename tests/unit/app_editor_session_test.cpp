// WI-14b: EditorSession's per-tab log-mode state (logModel()/
// logPatternRule()/logIndexInFlight()/applyLogIndexResult()). Headless -
// beginLogIndexing() requires a real LogIndexWorker (background thread +
// HWND), so its round trip is exercised by the integration test
// (tests/integration/logmode_log_index_worker_test.cpp and the future
// end-to-end wiring test) instead of here.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "neomifes/app/editor_session.h"
#include "neomifes/csvmode/csv_model.h"
#include "neomifes/git/git_diff_worker.h"
#include "neomifes/git/git_repository.h"
#include "neomifes/jsontree/json_tree.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"

namespace {

using neomifes::app::EditorSession;
using neomifes::csvmode::CsvModel;
using neomifes::git::GitDiffWorker;
using neomifes::git::kMsgGitDiffReady;
using neomifes::git::LineDiffKind;
using neomifes::git::LineDiffRegion;
using neomifes::jsontree::JsonNode;
using neomifes::jsontree::JsonNodeKind;
using neomifes::logmode::kAllLogLevelsVisible;
using neomifes::logmode::LogLevel;
using neomifes::logmode::logLevelFilterBit;
using neomifes::logmode::LogModel;

TEST(EditorSessionLogModeStateTest, InitiallyHasNoLogModelOrPatternRuleAndIsNotInFlight) {
    const EditorSession session;
    EXPECT_FALSE(session.logModel().has_value());
    EXPECT_FALSE(session.logPatternRule().has_value());
    EXPECT_FALSE(session.logIndexInFlight());
}

TEST(EditorSessionLogModeStateTest, ApplyLogIndexResultPopulatesLogModelAndClearsInFlight) {
    EditorSession session;

    LogModel model;
    session.applyLogIndexResult(std::move(model));

    const auto& logModel = session.logModel();
    ASSERT_TRUE(logModel.has_value());
    EXPECT_TRUE(logModel.value().lines().empty());
    EXPECT_FALSE(session.logIndexInFlight());
}

// WI-14c
TEST(EditorSessionLogModeStateTest, LogLevelFilterMaskDefaultsToShowingAllLevels) {
    const EditorSession session;
    EXPECT_EQ(session.logLevelFilterMask(), kAllLogLevelsVisible);
}

TEST(EditorSessionLogModeStateTest, LogLevelFilterMaskIsMutableThroughItsReferenceAccessor) {
    EditorSession session;
    session.logLevelFilterMask() = logLevelFilterBit(LogLevel::Error);
    EXPECT_EQ(session.logLevelFilterMask(), logLevelFilterBit(LogLevel::Error));
}

TEST(EditorSessionLogModeStateTest, DisableLogModeClearsModelAndResetsFilterMask) {
    // beginLogIndexing() (the only setter for logPatternRule()) requires a
    // real LogIndexWorker/HWND, so this headless test exercises the two
    // pieces of state that ARE settable without one: logModel() (via
    // applyLogIndexResult()) and the filter mask (via the mutable
    // accessor). disableLogMode()'s effect on logPatternRule() itself is
    // covered by the real-worker integration test
    // (tests/integration/logmode_log_index_worker_test.cpp).
    EditorSession session;
    session.applyLogIndexResult(LogModel{});
    session.logLevelFilterMask() = logLevelFilterBit(LogLevel::Error);

    session.disableLogMode();

    EXPECT_FALSE(session.logModel().has_value());
    EXPECT_FALSE(session.logIndexInFlight());
    EXPECT_EQ(session.logLevelFilterMask(), kAllLogLevelsVisible);
}

// WI-15b: EditorSession's per-tab JSON-tree state (jsonTree()/
// jsonTreeIndexInFlight()/applyJsonTreeResult()). Headless -
// beginJsonTreeIndexing() requires a real JsonTreeWorker (background thread
// + HWND), so its round trip is exercised by the integration test
// (tests/integration/jsontree_json_tree_worker_test.cpp) instead of here -
// same split as EditorSessionLogModeStateTest above.
TEST(EditorSessionJsonTreeStateTest, InitiallyHasNoJsonTreeAndIsNotInFlight) {
    const EditorSession session;
    EXPECT_FALSE(session.jsonTree().has_value());
    EXPECT_FALSE(session.jsonTreeIndexInFlight());
}

TEST(EditorSessionJsonTreeStateTest, ApplyJsonTreeResultPopulatesJsonTreeAndClearsInFlight) {
    EditorSession session;

    JsonNode node;
    node.kind = JsonNodeKind::Object;
    session.applyJsonTreeResult(std::move(node));

    const auto& tree = session.jsonTree();
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(tree->kind, JsonNodeKind::Object);
    EXPECT_FALSE(session.jsonTreeIndexInFlight());
}

// json_tree_worker.h's design departure from LogIndexWorker (see that
// header's own comment): a failed parse must still reach
// applyJsonTreeResult() as std::nullopt rather than being dropped, so
// jsonTreeIndexInFlight() cannot get stuck at true. This pins down
// EditorSession's half of that contract.
TEST(EditorSessionJsonTreeStateTest, ApplyJsonTreeResultWithNulloptClearsInFlightLeavingTreeEmpty) {
    EditorSession session;

    JsonNode node;
    node.kind = JsonNodeKind::Object;
    session.applyJsonTreeResult(std::move(node));
    ASSERT_TRUE(session.jsonTree().has_value());

    session.applyJsonTreeResult(std::nullopt);

    EXPECT_FALSE(session.jsonTree().has_value());
    EXPECT_FALSE(session.jsonTreeIndexInFlight());
}

// WI-16b: EditorSession's per-tab CSV-model state (csvModel()/
// csvIndexInFlight()/applyCsvIndexResult()). Headless -
// beginCsvIndexing() requires a real CsvModelWorker (background thread +
// HWND), so its round trip is exercised by the integration test
// (tests/integration/csvmode_csv_model_worker_test.cpp) instead of here -
// same split as EditorSessionLogModeStateTest/EditorSessionJsonTreeStateTest
// above.
TEST(EditorSessionCsvModelStateTest, InitiallyHasNoCsvModelAndIsNotInFlight) {
    const EditorSession session;
    EXPECT_FALSE(session.csvModel().has_value());
    EXPECT_FALSE(session.csvIndexInFlight());
}

TEST(EditorSessionCsvModelStateTest, ApplyCsvIndexResultPopulatesCsvModelAndClearsInFlight) {
    EditorSession session;

    auto built = CsvModel::build(session.document());
    ASSERT_TRUE(built.has_value());
    session.applyCsvIndexResult(std::move(*built));

    const auto& model = session.csvModel();
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->rowCount(), 1U);  // a blank EditorSession's document is empty
    EXPECT_FALSE(session.csvIndexInFlight());
}

// WI-17b: EditorSession's per-tab Git-diff state (gitDiff()/
// gitDiffIndexInFlight()/applyGitDiffResult()). Headless -
// beginGitDiffIndexing() requires a real GitDiffWorker (background thread +
// HWND) AND a real Document path to diff against, so its round trip
// (including the Untitled-buffer no-op case) is exercised by the
// integration test (tests/integration/git_diff_worker_test.cpp) instead of
// here - same split as EditorSessionLogModeStateTest/
// EditorSessionJsonTreeStateTest/EditorSessionCsvModelStateTest above.
TEST(EditorSessionGitDiffStateTest, InitiallyHasNoGitDiffAndIsNotInFlight) {
    const EditorSession session;
    EXPECT_FALSE(session.gitDiff().has_value());
    EXPECT_FALSE(session.gitDiffIndexInFlight());
}

TEST(EditorSessionGitDiffStateTest, ApplyGitDiffResultPopulatesGitDiffAndClearsInFlight) {
    EditorSession session;

    std::vector<LineDiffRegion> regions{
        LineDiffRegion{.startLine = 0, .lineCount = 1, .kind = LineDiffKind::Added}};
    session.applyGitDiffResult(regions);

    const auto& diff = session.gitDiff();
    ASSERT_TRUE(diff.has_value());
    EXPECT_EQ(*diff, regions);
    EXPECT_FALSE(session.gitDiffIndexInFlight());
}

// git_diff_worker.h's design departure from CsvModelWorker (see that
// header's own comment): a request for a file outside any Git repository
// must still reach applyGitDiffResult() as std::nullopt rather than being
// dropped, so gitDiffIndexInFlight() cannot get stuck at true. This pins
// down EditorSession's half of that contract, same as
// EditorSessionJsonTreeStateTest's identical test for JsonTreeWorker.
TEST(EditorSessionGitDiffStateTest, ApplyGitDiffResultWithNulloptClearsInFlightLeavingDiffEmpty) {
    EditorSession session;

    session.applyGitDiffResult(std::vector<LineDiffRegion>{
        LineDiffRegion{.startLine = 0, .lineCount = 1, .kind = LineDiffKind::Added}});
    ASSERT_TRUE(session.gitDiff().has_value());

    session.applyGitDiffResult(std::nullopt);

    EXPECT_FALSE(session.gitDiff().has_value());
    EXPECT_FALSE(session.gitDiffIndexInFlight());
}

// beginGitDiffIndexing()'s own guard clause: an Untitled buffer has no path
// to diff against, so this must be a complete no-op (no GitDiffWorker
// request fired, gitDiffIndexInFlight() stays false) - not just "gitDiff()
// stays nullopt", which would also be true if a request were fired and
// simply hadn't completed yet. A real GitDiffWorker + hidden window is used
// here (rather than app_editor_session_test.cpp's usual pure-headless
// style) specifically to prove the negative: no message ever arrives. This
// does not touch libgit2 directly (unlike git_repository_test.cpp/
// git_diff_worker_test.cpp's own fixture-building tests), so it stays a
// unit test rather than needing the integration suite's extra libgit2
// include-directory wiring.
TEST(EditorSessionGitDiffStateTest, BeginGitDiffIndexingIsNoOpForUntitledSession) {
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100, nullptr, nullptr, nullptr, nullptr);
    ASSERT_NE(hwnd, nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    EditorSession session;
    ASSERT_TRUE(session.isUntitled());
    GitDiffWorker worker(hwnd);

    session.beginGitDiffIndexing(worker);
    EXPECT_FALSE(session.gitDiffIndexInFlight());

    const ULONGLONG deadline = ::GetTickCount64() + 200;
    bool             sawMessage = false;
    while (::GetTickCount64() < deadline) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == kMsgGitDiffReady) {
                sawMessage = true;
            } else {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        ::Sleep(5);
    }
    EXPECT_FALSE(sawMessage) << "beginGitDiffIndexing() must not fire a request for an Untitled session";

    ::DestroyWindow(hwnd);
}

}  // namespace
