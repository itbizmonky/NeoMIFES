#include <gtest/gtest.h>

#include <vector>

#include "neomifes/app/outline_bridge.h"

namespace {

using neomifes::app::buildOutlineItems;
using neomifes::syntax::OutlineNode;
using neomifes::syntax::SymbolKind;
using neomifes::ui::OutlineItem;

TEST(AppOutlineBridgeTest, EmptyVectorProducesEmptyVector) {
    EXPECT_TRUE(buildOutlineItems({}).empty());
}

TEST(AppOutlineBridgeTest, SingleNodeWithNoChildrenMapsNameAndTargetPos) {
    const std::vector<OutlineNode> nodes{OutlineNode{
        .name             = u"foo",
        .pos              = 42u,
        .containingRange   = {.start = 0u, .end = 100u},
        .symbolKind        = SymbolKind::Function,
        .children          = {},
    }};

    const std::vector<OutlineItem> items = buildOutlineItems(nodes);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].name, u"foo");
    EXPECT_EQ(items[0].targetPos, 42u);
    EXPECT_TRUE(items[0].children.empty());
}

TEST(AppOutlineBridgeTest, NestedNodesMapRecursivelyAcrossThreeLevels) {
    const std::vector<OutlineNode> nodes{OutlineNode{
        .name             = u"outer",
        .pos              = 1u,
        .containingRange   = {.start = 0u, .end = 300u},
        .symbolKind        = SymbolKind::Namespace,
        .children          = {OutlineNode{
            .name             = u"Widget",
            .pos              = 20u,
            .containingRange   = {.start = 10u, .end = 200u},
            .symbolKind        = SymbolKind::Class,
            .children          = {OutlineNode{
                .name             = u"getValue",
                .pos              = 40u,
                .containingRange   = {.start = 30u, .end = 80u},
                .symbolKind        = SymbolKind::Function,
                .children          = {},
            }},
        }},
    }};

    const std::vector<OutlineItem> items = buildOutlineItems(nodes);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].name, u"outer");
    EXPECT_EQ(items[0].targetPos, 1u);
    ASSERT_EQ(items[0].children.size(), 1u);
    EXPECT_EQ(items[0].children[0].name, u"Widget");
    EXPECT_EQ(items[0].children[0].targetPos, 20u);
    ASSERT_EQ(items[0].children[0].children.size(), 1u);
    EXPECT_EQ(items[0].children[0].children[0].name, u"getValue");
    EXPECT_EQ(items[0].children[0].children[0].targetPos, 40u);
    EXPECT_TRUE(items[0].children[0].children[0].children.empty());
}

TEST(AppOutlineBridgeTest, SiblingOrderIsPreserved) {
    const std::vector<OutlineNode> nodes{
        OutlineNode{
            .name             = u"first",
            .pos              = 1u,
            .containingRange   = {.start = 0u, .end = 10u},
            .symbolKind        = SymbolKind::Function,
            .children          = {},
        },
        OutlineNode{
            .name             = u"second",
            .pos              = 20u,
            .containingRange   = {.start = 10u, .end = 30u},
            .symbolKind        = SymbolKind::Function,
            .children          = {},
        },
    };

    const std::vector<OutlineItem> items = buildOutlineItems(nodes);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].name, u"first");
    EXPECT_EQ(items[1].name, u"second");
}

}  // namespace
