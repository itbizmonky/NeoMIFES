// Integration test (not a unit test): exercises JsonTreeWorker's real
// background std::thread + PostMessageW-based completion handoff (WI-15b) -
// modeled directly on logmode_log_index_worker_test.cpp, this codebase's
// existing template for testing this exact kind of worker.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/jsontree/json_tree.h"
#include "neomifes/jsontree/json_tree_worker.h"

namespace {

using neomifes::document::Document;
using neomifes::jsontree::JsonNode;
using neomifes::jsontree::JsonNodeKind;
using neomifes::jsontree::JsonTreeWorker;
using neomifes::jsontree::kMsgJsonTreeReady;

// Same hidden-window pattern as logmode_log_index_worker_test.cpp - a plain
// message-capable window is all JsonTreeWorker's PostMessageW target needs.
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

struct JsonTreeResult {
    const void*                        sessionToken;
    std::unique_ptr<std::optional<JsonNode>> tree;
};

// Pumps this thread's message queue until `expectedCount` kMsgJsonTreeReady
// messages have been observed or `timeoutMs` elapses. Deliberately collects
// EVERY result (not just the latest) - the whole point of JsonTreeWorker's
// FIFO queue is that no request is ever silently dropped, so the test
// verifying that must be able to observe all of them.
std::vector<JsonTreeResult> pumpForJsonTreeResults(std::size_t expectedCount, std::uint32_t timeoutMs) {
    std::vector<JsonTreeResult> results;
    const ULONGLONG             deadline = ::GetTickCount64() + timeoutMs;
    while (::GetTickCount64() < deadline && results.size() < expectedCount) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            if (msg.message == kMsgJsonTreeReady) {
                results.push_back(JsonTreeResult{
                    .sessionToken = reinterpret_cast<const void*>(msg.wParam),
                    .tree = std::unique_ptr<std::optional<JsonNode>>(
                        reinterpret_cast<std::optional<JsonNode>*>(msg.lParam)),
                });
            } else {
                ::DispatchMessageW(&msg);
            }
        }
        ::Sleep(5);
    }
    return results;
}

TEST(JsonTreeWorkerTest, RequestIndexDeliversTreeViaWindowMessage) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"{\"a\":1,\"b\":[true,null]}");

    const int      token = 42;
    JsonTreeWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), &token);

    auto results = pumpForJsonTreeResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgJsonTreeReady never arrived within the timeout";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].tree, nullptr);
    ASSERT_TRUE(results[0].tree->has_value());
    EXPECT_EQ((*results[0].tree)->kind, JsonNodeKind::Object);
}

// 設計方針2の核心的検証: SyntaxWorker型の「最新のみ保持・上書き」ではない
// ことを直接証明する。2つの異なるセッショントークンで連続してrequestIndex()
// を呼び(間にpumpを挟まない)、両方の結果が届くことを確認する - 「最新の
// み保持」方式なら1件目の結果は永久に処理されないはずのシナリオ。
TEST(JsonTreeWorkerTest, MultipleSessionsAreAllProcessedNotJustTheLatest) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document docA;
    docA.insertText(0, u"{\"who\":\"a\"}");
    Document docB;
    docB.insertText(0, u"{\"who\":\"b\"}");

    const int      tokenA = 1;
    const int      tokenB = 2;
    JsonTreeWorker worker(window.get());
    worker.requestIndex(docA.snapshot(), &tokenA);
    worker.requestIndex(docB.snapshot(), &tokenB);

    const auto results = pumpForJsonTreeResults(2, 5000);
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

TEST(JsonTreeWorkerTest, WorkerDestructorJoinsCleanlyWithPendingRequests) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"{\"x\":1}");

    const int token = 7;
    {
        JsonTreeWorker worker(window.get());
        worker.requestIndex(doc.snapshot(), &token);
        // Deliberately no pump here - the destructor below must join the
        // worker thread cleanly regardless of whether it already started
        // (or even finished) processing the pending request.
    }
    // Drain and discard whatever the worker managed to post before
    // shutdown - reaching this line without hanging/crashing is the test.
    (void)pumpForJsonTreeResults(1, 200);
    SUCCEED();
}

// json_tree_worker.h's own design departure from LogIndexWorker (see that
// header's comment): a failed parse must still be posted, not dropped,
// or the requesting EditorSession's "indexing in flight" flag would stay
// stuck forever. This pins that contract down at the worker/message level.
TEST(JsonTreeWorkerTest, RequestIndexOnInvalidJsonStillDeliversNulloptResult) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"this is not json");

    const int      token = 99;
    JsonTreeWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), &token);

    auto results = pumpForJsonTreeResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgJsonTreeReady must arrive even for invalid JSON";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].tree, nullptr);
    EXPECT_FALSE(results[0].tree->has_value());
}

// Safety-net for the worker thread's stack: nlohmann::ordered_json::parse()
// runs on JsonTreeWorker's own std::thread (default stack size), not the
// UI thread. buildTree() (this module's OWN tree-build pass, WI-15a) is
// guaranteed iterative, but nlohmann's own DOM-building pass recurses one
// C++ stack frame per nesting level in both construction and destruction,
// with no depth-limiting option of its own - WI-15b's final gate
// (ubsan/clang-cl) actually reproduced a genuine STATUS_STACK_OVERFLOW at
// kDepth=2000 (MSVC Debug/Release survived it, but that was NOT proof of
// safety - stack consumption per nesting level varies with build/
// optimization settings). WI-15c (docs/issues/
// json_tree_worker_deep_nesting_stack_overflow.md) closed this by adding a
// SAX-based pre-parse depth check (json_tree.cpp's kMaxJsonNestingDepth,
// 200) that rejects anything deeper BEFORE nlohmann::ordered_json::parse()
// ever runs, so this test now pins down the actual contract - std::nullopt,
// not a crash - rather than merely "didn't crash or hang". kDepth is kept
// well below 2000 (the one depth with a confirmed real crash under ubsan)
// so this test does not itself risk reproducing that crash if the guard
// were ever accidentally removed.
TEST(JsonTreeWorkerTest, RequestIndexOnDeeplyNestedJsonReturnsNulloptNotCrash) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    constexpr int  kDepth = 500;  // > kMaxJsonNestingDepth (200), < the 2000 that crashed unguarded parse()
    std::u16string text(static_cast<std::size_t>(kDepth), u'[');
    text.append(static_cast<std::size_t>(kDepth), u']');
    Document doc;
    doc.insertText(0, text);

    const int      token = 123;
    JsonTreeWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), &token);

    auto results = pumpForJsonTreeResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "worker thread must survive deeply nested input";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].tree, nullptr);
    EXPECT_FALSE(results[0].tree->has_value()) << "depth guard must reject input past kMaxJsonNestingDepth";
}

}  // namespace
