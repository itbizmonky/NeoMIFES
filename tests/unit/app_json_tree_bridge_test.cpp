#include <gtest/gtest.h>

#include "neomifes/app/json_tree_bridge.h"

namespace {

using neomifes::app::buildJsonTreeItems;
using neomifes::jsontree::JsonNode;
using neomifes::jsontree::JsonNodeKind;
using neomifes::ui::OutlineItem;

TEST(AppJsonTreeBridgeTest, LeafRootWithNoKeyUsesRawTextAsLabel) {
    const JsonNode node{
        .kind     = JsonNodeKind::Number,
        .key      = std::nullopt,
        .text     = u"42",
        .startPos = 0,
        .endPos   = 2,
        .children = {},
    };

    const OutlineItem item = buildJsonTreeItems(node);
    EXPECT_EQ(item.name, u"42");
    EXPECT_EQ(item.targetPos, 0u);
    EXPECT_TRUE(item.children.empty());
}

TEST(AppJsonTreeBridgeTest, LeafWithKeyPrefixesLabelWithKey) {
    const JsonNode node{
        .kind     = JsonNodeKind::String,
        .key      = u"name",
        .text     = u"\"Alice\"",
        .startPos = 5,
        .endPos   = 20,
        .children = {},
    };

    const OutlineItem item = buildJsonTreeItems(node);
    EXPECT_EQ(item.name, u"name: \"Alice\"");
    EXPECT_EQ(item.targetPos, 5u);
}

TEST(AppJsonTreeBridgeTest, EmptyObjectRootHasZeroCountLabelAndNoChildren) {
    const JsonNode node{
        .kind     = JsonNodeKind::Object,
        .key      = std::nullopt,
        .text     = u"",
        .startPos = 0,
        .endPos   = 2,
        .children = {},
    };

    const OutlineItem item = buildJsonTreeItems(node);
    EXPECT_EQ(item.name, u"{0}");
    EXPECT_TRUE(item.children.empty());
}

TEST(AppJsonTreeBridgeTest, ObjectWithKeyAndChildrenUsesBraceCountLabel) {
    const JsonNode leaf{
        .kind = JsonNodeKind::Null, .key = u"x", .text = u"null", .startPos = 10, .endPos = 14, .children = {}};
    const JsonNode node{
        .kind     = JsonNodeKind::Object,
        .key      = u"config",
        .text     = u"",
        .startPos = 1,
        .endPos   = 20,
        .children = {leaf},
    };

    const OutlineItem item = buildJsonTreeItems(node);
    EXPECT_EQ(item.name, u"config: {1}");
    ASSERT_EQ(item.children.size(), 1u);
    EXPECT_EQ(item.children[0].name, u"x: null");
    EXPECT_EQ(item.children[0].targetPos, 10u);
}

TEST(AppJsonTreeBridgeTest, ArrayWithoutKeyUsesBracketCountLabel) {
    const JsonNode elem0{
        .kind = JsonNodeKind::Boolean, .key = std::nullopt, .text = u"true", .startPos = 1, .endPos = 5, .children = {}};
    const JsonNode elem1{
        .kind = JsonNodeKind::Boolean, .key = std::nullopt, .text = u"false", .startPos = 7, .endPos = 12, .children = {}};
    const JsonNode node{
        .kind     = JsonNodeKind::Array,
        .key      = std::nullopt,
        .text     = u"",
        .startPos = 0,
        .endPos   = 13,
        .children = {elem0, elem1},
    };

    const OutlineItem item = buildJsonTreeItems(node);
    EXPECT_EQ(item.name, u"[2]");
    ASSERT_EQ(item.children.size(), 2u);
    EXPECT_EQ(item.children[0].name, u"true");
    EXPECT_EQ(item.children[1].name, u"false");
}

TEST(AppJsonTreeBridgeTest, NestedTreeMapsAcrossThreeLevelsPreservingSiblingOrder) {
    const JsonNode innerLeaf{
        .kind = JsonNodeKind::Number, .key = u"c", .text = u"1", .startPos = 40, .endPos = 41, .children = {}};
    const JsonNode inner{
        .kind     = JsonNodeKind::Object,
        .key      = u"b",
        .text     = u"",
        .startPos = 20,
        .endPos   = 45,
        .children = {innerLeaf},
    };
    const JsonNode sibling{
        .kind = JsonNodeKind::String, .key = u"d", .text = u"\"x\"", .startPos = 50, .endPos = 53, .children = {}};
    const JsonNode root{
        .kind     = JsonNodeKind::Object,
        .key      = std::nullopt,
        .text     = u"",
        .startPos = 0,
        .endPos   = 60,
        .children = {inner, sibling},
    };

    const OutlineItem item = buildJsonTreeItems(root);
    EXPECT_EQ(item.name, u"{2}");
    ASSERT_EQ(item.children.size(), 2u);

    EXPECT_EQ(item.children[0].name, u"b: {1}");
    EXPECT_EQ(item.children[0].targetPos, 20u);
    ASSERT_EQ(item.children[0].children.size(), 1u);
    EXPECT_EQ(item.children[0].children[0].name, u"c: 1");
    EXPECT_EQ(item.children[0].children[0].targetPos, 40u);
    EXPECT_TRUE(item.children[0].children[0].children.empty());

    EXPECT_EQ(item.children[1].name, u"d: \"x\"");
    EXPECT_EQ(item.children[1].targetPos, 50u);
}

}  // namespace
