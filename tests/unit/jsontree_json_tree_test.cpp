#include <gtest/gtest.h>

#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/jsontree/json_tree.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::jsontree::JsonNode;
using neomifes::jsontree::JsonNodeKind;
using neomifes::jsontree::parseJsonTree;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// --- Structural correctness ---------------------------------------------

TEST(JsonTreeTest, NestedObjectArrayScalarMixHasExpectedShape) {
    const Document doc = makeDoc(u"{\"a\":1,\"b\":[true,null,\"x\"]}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());

    EXPECT_EQ(tree->kind, JsonNodeKind::Object);
    EXPECT_FALSE(tree->key.has_value());
    ASSERT_EQ(tree->children.size(), 2U);

    const JsonNode& a = tree->children[0];
    EXPECT_EQ(a.kind, JsonNodeKind::Number);
    ASSERT_TRUE(a.key.has_value());
    EXPECT_EQ(*a.key, u"a");
    EXPECT_EQ(a.text, u"1");
    EXPECT_TRUE(a.children.empty());

    const JsonNode& b = tree->children[1];
    EXPECT_EQ(b.kind, JsonNodeKind::Array);
    ASSERT_TRUE(b.key.has_value());
    EXPECT_EQ(*b.key, u"b");
    ASSERT_EQ(b.children.size(), 3U);

    EXPECT_EQ(b.children[0].kind, JsonNodeKind::Boolean);
    EXPECT_FALSE(b.children[0].key.has_value());
    EXPECT_EQ(b.children[0].text, u"true");

    EXPECT_EQ(b.children[1].kind, JsonNodeKind::Null);
    EXPECT_FALSE(b.children[1].key.has_value());
    EXPECT_EQ(b.children[1].text, u"null");

    EXPECT_EQ(b.children[2].kind, JsonNodeKind::String);
    EXPECT_FALSE(b.children[2].key.has_value());
    // Leaf text is raw source, quotes included - see json_tree.h.
    EXPECT_EQ(b.children[2].text, u"\"x\"");
}

TEST(JsonTreeTest, DeeplyNestedObjectTracksDepth) {
    const Document doc  = makeDoc(u"{\"a\":{\"b\":{\"c\":1}}}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());

    ASSERT_EQ(tree->children.size(), 1U);
    const JsonNode& a = tree->children[0];
    EXPECT_EQ(a.kind, JsonNodeKind::Object);
    ASSERT_EQ(a.children.size(), 1U);
    const JsonNode& b = a.children[0];
    EXPECT_EQ(b.kind, JsonNodeKind::Object);
    ASSERT_EQ(b.children.size(), 1U);
    const JsonNode& c = b.children[0];
    EXPECT_EQ(c.kind, JsonNodeKind::Number);
    EXPECT_EQ(c.text, u"1");
    EXPECT_TRUE(c.children.empty());
}

TEST(JsonTreeTest, EmptyObjectAndArrayHaveNoChildren) {
    const Document doc  = makeDoc(u"{\"o\":{},\"a\":[]}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    ASSERT_EQ(tree->children.size(), 2U);
    EXPECT_EQ(tree->children[0].kind, JsonNodeKind::Object);
    EXPECT_TRUE(tree->children[0].children.empty());
    EXPECT_EQ(tree->children[1].kind, JsonNodeKind::Array);
    EXPECT_TRUE(tree->children[1].children.empty());
}

TEST(JsonTreeTest, BareScalarRootIsSupported) {
    const Document doc  = makeDoc(u"42");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(tree->kind, JsonNodeKind::Number);
    EXPECT_EQ(tree->text, u"42");
    EXPECT_FALSE(tree->key.has_value());
    EXPECT_TRUE(tree->children.empty());
}

// --- Key order preservation ----------------------------------------------

TEST(JsonTreeTest, ObjectKeysKeepSourceOrderNotAlphabetical) {
    // A std::map-backed DOM (the default nlohmann::json) would re-sort this
    // to a, m, z - the whole point of using nlohmann::ordered_json.
    const Document doc  = makeDoc(u"{\"z\":1,\"a\":2,\"m\":3}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    ASSERT_EQ(tree->children.size(), 3U);
    ASSERT_TRUE(tree->children[0].key.has_value());
    ASSERT_TRUE(tree->children[1].key.has_value());
    ASSERT_TRUE(tree->children[2].key.has_value());
    EXPECT_EQ(*tree->children[0].key, u"z");
    EXPECT_EQ(*tree->children[1].key, u"a");
    EXPECT_EQ(*tree->children[2].key, u"m");
}

// --- Position accuracy -----------------------------------------------------

TEST(JsonTreeTest, SimpleObjectMemberPositionsSpanKeyToValueEnd) {
    // {"k":5}
    //  0123456
    const Document doc  = makeDoc(u"{\"k\":5}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(tree->startPos, 0U);
    EXPECT_EQ(tree->endPos, 7U);
    ASSERT_EQ(tree->children.size(), 1U);
    const JsonNode& k = tree->children[0];
    // Member span starts at the key's opening quote (index 1), not the
    // value's own start (index 5) - see json_tree.h's startPos/endPos
    // comment.
    EXPECT_EQ(k.startPos, 1U);
    EXPECT_EQ(k.endPos, 6U);
    EXPECT_EQ(k.text, u"5");
}

TEST(JsonTreeTest, EscapedQuoteInKeyDecodesCorrectlyAndPositionsSpanTheWholeMember) {
    // {"a\"b":1}
    // 0123456789
    const Document doc  = makeDoc(u"{\"a\\\"b\":1}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(tree->startPos, 0U);
    EXPECT_EQ(tree->endPos, 10U);
    ASSERT_EQ(tree->children.size(), 1U);
    const JsonNode& member = tree->children[0];
    ASSERT_TRUE(member.key.has_value());
    EXPECT_EQ(*member.key, u"a\"b");
    EXPECT_EQ(member.startPos, 1U);
    EXPECT_EQ(member.endPos, 9U);
    EXPECT_EQ(member.text, u"1");
}

TEST(JsonTreeTest, NonAsciiStringValuePositionsAccountForBmpCharacters) {
    // {"s":"日本語"}
    // 0123456789 10
    const Document doc  = makeDoc(u"{\"s\":\"日本語\"}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(tree->startPos, 0U);
    EXPECT_EQ(tree->endPos, 11U);
    ASSERT_EQ(tree->children.size(), 1U);
    const JsonNode& member = tree->children[0];
    ASSERT_TRUE(member.key.has_value());
    EXPECT_EQ(*member.key, u"s");
    EXPECT_EQ(member.startPos, 1U);
    EXPECT_EQ(member.endPos, 10U);
    EXPECT_EQ(member.kind, JsonNodeKind::String);
    EXPECT_EQ(member.text, u"\"日本語\"");
}

TEST(JsonTreeTest, NestedObjectMemberSpansFromItsOwnKeyNotTheOuterKey) {
    // {"a":{"b":2}}
    // 0123456789012
    const Document doc  = makeDoc(u"{\"a\":{\"b\":2}}");
    const auto      tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    ASSERT_EQ(tree->children.size(), 1U);
    const JsonNode& a = tree->children[0];
    EXPECT_EQ(a.kind, JsonNodeKind::Object);
    EXPECT_EQ(a.startPos, 1U);   // "a" member starts at its own key's quote
    // Ends where its VALUE (the nested object) ends - the nested object's
    // own closing '}' is at index 11, one past it is 12. The OUTER '}' at
    // index 12 belongs to the root, not to member "a".
    EXPECT_EQ(a.endPos, 12U);
    ASSERT_EQ(a.children.size(), 1U);
    const JsonNode& b = a.children[0];
    EXPECT_EQ(b.startPos, 6U);  // "b" member starts at ITS OWN key's quote
    EXPECT_EQ(b.endPos, 11U);
    EXPECT_EQ(b.text, u"2");
}

// --- Malformed input -------------------------------------------------------

TEST(JsonTreeTest, MissingClosingBraceReturnsNullopt) {
    const Document doc = makeDoc(u"{\"a\":1");
    EXPECT_FALSE(parseJsonTree(doc).has_value());
}

TEST(JsonTreeTest, MissingColonReturnsNullopt) {
    const Document doc = makeDoc(u"{\"a\" 1}");
    EXPECT_FALSE(parseJsonTree(doc).has_value());
}

TEST(JsonTreeTest, TrailingGarbageReturnsNullopt) {
    const Document doc = makeDoc(u"{\"a\":1} garbage");
    EXPECT_FALSE(parseJsonTree(doc).has_value());
}

TEST(JsonTreeTest, EmptyDocumentReturnsNullopt) {
    const Document doc = makeDoc(u"");
    EXPECT_FALSE(parseJsonTree(doc).has_value());
}

TEST(JsonTreeTest, WhitespaceOnlyDocumentReturnsNullopt) {
    const Document doc = makeDoc(u"   \n\t  ");
    EXPECT_FALSE(parseJsonTree(doc).has_value());
}

// --- BufferSnapshot overload (WI-15b) --------------------------------------

TEST(JsonTreeTest, ParseJsonTreeFromBufferSnapshotMatchesDocumentOverload) {
    // The Document-taking parseJsonTree() is a one-line delegation to the
    // BufferSnapshot-taking one (WI-15b) - confirm both entry points produce
    // identical trees for the same document.
    const Document doc = makeDoc(u"{\"a\":1,\"b\":[true,null,\"x\"]}");
    const auto     viaDocument = parseJsonTree(doc);
    const auto     viaSnapshot = parseJsonTree(*doc.snapshot());
    ASSERT_TRUE(viaDocument.has_value());
    ASSERT_TRUE(viaSnapshot.has_value());
    EXPECT_EQ(*viaDocument, *viaSnapshot);
}

TEST(JsonTreeTest, ParseJsonTreeAcrossMultiplePiecesMatchesSinglePieceResult) {
    // Forces the document below to span multiple pieces (not just the one
    // insertText() call every other test in this file uses via makeDoc()) -
    // parseJsonTree() concatenates snapshot->pieces() into one buffer before
    // parsing, so this is a regression guard against that concatenation
    // silently going wrong at a piece boundary. Mirrors
    // LogModelTest.LineContentSpanningMultiplePiecesMatchesCorrectly
    // (logmode_log_model_test.cpp).
    const std::u16string text = u"{\"name\":\"Alice\",\"age\":30}";
    Document              doc;
    doc.insertText(0, text);
    constexpr std::uint64_t kSplitPos = 12;  // inside "Alice"
    doc.insertText(kSplitPos, u"XXXX");
    doc.eraseRange(TextRange{.start = kSplitPos, .end = kSplitPos + 4});

    // Precondition check: confirm this test actually exercises the
    // multi-piece path rather than silently degrading to a single piece.
    ASSERT_GT(doc.snapshot()->pieces().size(), 1U);

    const auto tree = parseJsonTree(doc);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(tree->kind, JsonNodeKind::Object);
    ASSERT_EQ(tree->children.size(), 2U);
    ASSERT_TRUE(tree->children[0].key.has_value());
    EXPECT_EQ(*tree->children[0].key, u"name");
    EXPECT_EQ(tree->children[0].text, u"\"Alice\"");
    ASSERT_TRUE(tree->children[1].key.has_value());
    EXPECT_EQ(*tree->children[1].key, u"age");
    EXPECT_EQ(tree->children[1].text, u"30");
}

}  // namespace
