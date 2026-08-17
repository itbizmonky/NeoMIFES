// WI-14b: EditorSession's per-tab log-mode state (logModel()/
// logPatternRule()/logIndexInFlight()/applyLogIndexResult()). Headless -
// beginLogIndexing() requires a real LogIndexWorker (background thread +
// HWND), so its round trip is exercised by the integration test
// (tests/integration/logmode_log_index_worker_test.cpp and the future
// end-to-end wiring test) instead of here.

#include <gtest/gtest.h>

#include "neomifes/app/editor_session.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"

namespace {

using neomifes::app::EditorSession;
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

}  // namespace
