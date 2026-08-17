// WI-14b: EditorSession's per-tab log-mode state (logModel()/
// logPatternRule()/logIndexInFlight()/applyLogIndexResult()). Headless -
// beginLogIndexing() requires a real LogIndexWorker (background thread +
// HWND), so its round trip is exercised by the integration test
// (tests/integration/logmode_log_index_worker_test.cpp and the future
// end-to-end wiring test) instead of here.

#include <gtest/gtest.h>

#include "neomifes/app/editor_session.h"
#include "neomifes/logmode/log_model.h"

namespace {

using neomifes::app::EditorSession;
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

}  // namespace
