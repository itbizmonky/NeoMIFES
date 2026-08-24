// Integration test (not a unit test): exercises XmlTreeWorker's real
// background std::thread + PostMessageW-based completion handoff (WI-15g) -
// modeled directly on jsontree_json_tree_worker_test.cpp, this codebase's
// existing template for testing this exact kind of worker.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/xmltree/xml_tree.h"
#include "neomifes/xmltree/xml_tree_worker.h"

namespace {

using neomifes::document::Document;
using neomifes::xmltree::kMsgXmlTreeReady;
using neomifes::xmltree::XmlNodeKind;
using neomifes::xmltree::XmlTree;
using neomifes::xmltree::XmlTreeWorker;

// Same hidden-window pattern as jsontree_json_tree_worker_test.cpp - a plain
// message-capable window is all XmlTreeWorker's PostMessageW target needs.
class HiddenWindow {
public:
    HiddenWindow()
        : m_hwnd(::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100, nullptr, nullptr, nullptr,
                                   nullptr)) {}
    ~HiddenWindow() {
        if (m_hwnd != nullptr) {
            ::DestroyWindow(m_hwnd);
        }
    }
    HiddenWindow(const HiddenWindow&)            = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
    HiddenWindow(HiddenWindow&&)                 = delete;
    HiddenWindow& operator=(HiddenWindow&&)      = delete;

    [[nodiscard]] HWND get() const noexcept { return m_hwnd; }

private:
    HWND m_hwnd = nullptr;
};

struct XmlTreeResult {
    const void*             sessionToken;
    std::unique_ptr<XmlTree> tree;
};

// Builds `<a><a>...<a>x</a>...</a></a>` with `depth` levels of nesting -
// factored out purely to keep RequestIndexOnDeeplyNestedXmlSurvivesWorkerThread's
// own cognitive complexity under clang-tidy's threshold (same rationale as
// xmltree_xml_tree_test.cpp's identical helper - gtest's assertion macros
// already count heavily against the enclosing function).
[[nodiscard]] std::u16string buildDeepNestingXml(int depth) {
    std::u16string text;
    text.reserve((static_cast<std::size_t>(depth) * 7) + 1);
    for (int i = 0; i < depth; ++i) {
        text += u"<a>";
    }
    text += u"x";
    for (int i = 0; i < depth; ++i) {
        text += u"</a>";
    }
    return text;
}

// Pumps this thread's message queue until `expectedCount` kMsgXmlTreeReady
// messages have been observed or `timeoutMs` elapses. Deliberately collects
// EVERY result (not just the latest) - the whole point of XmlTreeWorker's
// FIFO queue is that no request is ever silently dropped, so the test
// verifying that must be able to observe all of them.
std::vector<XmlTreeResult> pumpForXmlTreeResults(std::size_t expectedCount, std::uint32_t timeoutMs) {
    std::vector<XmlTreeResult> results;
    const ULONGLONG            deadline = ::GetTickCount64() + timeoutMs;
    while (::GetTickCount64() < deadline && results.size() < expectedCount) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            if (msg.message == kMsgXmlTreeReady) {
                results.push_back(XmlTreeResult{
                    .sessionToken = reinterpret_cast<const void*>(msg.wParam),
                    .tree         = std::unique_ptr<XmlTree>(reinterpret_cast<XmlTree*>(msg.lParam)),
                });
            } else {
                ::DispatchMessageW(&msg);
            }
        }
        ::Sleep(5);
    }
    return results;
}

TEST(XmlTreeWorkerTest, RequestIndexDeliversTreeViaWindowMessage) {
    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"<a><b>1</b></a>");

    const int     token = 42;
    XmlTreeWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), &token);

    auto results = pumpForXmlTreeResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgXmlTreeReady never arrived within the timeout";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].tree, nullptr);
    EXPECT_FALSE(results[0].tree->hasErrors);
    EXPECT_EQ(results[0].tree->root.kind, XmlNodeKind::Element);
    EXPECT_EQ(results[0].tree->root.tagName, u"a");
}

