// Integration test (not a unit test): exercises LogIndexWorker's real
// background std::thread + PostMessageW-based completion handoff (WI-14b) -
// modeled directly on render_syntax_worker_test.cpp, this codebase's
// existing template for testing this exact kind of worker.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/logmode/log_index_worker.h"
#include "neomifes/logmode/log_model.h"
#include "neomifes/logmode/log_pattern.h"

namespace {

using neomifes::document::Document;
using neomifes::logmode::builtInLogPatterns;
using neomifes::logmode::kMsgLogIndexReady;
using neomifes::logmode::LogIndexWorker;
using neomifes::logmode::LogModel;
using neomifes::logmode::LogPatternRule;

[[nodiscard]] const LogPatternRule& ruleById(std::u16string_view id) {
    for (const LogPatternRule& rule : builtInLogPatterns()) {
        if (rule.id == id) {
            return rule;
        }
    }
    ADD_FAILURE() << "no built-in rule with that id";
    return builtInLogPatterns().front();
}

// Same hidden-window pattern as render_syntax_worker_test.cpp - a plain
// message-capable window is all LogIndexWorker's PostMessageW target needs.
class HiddenWindow {
public:
    HiddenWindow() {
        m_hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100, nullptr, nullptr,
                                   nullptr, nullptr);
    }
    ~HiddenWindow() {
        if (m_hwnd != nullptr) {
            ::DestroyWindow(m_hwnd);
        }
    }
    HiddenWindow(const HiddenWindow&)            = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    [[nodiscard]] HWND get() const noexcept { return m_hwnd; }

private:
    HWND m_hwnd = nullptr;
};

struct LogIndexResult {
    const void*                sessionToken;
    std::unique_ptr<LogModel> model;
};

// Pumps this thread's message queue until `expectedCount` kMsgLogIndexReady
// messages have been observed or `timeoutMs` elapses. Deliberately collects
// EVERY result (not just the latest, unlike render_syntax_worker_test.cpp's
// pumpForLatestTokens()) - the whole point of LogIndexWorker's FIFO queue
// (WI-14b 設計方針3) is that no request is ever silently dropped, so the
// test verifying that must be able to observe all of them.
std::vector<LogIndexResult> pumpForLogIndexResults(std::size_t expectedCount, std::uint32_t timeoutMs) {
    std::vector<LogIndexResult> results;
    const ULONGLONG             deadline = ::GetTickCount64() + timeoutMs;
    while (::GetTickCount64() < deadline && results.size() < expectedCount) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            if (msg.message == kMsgLogIndexReady) {
                results.push_back(LogIndexResult{
                    .sessionToken = reinterpret_cast<const void*>(msg.wParam),
                    .model        = std::unique_ptr<LogModel>(reinterpret_cast<LogModel*>(msg.lParam)),
                });
            } else {
                ::DispatchMessageW(&msg);
            }
        }
        ::Sleep(5);
    }
    return results;
}

TEST(LogIndexWorkerTest, RequestIndexDeliversModelViaWindowMessage) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"127.0.0.1 - - [10/Oct/2000:13:55:36 -0700] \"GET /x HTTP/1.0\" 200 100\n");

    const int      token = 42;
    LogIndexWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), ruleById(u"apache_nginx_clf"), std::nullopt, &token);

    auto results = pumpForLogIndexResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgLogIndexReady never arrived within the timeout";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].model, nullptr);
    ASSERT_GE(results[0].model->lines().size(), 1U);
    EXPECT_TRUE(results[0].model->lines()[0].matched);
}

// WI-14b 設計方針3の核心的検証: SyntaxWorker型の「最新のみ保持・上書き」
// ではないことを直接証明する。2つの異なるセッショントークンで連続して
// requestIndex()を呼び(間にpumpを挟まない)、両方の結果が届くことを
// 確認する - 「最新のみ保持」方式なら1件目の結果は永久に処理されない
// はずのシナリオ。
TEST(LogIndexWorkerTest, MultipleSessionsAreAllProcessedNotJustTheLatest) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document docA;
    docA.insertText(0, u"<34>1 2003-10-11T22:14:15.003Z host app 1 ID1 - a\n");
    Document docB;
    docB.insertText(0, u"<34>1 2003-10-11T22:14:16.003Z host app 2 ID2 - b\n");

    const int      tokenA = 1;
    const int      tokenB = 2;
    LogIndexWorker worker(window.get());
    worker.requestIndex(docA.snapshot(), ruleById(u"rfc5424_syslog"), std::nullopt, &tokenA);
    worker.requestIndex(docB.snapshot(), ruleById(u"rfc5424_syslog"), std::nullopt, &tokenB);

    const auto results = pumpForLogIndexResults(2, 5000);
    ASSERT_EQ(results.size(), 2U) << "both requests must be delivered, not just the latest";

    bool sawTokenA = false;
    bool sawTokenB = false;
    for (const auto& result : results) {
        if (result.sessionToken == &tokenA) {
            sawTokenA = true;
        } else if (result.sessionToken == &tokenB) {
            sawTokenB = true;
        }
    }
    EXPECT_TRUE(sawTokenA);
    EXPECT_TRUE(sawTokenB);
}

TEST(LogIndexWorkerTest, WorkerDestructorJoinsCleanlyWithPendingRequests) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"2026-08-16 10:15:32.123 ERROR Something broke\n");

    const int token = 7;
    {
        LogIndexWorker worker(window.get());
        worker.requestIndex(doc.snapshot(), ruleById(u"generic_iso8601_level"), std::nullopt, &token);
        // Deliberately no pump here - the destructor below must join the
        // worker thread cleanly regardless of whether it already started
        // (or even finished) processing the pending request.
    }
    // Drain and discard whatever the worker managed to post before
    // shutdown - reaching this line without hanging/crashing is the test.
    (void)pumpForLogIndexResults(1, 200);
    SUCCEED();
}

}  // namespace
