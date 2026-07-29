// Integration test (not a unit test): exercises SyntaxWorker's real
// background std::thread + PostMessageW-based completion handoff (Phase
// 7c) - this codebase's first std::thread, so a plain unit test binary
// (no real HWND, no message pump) can't exercise it end-to-end.

#include <gtest/gtest.h>

#include <windows.h>

#include <memory>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/render/syntax_worker.h"
#include "neomifes/syntax/syntax.h"

namespace {

using neomifes::document::Document;
using neomifes::render::kMsgSyntaxTokensReady;
using neomifes::render::SyntaxWorker;
using neomifes::syntax::Language;
using neomifes::syntax::parseCpp;
using neomifes::syntax::Token;

// Same hidden-window pattern as render_text_smoke_test.cpp - a plain
// message-capable window is all SyntaxWorker's PostMessageW target needs
// (no D2D/DXGI device required, unlike RenderPipeline's own tests).
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

// Pumps this thread's message queue for up to `timeoutMs`, returning the
// LAST kMsgSyntaxTokensReady payload observed (unique_ptr ownership
// reclaimed immediately per SyntaxWorker's documented contract), or nullptr
// if none arrived in time. Draining to the LAST one (not just the first)
// is what makes the coalescing test below deterministic regardless of how
// many intermediate results happen to slip through.
std::unique_ptr<std::vector<Token>> pumpForLatestTokens(std::uint32_t timeoutMs) {
    std::unique_ptr<std::vector<Token>> latest;
    const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
    while (::GetTickCount64() < deadline) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            if (msg.message == kMsgSyntaxTokensReady) {
                latest.reset(reinterpret_cast<std::vector<Token>*>(msg.lParam));
            } else {
                ::DispatchMessageW(&msg);
            }
        }
        if (latest) {
            // Give any already-in-flight follow-up result a brief chance to
            // also arrive and supersede this one, then stop - avoids
            // returning early on the FIRST of several rapid results.
            ::Sleep(50);
            continue;
        }
        ::Sleep(5);
    }
    return latest;
}

TEST(SyntaxWorkerTest, RequestParseDeliversTokensViaWindowMessage) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"int x = 42;");

    SyntaxWorker worker(window.get());
    worker.requestParse(doc.snapshot(), Language::Cpp, doc.takePendingEdits(),
                        /*resetIncrementalState=*/true);

    const auto tokens = pumpForLatestTokens(5000);
    ASSERT_NE(tokens, nullptr) << "kMsgSyntaxTokensReady never arrived within the timeout";
    // Same known classification as SyntaxParseCppTest.
    // ClassifiesTypeIdentifierNumberAndPunctuation (syntax_syntax_test.cpp):
    // int(Type) x(Variable) =(Punctuation) 42(Number) ;(Punctuation)
    ASSERT_EQ(tokens->size(), 5u);
    EXPECT_EQ((*tokens)[0].kind, neomifes::syntax::TokenKind::Type);
    EXPECT_EQ((*tokens)[3].kind, neomifes::syntax::TokenKind::Number);
}

// Phase 7d: the same worker/message-passing path, but requesting a Python
// parse - confirms the language parameter actually reaches
// syntax::parse() inside the worker thread, not just that SyntaxWorker
// compiles against the new signature.
TEST(SyntaxWorkerTest, RequestParseWithPythonLanguageParsesAsPython) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"x = 42");

    SyntaxWorker worker(window.get());
    worker.requestParse(doc.snapshot(), Language::Python, doc.takePendingEdits(),
                        /*resetIncrementalState=*/true);

    const auto tokens = pumpForLatestTokens(5000);
    ASSERT_NE(tokens, nullptr) << "kMsgSyntaxTokensReady never arrived within the timeout";
    // Same known classification as SyntaxParsePythonTest.
    // ClassifiesSimpleAssignment (syntax_syntax_test.cpp):
    // x(Variable) =(Punctuation) 42(Number) - notably NOT Type for "x",
    // unlike the C++ case above, since Python's grammar has no primitive_type
    // leaf; a wrong (C++) dispatch would instead have produced 5 tokens.
    ASSERT_EQ(tokens->size(), 3u);
    EXPECT_EQ((*tokens)[0].kind, neomifes::syntax::TokenKind::Variable);
    EXPECT_EQ((*tokens)[2].kind, neomifes::syntax::TokenKind::Number);
}

