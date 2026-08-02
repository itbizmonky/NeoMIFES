#include "neomifes/app/plugin_core_api_bridge.h"

#include <gtest/gtest.h>

#include <array>

#include "neomifes/document/document.h"

namespace neomifes::app {
namespace {

using document::Document;

TEST(PluginCoreApiBridgeTest, InsertTextInsertsAtTheGivenLineAndColumn) {
    Document doc;
    doc.insertText(0, u"line0\nline1");
    NeoMifesDocument* handle = toNeoMifesDocument(doc);

    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->insertText(handle, L"X", 1, 0);  // "Xline1"
    EXPECT_EQ(doc.toU16String(), u"line0\nXline1");
}

TEST(PluginCoreApiBridgeTest, InsertTextWithOutOfRangeLineClampsToTheLastLinesStart) {
    // Document::lineColumnToOffset()'s comment: an out-of-range line
    // inherits lineToOffset()'s own clamp (the LAST LINE'S START), which is
    // different from an out-of-range column's clamp (the document's own
    // end) - see the next test.
    Document doc;
    doc.insertText(0, u"line0\nline1");

    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->insertText(toNeoMifesDocument(doc), L"X", 9999, 0);
    EXPECT_EQ(doc.toU16String(), u"line0\nXline1");
}

TEST(PluginCoreApiBridgeTest, InsertTextWithOutOfRangeColumnClampsToDocumentEndNotLineEnd) {
    // Deliberate deviation documented in Document::lineColumnToOffset()'s
    // comment: a huge column on an early line spills to the document's own
    // end, not that line's own end.
    Document doc;
    doc.insertText(0, u"line0\nline1");

    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->insertText(toNeoMifesDocument(doc), L"X", 0, 9999);
    EXPECT_EQ(doc.toU16String(), u"line0\nline1X");
}

TEST(PluginCoreApiBridgeTest, InsertTextIsANoOpWhenDocIsNull) {
    // A crash here (null deref) would abort the whole test binary - simply
    // completing this test IS the proof, same convention as this
    // codebase's other "...DoesNotCrash..." tests (see syntax_syntax_test.cpp).
    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->insertText(nullptr, L"X", 0, 0);
}

TEST(PluginCoreApiBridgeTest, InsertTextIsANoOpWhenTextIsNull) {
    Document doc;
    doc.insertText(0, u"hello");
    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->insertText(toNeoMifesDocument(doc), nullptr, 0, 0);
    EXPECT_EQ(doc.toU16String(), u"hello");
}

TEST(PluginCoreApiBridgeTest, DeleteRangeDeletesTheGivenSpan) {
    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");

    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->deleteRange(toNeoMifesDocument(doc), 0, 0, 1, 0);  // "line0\n"
    EXPECT_EQ(doc.toU16String(), u"line1\nline2");
}

TEST(PluginCoreApiBridgeTest, DeleteRangeWithSwappedEndpointsStillDeletesTheIntendedSpan) {
    // A plugin's line/column input is untrusted - see this file's own
    // comment on the bridge normalizing a resolved end-before-start pair.
    // Contrast with document::Document::eraseRange() itself, which
    // silently no-ops on an inverted TextRange (PieceTree::eraseRange()'s
    // start>=end guard) - the bridge exists precisely to avoid that trap.
    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");

    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->deleteRange(toNeoMifesDocument(doc), /*lineStart=*/1, /*columnStart=*/0,
                                       /*lineEnd=*/0, /*columnEnd=*/0);  // swapped: still "line0\n"
    EXPECT_EQ(doc.toU16String(), u"line1\nline2");
}

TEST(PluginCoreApiBridgeTest, DeleteRangeIsANoOpWhenDocIsNull) {
    buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->deleteRange(nullptr, 0, 0, 0, 0);
}

TEST(PluginCoreApiBridgeTest, GetLineCountReturnsTheDocumentsLineCount) {
    Document doc;
    doc.insertText(0, u"line0\nline1\nline2");
    EXPECT_EQ(buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineCount(toNeoMifesDocument(doc)), 3u);
}

TEST(PluginCoreApiBridgeTest, GetLineCountReturnsZeroWhenDocIsNull) {
    EXPECT_EQ(buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineCount(nullptr), 0u);
}

TEST(PluginCoreApiBridgeTest, GetLineTextCopiesALineThatFitsExactly) {
    Document doc;
    doc.insertText(0, u"hello");
    std::array<wchar_t, 6> buffer{};  // exactly "hello" + '\0'

    const unsigned written = buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineText(
        toNeoMifesDocument(doc), 0, buffer.data(), static_cast<unsigned>(buffer.size()));
    EXPECT_EQ(written, 5u);
    EXPECT_STREQ(buffer.data(), L"hello");
}

TEST(PluginCoreApiBridgeTest, GetLineTextTruncatesAndStillNullTerminates) {
    Document doc;
    doc.insertText(0, u"hello world");
    std::array<wchar_t, 4> buffer{};  // room for 3 chars + '\0'

    const unsigned written = buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineText(
        toNeoMifesDocument(doc), 0, buffer.data(), static_cast<unsigned>(buffer.size()));
    EXPECT_EQ(written, 3u);
    EXPECT_STREQ(buffer.data(), L"hel");
}

TEST(PluginCoreApiBridgeTest, GetLineTextWithBufferLenOneWritesOnlyTheTerminator) {
    Document doc;
    doc.insertText(0, u"hello");
    std::array<wchar_t, 1> buffer{L'?'};

    const unsigned written =
        buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineText(toNeoMifesDocument(doc), 0, buffer.data(), 1);
    EXPECT_EQ(written, 0u);
    EXPECT_EQ(buffer[0], L'\0');
}

TEST(PluginCoreApiBridgeTest, GetLineTextReturnsZeroWhenBufferIsNull) {
    Document doc;
    doc.insertText(0, u"hello");
    EXPECT_EQ(buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineText(toNeoMifesDocument(doc), 0, nullptr, 16), 0u);
}

TEST(PluginCoreApiBridgeTest, GetLineTextReturnsZeroWhenBufferLenIsZero) {
    Document doc;
    doc.insertText(0, u"hello");
    std::array<wchar_t, 4> buffer{L'?', L'?', L'?', L'?'};
    EXPECT_EQ(buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineText(toNeoMifesDocument(doc), 0, buffer.data(), 0), 0u);
}

TEST(PluginCoreApiBridgeTest, GetLineTextReturnsZeroWhenDocIsNull) {
    std::array<wchar_t, 16> buffer{};
    EXPECT_EQ(buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineText(nullptr, 0, buffer.data(),
                                                 static_cast<unsigned>(buffer.size())),
              0u);
}

TEST(PluginCoreApiBridgeTest, GetLineTextOnEmptyDocumentReturnsEmptyString) {
    Document doc;
    std::array<wchar_t, 16> buffer{L'?'};

    const unsigned written = buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)->getLineText(
        toNeoMifesDocument(doc), 0, buffer.data(), static_cast<unsigned>(buffer.size()));
    EXPECT_EQ(written, 0u);
    EXPECT_EQ(buffer[0], L'\0');
}

TEST(PluginCoreApiBridgeTest,
     BuildPluginCoreApiWithoutDocumentPermissionReturnsAllNullFunctionPointersButValidApiVersion) {
    const NeoMifesCoreApi* api = buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_NONE);
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->apiVersion, NEOMIFES_CORE_API_VERSION);
    EXPECT_EQ(api->insertText, nullptr);
    EXPECT_EQ(api->deleteRange, nullptr);
    EXPECT_EQ(api->getLineCount, nullptr);
    EXPECT_EQ(api->getLineText, nullptr);
}

TEST(PluginCoreApiBridgeTest, BuildPluginCoreApiWithUnrelatedPermissionBitsStillDeniesDocumentAccess) {
    // NEOMIFES_PLUGIN_PERMISSION_NETWORK doesn't include the DOCUMENT bit -
    // verifies the gate is a bitwise AND, not e.g. "any permission granted".
    const NeoMifesCoreApi* api = buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_NETWORK);
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->apiVersion, NEOMIFES_CORE_API_VERSION);
    EXPECT_EQ(api->insertText, nullptr);
}

}  // namespace
}  // namespace neomifes::app
