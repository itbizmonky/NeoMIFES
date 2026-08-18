// Integration test (not a unit test): exercises CsvModelWorker's real
// background std::thread + PostMessageW-based completion handoff (WI-16b) -
// modeled directly on jsontree_json_tree_worker_test.cpp/
// logmode_log_index_worker_test.cpp, this codebase's existing templates for
// testing this exact kind of worker.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "neomifes/csvmode/csv_model.h"
#include "neomifes/csvmode/csv_model_worker.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::csvmode::CsvModel;
using neomifes::csvmode::CsvModelWorker;
using neomifes::csvmode::CsvParseOptions;
using neomifes::csvmode::kMsgCsvIndexReady;
using neomifes::document::Document;

// Same hidden-window pattern as jsontree_json_tree_worker_test.cpp - a plain
// message-capable window is all CsvModelWorker's PostMessageW target needs.
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

struct CsvIndexResult {
    const void*             sessionToken;
    std::unique_ptr<CsvModel> model;
};

// Pumps this thread's message queue until `expectedCount` kMsgCsvIndexReady
// messages have been observed or `timeoutMs` elapses. Deliberately collects
// EVERY result (not just the latest) - the whole point of CsvModelWorker's
// FIFO queue is that no request is ever silently dropped, so the test
// verifying that must be able to observe all of them.
std::vector<CsvIndexResult> pumpForCsvIndexResults(std::size_t expectedCount, std::uint32_t timeoutMs) {
    std::vector<CsvIndexResult> results;
    const ULONGLONG              deadline = ::GetTickCount64() + timeoutMs;
    while (::GetTickCount64() < deadline && results.size() < expectedCount) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            if (msg.message == kMsgCsvIndexReady) {
                results.push_back(CsvIndexResult{
                    .sessionToken = reinterpret_cast<const void*>(msg.wParam),
                    .model        = std::unique_ptr<CsvModel>(reinterpret_cast<CsvModel*>(msg.lParam)),
                });
            } else {
                ::DispatchMessageW(&msg);
            }
        }
        ::Sleep(5);
    }
    return results;
}

TEST(CsvModelWorkerTest, RequestIndexDeliversModelViaWindowMessage) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"a,b\n1,2\n");

    const int      token = 42;
    CsvModelWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), CsvParseOptions{}, &token);

    auto results = pumpForCsvIndexResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgCsvIndexReady never arrived within the timeout";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].model, nullptr);
    EXPECT_EQ(results[0].model->rowCount(), 3U);  // header + 1 data row + trailing implicit empty row
}

// The core of this design (see csv_model_worker.h's header comment): NOT
// SyntaxWorker's "keep only the latest pending request" model. Two distinct
// session tokens requestIndex() back-to-back (no pump between them), both
// results must arrive - a "latest only" design would leave the first
// forever unprocessed.
TEST(CsvModelWorkerTest, MultipleSessionsAreAllProcessedNotJustTheLatest) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document docA;
    docA.insertText(0, u"who\na\n");
    Document docB;
    docB.insertText(0, u"who\nb\n");

    const int      tokenA = 1;
    const int      tokenB = 2;
    CsvModelWorker worker(window.get());
    worker.requestIndex(docA.snapshot(), CsvParseOptions{}, &tokenA);
    worker.requestIndex(docB.snapshot(), CsvParseOptions{}, &tokenB);

    const auto results = pumpForCsvIndexResults(2, 5000);
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

TEST(CsvModelWorkerTest, WorkerDestructorJoinsCleanlyWithPendingRequests) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"x\n1\n");

    const int token = 7;
    {
        CsvModelWorker worker(window.get());
        worker.requestIndex(doc.snapshot(), CsvParseOptions{}, &token);
        // Deliberately no pump here - the destructor below must join the
        // worker thread cleanly regardless of whether it already started
        // (or even finished) processing the pending request.
    }
    // Drain and discard whatever the worker managed to post before
    // shutdown - reaching this line without hanging/crashing is the test.
    (void)pumpForCsvIndexResults(1, 200);
    SUCCEED();
}

// csv_model_worker.h's own design departure from JsonTreeWorker (see that
// header's comment): unlike a JSON parse failure (an everyday, content-
// dependent outcome), CsvParseError::InvalidDelimiter is purely a caller
// configuration mistake - the worker silently drops it, the same treatment
// LogIndexWorker gives LogPatternError::InvalidRegex. This pins that
// contract down at the worker/message level: no message must ever arrive
// for a request whose options.delimiter is invalid.
TEST(CsvModelWorkerTest, RequestIndexWithInvalidDelimiterNeverDeliversAMessage) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"a,b\n1,2\n");

    CsvParseOptions options;
    options.delimiter = u'"';  // csv_model.h's own documented failure case

    const int      token = 99;
    CsvModelWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), options, &token);

    const auto results = pumpForCsvIndexResults(1, 500);
    EXPECT_TRUE(results.empty()) << "an InvalidDelimiter request must never post kMsgCsvIndexReady";
}

}  // namespace