// Phase 7l: replaces the Phase 7c-era "silently discard whatever request
// hadn't been picked up yet" behavior this test used to pin down - true
// incremental reparsing (Phase 7k's IncrementalParser) requires every
// recorded edit to survive even when multiple requestParse() calls race
// ahead of the worker picking any of them up, or the retained tree's byte
// offsets silently desync from the real document (see requestParse()'s doc
// comment). This test forces exactly that race - two calls fired back-to-
// back with no pump in between, the same technique the old version used -
// but now on a SINGLE evolving document via two real edits, and checks that
// no matter how many of the two requests the worker actually processes
// separately, the FINAL delivered tokens exactly match an independent full
// reparse of the FINAL text: proof that no edit was silently dropped.
TEST(SyntaxWorkerTest, RapidSequentialEditsNeverLoseAnEditEvenWhenCoalesced) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    Document doc;
    doc.insertText(0, u"int x = 1;");

    SyntaxWorker worker(window.get());
    worker.requestParse(doc.snapshot(), Language::Cpp, doc.takePendingEdits(),
                        /*resetIncrementalState=*/true);

    // Fired immediately after the first, no pump in between - forces the
    // same race the old coalescing test exercised, but this time as a real
    // edit chain on one document rather than two unrelated documents.
    doc.insertText(doc.length(), u" int y = 2;");
    worker.requestParse(doc.snapshot(), Language::Cpp, doc.takePendingEdits(),
                        /*resetIncrementalState=*/false);

    const auto tokens = pumpForLatestTokens(5000);
    ASSERT_NE(tokens, nullptr) << "kMsgSyntaxTokensReady never arrived within the timeout";
    const auto expected = parseCpp(doc.toU16String());
    EXPECT_EQ(*tokens, expected);
}

// Phase 7l: resetIncrementalState=true must ACTUALLY discard whatever
// incremental-parse tree the worker retained for an earlier, unrelated
// document - passing edits=empty alone is NOT sufficient, since tree-sitter
// would otherwise still receive the stale tree as its reparse hint and may
// incorrectly reuse subtrees against completely unrelated text (see
// IncrementalParser::reparseDelta()'s behavior when a tree is retained). Uses the
// SAME language (Cpp) for both documents deliberately, to isolate this
// flag's own effect from the worker's separate internal language-mismatch
// safety net (which a same-language document switch never triggers) - a
// realistic stand-in for e.g. an F12 tag jump or Grep result jump landing on
// a different .cpp file.
TEST(SyntaxWorkerTest, ResetIncrementalStateDiscardsStaleTreeAcrossUnrelatedDocument) {
    HiddenWindow window;
    ASSERT_NE(window.get(), nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    SyntaxWorker worker(window.get());

    Document firstDoc;
    firstDoc.insertText(0, u"int x = 1;");
    worker.requestParse(firstDoc.snapshot(), Language::Cpp, firstDoc.takePendingEdits(),
                        /*resetIncrementalState=*/true);
    ASSERT_NE(pumpForLatestTokens(5000), nullptr) << "first parse never completed";

    Document secondDoc;
    secondDoc.insertText(0, u"double pi = 3.14;\nbool ready = true;\n");
    worker.requestParse(secondDoc.snapshot(), Language::Cpp, secondDoc.takePendingEdits(),
                        /*resetIncrementalState=*/true);

    const auto tokens = pumpForLatestTokens(5000);
    ASSERT_NE(tokens, nullptr) << "second parse never completed";
    const auto expected = parseCpp(secondDoc.toU16String());
    EXPECT_EQ(*tokens, expected);
}

}  // namespace
