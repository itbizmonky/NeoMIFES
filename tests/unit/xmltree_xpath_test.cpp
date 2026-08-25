#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

#include "neomifes/document/document.h"
#include "neomifes/xmltree/xml_tree.h"
#include "neomifes/xmltree/xpath.h"

namespace {

using neomifes::document::Document;
using neomifes::xmltree::evaluateXPath;
using neomifes::xmltree::parseXmlTree;
using neomifes::xmltree::parseXPath;
using neomifes::xmltree::XmlNode;
using neomifes::xmltree::XmlNodeKind;
using neomifes::xmltree::XPathExpression;
using neomifes::xmltree::XPathSegment;
using neomifes::xmltree::XPathSegmentKind;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// --- parseXPath ---

TEST(XPathParseTest, RootAloneParsesToEmptyExpression) {
    const auto result = parseXPath(u"/");
    ASSERT_TRUE(result.has_value());
    const XPathExpression& segments = *result;
    EXPECT_TRUE(segments.empty());
}

TEST(XPathParseTest, TagChainParsesToTagNameSegments) {
    const auto result = parseXPath(u"/book/title");
    ASSERT_TRUE(result.has_value());
    const XPathExpression expected{
        XPathSegment{.kind = XPathSegmentKind::TagName, .tagName = u"book"},
        XPathSegment{.kind = XPathSegmentKind::TagName, .tagName = u"title"},
    };
    EXPECT_EQ(*result, expected);
}

TEST(XPathParseTest, BracketIndexParsesToIndexOnTagNameSegment) {
    const auto result = parseXPath(u"/book[2]");
    ASSERT_TRUE(result.has_value());
    const XPathExpression& segments = *result;
    ASSERT_EQ(segments.size(), 1U);
    EXPECT_EQ(segments[0].kind, XPathSegmentKind::TagName);
    EXPECT_EQ(segments[0].tagName, u"book");
    EXPECT_EQ(segments[0].index, 2U);
}

TEST(XPathParseTest, WildcardParsesToWildcardSegment) {
    const auto result = parseXPath(u"/*");
    ASSERT_TRUE(result.has_value());
    const XPathExpression& segments = *result;
    ASSERT_EQ(segments.size(), 1U);
    EXPECT_EQ(segments[0].kind, XPathSegmentKind::Wildcard);
    EXPECT_EQ(segments[0].index, 0U);
}

TEST(XPathParseTest, WildcardWithIndexParsesToIndexOnWildcardSegment) {
    const auto result = parseXPath(u"/*[3]");
    ASSERT_TRUE(result.has_value());
    const XPathExpression& segments = *result;
    ASSERT_EQ(segments.size(), 1U);
    EXPECT_EQ(segments[0].kind, XPathSegmentKind::Wildcard);
    EXPECT_EQ(segments[0].index, 3U);
}

TEST(XPathParseTest, MixedChainParsesInOrder) {
    const auto result = parseXPath(u"/a/*/c[1]");
    ASSERT_TRUE(result.has_value());
    const XPathExpression expected{
        XPathSegment{.kind = XPathSegmentKind::TagName, .tagName = u"a"},
        XPathSegment{.kind = XPathSegmentKind::Wildcard},
        XPathSegment{.kind = XPathSegmentKind::TagName, .tagName = u"c", .index = 1},
    };
    EXPECT_EQ(*result, expected);
}

TEST(XPathParseTest, EmptyInputIsRejected) {
    EXPECT_FALSE(parseXPath(u"").has_value());
}

TEST(XPathParseTest, MissingLeadingSlashIsRejected) {
    EXPECT_FALSE(parseXPath(u"book/title").has_value());
}

TEST(XPathParseTest, DescendantAxisIsUnsupportedAndRejected) {
    EXPECT_FALSE(parseXPath(u"//book").has_value());
}

TEST(XPathParseTest, TrailingSlashWithNoStepIsRejected) {
    EXPECT_FALSE(parseXPath(u"/book/").has_value());
}

TEST(XPathParseTest, UnterminatedBracketIsRejected) {
    EXPECT_FALSE(parseXPath(u"/book[1").has_value());
}

TEST(XPathParseTest, NonDigitBracketContentIsRejected) {
    EXPECT_FALSE(parseXPath(u"/book[x]").has_value());
}

TEST(XPathParseTest, EmptyBracketIsRejected) {
    EXPECT_FALSE(parseXPath(u"/book[]").has_value());
}

TEST(XPathParseTest, ZeroIndexIsRejected) {
    EXPECT_FALSE(parseXPath(u"/book[0]").has_value());
}

TEST(XPathParseTest, StrayCharacterAfterPredicateIsRejected) {
    EXPECT_FALSE(parseXPath(u"/book[1]x").has_value());
}

// --- evaluateXPath ---

constexpr std::u16string_view kSampleXml =
    u"<catalog>\n"
    u"  <book id=\"1\"><title>Learning XML</title></book>\n"
    u"  <book id=\"2\"><title>Effective XML</title></book>\n"
    u"  <magazine><title>Wired</title></magazine>\n"
    u"</catalog>";

[[nodiscard]] XmlNode parseSample() {
    const Document doc  = makeDoc(kSampleXml);
    auto            tree = parseXmlTree(doc);
    if (tree.hasErrors) {
        ADD_FAILURE() << "kSampleXml unexpectedly failed to parse as XML";
    }
    return std::move(tree.root);
}

TEST(XPathEvaluateTest, EmptyExpressionMatchesRootOnly) {
    const XmlNode root    = parseSample();
    const auto     matches = evaluateXPath(root, XPathExpression{});
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0], &root);
}

