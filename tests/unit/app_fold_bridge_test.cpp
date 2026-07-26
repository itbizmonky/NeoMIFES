#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "neomifes/app/fold_bridge.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/document/document.h"
#include "neomifes/syntax/outline.h"

namespace {

using neomifes::app::buildFoldRegions;
using neomifes::core::FoldRegion;
using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::syntax::OutlineNode;
using neomifes::syntax::SymbolKind;

TEST(AppFoldBridgeTest, EmptyVectorProducesEmptyVector) {
    Document doc;
    EXPECT_TRUE(buildFoldRegions({}, doc).empty());
}

TEST(AppFoldBridgeTest, SingleLineNodeIsExcluded) {
    Document doc;
    doc.insertText(0, u"int foo() { return 0; }\n");
    const std::vector<OutlineNode> nodes{OutlineNode{
        .name             = u"foo",
        .pos              = 4,
        .containingRange  = TextRange{.start = 0, .end = 24},  // all on line 0
        .symbolKind       = SymbolKind::Function,
        .children         = {},
    }};
    EXPECT_TRUE(buildFoldRegions(nodes, doc).empty());
}

TEST(AppFoldBridgeTest, MultiLineNodeProducesRegionWithCorrectHeaderAndEndLines) {
    Document doc;
    doc.insertText(0,
                    u"int foo() {\n"    // line 0 (header)
                    u"    return 0;\n"  // line 1
                    u"}\n");            // line 2 (last line of containingRange)
    const std::vector<OutlineNode> nodes{OutlineNode{
        .name             = u"foo",
        .pos              = 4,
        .containingRange  = TextRange{.start = 0, .end = 27},  // through the closing '}' on line 2 (end exclusive)
        .symbolKind       = SymbolKind::Function,
        .children         = {},
    }};
    const auto regions = buildFoldRegions(nodes, doc);
    ASSERT_EQ(regions.size(), 1U);
    EXPECT_EQ(regions[0].headerLine, 0U);
    EXPECT_EQ(regions[0].endLineInclusive, 2U);
    EXPECT_FALSE(regions[0].folded);
}

TEST(AppFoldBridgeTest, NestedTreeFlattensAllLevelsIncludingSingleLineExclusion) {
    Document doc;
    doc.insertText(0,
                    u"namespace outer {\n"    // line 0
                    u"class Widget {\n"        // line 1
                    u"    int getValue() { return 0; }\n"  // line 2 (single-line method)
                    u"};\n"                    // line 3
                    u"}\n");                   // line 4
    const OutlineNode method{
        .name             = u"getValue",
        .pos              = 0,   // exact offset not load-bearing for this test
        .containingRange  = TextRange{.start = 0, .end = 0},  // will be overwritten below
        .symbolKind       = SymbolKind::Function,
        .children         = {},
    };
    // Recompute containingRange to span only line 2 (single line -> excluded).
    OutlineNode methodOnLine2 = method;
    methodOnLine2.pos             = doc.lineToOffset(2) + 8;  // inside "getValue"
    methodOnLine2.containingRange = TextRange{.start = doc.lineToOffset(2), .end = doc.lineToOffset(3) - 1};

    OutlineNode widgetClass{
        .name             = u"Widget",
        .pos              = doc.lineToOffset(1) + 6,
        .containingRange  = TextRange{.start = doc.lineToOffset(1), .end = doc.lineToOffset(4)},
        .symbolKind       = SymbolKind::Class,
        .children         = {methodOnLine2},
    };
    const std::vector<OutlineNode> nodes{OutlineNode{
        .name             = u"outer",
        .pos              = 10,
        .containingRange  = TextRange{.start = 0, .end = doc.lineToOffset(4) + 1},
        .symbolKind       = SymbolKind::Namespace,
        .children         = {widgetClass},
    }};

    const auto regions = buildFoldRegions(nodes, doc);

    // outer (multi-line) + Widget (multi-line) included; getValue (single
    // line) excluded - 2 entries total, both nesting levels represented.
    ASSERT_EQ(regions.size(), 2U);
    const bool hasOuter = std::ranges::any_of(regions, [](const FoldRegion& r) { return r.headerLine == 0U; });
    const bool hasWidget = std::ranges::any_of(regions, [](const FoldRegion& r) { return r.headerLine == 1U; });
    EXPECT_TRUE(hasOuter);
    EXPECT_TRUE(hasWidget);
}

}  // namespace
