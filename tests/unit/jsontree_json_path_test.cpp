#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

#include "neomifes/document/document.h"
#include "neomifes/jsontree/json_path.h"
#include "neomifes/jsontree/json_tree.h"

namespace {

using neomifes::document::Document;
using neomifes::jsontree::evaluateJsonPath;
using neomifes::jsontree::JsonNode;
using neomifes::jsontree::JsonPathExpression;
using neomifes::jsontree::JsonPathSegment;
using neomifes::jsontree::JsonPathSegmentKind;
using neomifes::jsontree::parseJsonPath;
using neomifes::jsontree::parseJsonTree;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// --- parseJsonPath ---

TEST(JsonPathParseTest, RootAloneParsesToEmptyExpression) {
    const auto result = parseJsonPath(u"$");
    ASSERT_TRUE(result.has_value());
    const JsonPathExpression& segments = *result;
    EXPECT_TRUE(segments.empty());
}

TEST(JsonPathParseTest, DotKeyChainParsesToKeySegments) {
    const auto result = parseJsonPath(u"$.users.name");
    ASSERT_TRUE(result.has_value());
    const JsonPathExpression expected{
        JsonPathSegment{.kind = JsonPathSegmentKind::Key, .key = u"users"},
        JsonPathSegment{.kind = JsonPathSegmentKind::Key, .key = u"name"},
    };
    EXPECT_EQ(*result, expected);
}

TEST(JsonPathParseTest, BracketIndexParsesToIndexSegment) {
    const auto result = parseJsonPath(u"$[0]");
    ASSERT_TRUE(result.has_value());
    const JsonPathExpression& segments = *result;
    ASSERT_EQ(segments.size(), 1U);
    EXPECT_EQ(segments[0].kind, JsonPathSegmentKind::Index);
    EXPECT_EQ(segments[0].index, 0U);
}

TEST(JsonPathParseTest, BracketQuotedKeyAcceptsSingleAndDoubleQuotes) {
    const auto singleQuoted = parseJsonPath(u"$['a b']");
    ASSERT_TRUE(singleQuoted.has_value());
    const JsonPathExpression& singleSegments = *singleQuoted;
    ASSERT_EQ(singleSegments.size(), 1U);
    EXPECT_EQ(singleSegments[0].kind, JsonPathSegmentKind::Key);
    EXPECT_EQ(singleSegments[0].key, u"a b");

    const auto doubleQuoted = parseJsonPath(u"$[\"a b\"]");
    ASSERT_TRUE(doubleQuoted.has_value());
    EXPECT_EQ(*doubleQuoted, singleSegments);
}

TEST(JsonPathParseTest, WildcardParsesToWildcardSegment) {
    const auto result = parseJsonPath(u"$[*]");
    ASSERT_TRUE(result.has_value());
    const JsonPathExpression& segments = *result;
    ASSERT_EQ(segments.size(), 1U);
    EXPECT_EQ(segments[0].kind, JsonPathSegmentKind::Wildcard);
}

TEST(JsonPathParseTest, MixedChainParsesInOrder) {
    const auto result = parseJsonPath(u"$.users[0].name");
    ASSERT_TRUE(result.has_value());
    const JsonPathExpression expected{
        JsonPathSegment{.kind = JsonPathSegmentKind::Key, .key = u"users"},
        JsonPathSegment{.kind = JsonPathSegmentKind::Index, .index = 0},
        JsonPathSegment{.kind = JsonPathSegmentKind::Key, .key = u"name"},
    };
    EXPECT_EQ(*result, expected);
}

TEST(JsonPathParseTest, EmptyInputIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"").has_value());
}

TEST(JsonPathParseTest, MissingLeadingDollarIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"users.name").has_value());
}

TEST(JsonPathParseTest, RecursiveDescentIsUnsupportedAndRejected) {
    EXPECT_FALSE(parseJsonPath(u"$..name").has_value());
}

TEST(JsonPathParseTest, TrailingDotWithNoKeyIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"$.").has_value());
}

TEST(JsonPathParseTest, UnterminatedBracketIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"$[0").has_value());
}

TEST(JsonPathParseTest, UnterminatedQuoteIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"$['a]").has_value());
}

TEST(JsonPathParseTest, NonDigitNonQuotedBracketContentIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"$[abc]").has_value());
}

TEST(JsonPathParseTest, EmptyBracketIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"$[]").has_value());
}

TEST(JsonPathParseTest, StrayCharacterOutsideSegmentIsRejected) {
    EXPECT_FALSE(parseJsonPath(u"$ .a").has_value());
    EXPECT_FALSE(parseJsonPath(u"$x").has_value());
}

// --- evaluateJsonPath ---

constexpr std::u16string_view kSampleJson =
    u"{\"users\":[{\"name\":\"Alice\",\"age\":30},{\"name\":\"Bob\",\"age\":25}],\"count\":2}";

[[nodiscard]] JsonNode parseSample() {
    const Document doc  = makeDoc(kSampleJson);
    auto            tree = parseJsonTree(doc);
    if (!tree.has_value()) {
        ADD_FAILURE() << "kSampleJson unexpectedly failed to parse as JSON";
        return JsonNode{};
    }
    return std::move(*tree);
}

TEST(JsonPathEvaluateTest, EmptyExpressionMatchesRootOnly) {
    const JsonNode root = parseSample();
    const auto matches = evaluateJsonPath(root, JsonPathExpression{});
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0], &root);
}

TEST(JsonPathEvaluateTest, DotKeyThenIndexThenKeyResolvesToSingleLeaf) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$.users[0].name");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateJsonPath(root, *expr);
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0]->text, u"\"Alice\"");
}

TEST(JsonPathEvaluateTest, WildcardThenKeyFansOutAcrossArrayElements) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$.users[*].name");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateJsonPath(root, *expr);
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0]->text, u"\"Alice\"");
    EXPECT_EQ(matches[1]->text, u"\"Bob\"");
}

TEST(JsonPathEvaluateTest, BracketQuotedKeyMatchesSameAsDotKey) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$['count']");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateJsonPath(root, *expr);
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0]->text, u"2");
}

TEST(JsonPathEvaluateTest, OutOfRangeIndexYieldsNoMatches) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$.users[5]");
    ASSERT_TRUE(expr.has_value());
    EXPECT_TRUE(evaluateJsonPath(root, *expr).empty());
}

TEST(JsonPathEvaluateTest, MissingKeyYieldsNoMatches) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$.missing");
    ASSERT_TRUE(expr.has_value());
    EXPECT_TRUE(evaluateJsonPath(root, *expr).empty());
}

TEST(JsonPathEvaluateTest, IndexOnObjectYieldsNoMatches) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$[0]");  // root is an Object, not an Array
    ASSERT_TRUE(expr.has_value());
    EXPECT_TRUE(evaluateJsonPath(root, *expr).empty());
}

TEST(JsonPathEvaluateTest, DotKeyOnArrayYieldsNoMatches) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$.users.name");  // users is an Array, not an Object
    ASSERT_TRUE(expr.has_value());
    EXPECT_TRUE(evaluateJsonPath(root, *expr).empty());
}

TEST(JsonPathEvaluateTest, RootWildcardMatchesAllTopLevelMembers) {
    const JsonNode root = parseSample();
    const auto      expr = parseJsonPath(u"$[*]");
    ASSERT_TRUE(expr.has_value());
    const auto matches = evaluateJsonPath(root, *expr);
    ASSERT_EQ(matches.size(), 2U);  // "users" and "count"
}

}  // namespace