TEST(XPathEvaluateTest, TagStepFansOutAcrossMatchingSiblings) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/book");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateXPath(root, *expr);
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0]->tagName, u"book");
    EXPECT_EQ(matches[1]->tagName, u"book");
}

TEST(XPathEvaluateTest, TagChainResolvesNestedElementsInEachMatchingParent) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/book/title");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateXPath(root, *expr);
    ASSERT_EQ(matches.size(), 2U);
    ASSERT_EQ(matches[0]->children.size(), 1U);
    EXPECT_EQ(matches[0]->children[0].text, u"Learning XML");
    ASSERT_EQ(matches[1]->children.size(), 1U);
    EXPECT_EQ(matches[1]->children[0].text, u"Effective XML");
}

TEST(XPathEvaluateTest, IndexPredicateSelectsNthMatchingSibling) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/book[2]");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateXPath(root, *expr);
    ASSERT_EQ(matches.size(), 1U);
    ASSERT_EQ(matches[0]->attributes.size(), 1U);
    EXPECT_EQ(matches[0]->attributes[0].value, u"\"2\"");
}

TEST(XPathEvaluateTest, WildcardMatchesEveryElementChildRegardlessOfTagName) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/*");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateXPath(root, *expr);
    // book, book, magazine - the whitespace Text nodes between them (this
    // sample is pretty-printed) must never be candidates.
    ASSERT_EQ(matches.size(), 3U);
    EXPECT_EQ(matches[0]->tagName, u"book");
    EXPECT_EQ(matches[1]->tagName, u"book");
    EXPECT_EQ(matches[2]->tagName, u"magazine");
}

TEST(XPathEvaluateTest, WildcardWithIndexSelectsNthChildOverall) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/*[3]");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateXPath(root, *expr);
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0]->tagName, u"magazine");
}

TEST(XPathEvaluateTest, IndexPredicateIsComputedIndependentlyPerFannedOutParent) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/book/*[1]");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateXPath(root, *expr);
    // Each of the 2 <book> elements has exactly one child (<title>) - the
    // [1] predicate must resolve to "first child of THIS book", independently
    // for each fanned-out <book>, not "first of some combined global list".
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0]->tagName, u"title");
    EXPECT_EQ(matches[1]->tagName, u"title");
}

TEST(XPathEvaluateTest, OutOfRangeIndexYieldsNoMatches) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/book[5]");
    ASSERT_TRUE(expr.has_value());
    EXPECT_TRUE(evaluateXPath(root, *expr).empty());
}

TEST(XPathEvaluateTest, NonExistentTagYieldsNoMatches) {
    const XmlNode root  = parseSample();
    const auto     expr = parseXPath(u"/chapter");
    ASSERT_TRUE(expr.has_value());
    EXPECT_TRUE(evaluateXPath(root, *expr).empty());
}

TEST(XPathEvaluateTest, TagStepNeverMatchesNonElementChildren) {
    const XmlNode leafText{.kind = XmlNodeKind::Text, .text = u"hi"};
    const XmlNode root{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"root",
        .selfClosing = false,
        .children    = {leafText},
    };
    const auto expr = parseXPath(u"/text");
    ASSERT_TRUE(expr.has_value());
    EXPECT_TRUE(evaluateXPath(root, *expr).empty());
    const auto wildcardExpr = parseXPath(u"/*");
    ASSERT_TRUE(wildcardExpr.has_value());
    EXPECT_TRUE(evaluateXPath(root, *wildcardExpr).empty());
}

}  // namespace