// 設計方針2の核心的検証: SyntaxWorker型の「最新のみ保持・上書き」ではない
// ことを直接証明する。2つの異なるセッショントークンで連続してrequestIndex()
// を呼び(間にpumpを挟まない)、両方の結果が届くことを確認する - 「最新の
// み保持」方式なら1件目の結果は永久に処理されないはずのシナリオ。
TEST(XmlTreeWorkerTest, MultipleSessionsAreAllProcessedNotJustTheLatest) {
    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document docA;
    docA.insertText(0, u"<a/>");
    Document docB;
    docB.insertText(0, u"<b/>");

    const int     tokenA = 1;
    const int     tokenB = 2;
    XmlTreeWorker worker(window.get());
    worker.requestIndex(docA.snapshot(), &tokenA);
    worker.requestIndex(docB.snapshot(), &tokenB);

    const auto results = pumpForXmlTreeResults(2, 5000);
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

TEST(XmlTreeWorkerTest, WorkerDestructorJoinsCleanlyWithPendingRequests) {
    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"<x/>");

    const int token = 7;
    {
        XmlTreeWorker worker(window.get());
        worker.requestIndex(doc.snapshot(), &token);
        // Deliberately no pump here - the destructor below must join the
        // worker thread cleanly regardless of whether it already started
        // (or even finished) processing the pending request.
    }
    // Drain and discard whatever the worker managed to post before
    // shutdown - reaching this line without hanging/crashing is the test.
    (void)pumpForXmlTreeResults(1, 200);
    SUCCEED();
}

// xml_tree_worker.h's own design departure from JsonTreeWorker (see that
// header's comment): parseXmlTree() never returns std::optional at all, so
// there is no "drop vs post a failure" question - malformed input still
// yields a real XmlTree (root.kind == Error), and this pins that contract
// down at the worker/message level, not just at the headless parseXmlTree()
// level (WI-15f's own unit tests already cover the latter).
TEST(XmlTreeWorkerTest, RequestIndexOnMalformedXmlStillDeliversErrorTree) {
    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"<From>Jani</from>");

    const int     token = 99;
    XmlTreeWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), &token);

    auto results = pumpForXmlTreeResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "kMsgXmlTreeReady must arrive even for malformed XML";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].tree, nullptr);
    EXPECT_TRUE(results[0].tree->hasErrors);
    EXPECT_EQ(results[0].tree->root.kind, XmlNodeKind::Error);
}

// Safety-net for the worker thread's stack: parseXmlTree() runs on
// XmlTreeWorker's own std::thread (default stack size), not the UI thread.
// buildXmlTree() (this module's own tree-build pass, WI-15f) is guaranteed
// iterative, and a standalone probe confirmed tree-sitter-xml's own C parser
// survives even pathological nesting depths without crashing (see
// xml_tree.cpp's header comment) - unlike JSON, no kMaxXmlNestingDepth guard
// exists, so this test just confirms a realistically deep, well-formed
// document round-trips correctly through the worker thread.
TEST(XmlTreeWorkerTest, RequestIndexOnDeeplyNestedXmlSurvivesWorkerThread) {
    const HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    constexpr int kDepth = 300;  // comfortably below the ~505 tree-sitter-xml misparse boundary
    Document      doc;
    doc.insertText(0, buildDeepNestingXml(kDepth));

    const int     token = 123;
    XmlTreeWorker worker(window.get());
    worker.requestIndex(doc.snapshot(), &token);

    auto results = pumpForXmlTreeResults(1, 5000);
    ASSERT_EQ(results.size(), 1U) << "worker thread must survive deeply nested well-formed input";
    EXPECT_EQ(results[0].sessionToken, &token);
    ASSERT_NE(results[0].tree, nullptr);
    EXPECT_FALSE(results[0].tree->hasErrors);
    EXPECT_EQ(results[0].tree->root.kind, XmlNodeKind::Element);
}

}  // namespace
