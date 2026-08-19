#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "neomifes/app/json_fold_bridge.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::app::buildJsonFoldRegions;
using neomifes::core::FoldRegion;
using neomifes::document::Document;
using neomifes::jsontree::JsonNode;
using neomifes::jsontree::JsonNodeKind;

TEST(AppJsonFoldBridgeTest, LeafRootProducesNoRegions) {
    Document doc;
    doc.insertText(0, u"42");
    const JsonNode node{
        .kind = JsonNodeKind::Number, .key = std::nullopt, .text = u"42", .startPos = 0, .endPos = 2, .children = {}};
    EXPECT_TRUE(buildJsonFoldRegions(node, doc).empty());
}

TEST(AppJsonFoldBridgeTest, SingleLineObjectIsExcluded) {
    Document doc;
    doc.insertText(0, u"{\"a\": 1}\n");
    const JsonNode node{
        .kind = JsonNodeKind::Object, .key = std::nullopt, .text = u"", .startPos = 0, .endPos = 8, .children = {}};
    EXPECT_TRUE(buildJsonFoldRegions(node, doc).empty());
}

TEST(AppJsonFoldBridgeTest, MultiLineObjectProducesRegionWithCorrectHeaderAndEndLines) {
    Document doc;
    doc.insertText(0,
                    u"{\n"          // line 0 (header)
                    u"  \"a\": 1\n"  // line 1
                    u"}\n");        // line 2 (last line of the object, endPos exclusive lands right after '}')
    const JsonNode node{
        .kind     = JsonNodeKind::Object,
        .key      = std::nullopt,
        .text     = u"",
        .startPos = 0,
        .endPos   = doc.lineToOffset(3),  // just past the closing '}' on line 2
        .children = {},
    };

    const auto regions = buildJsonFoldRegions(node, doc);
    ASSERT_EQ(regions.size(), 1U);
    EXPECT_EQ(regions[0].headerLine, 0U);
    EXPECT_EQ(regions[0].endLineInclusive, 2U);
    EXPECT_FALSE(regions[0].folded);
}

TEST(AppJsonFoldBridgeTest, LeafChildrenNeverProduceRegionsEvenWhenMultiLine) {
    Document doc;
    doc.insertText(0,
                    u"{\n"
                    u"  \"a\": \"line1\nline2\"\n"
                    u"}\n");
    const JsonNode leaf{
        .kind     = JsonNodeKind::String,
        .key      = u"a",
        .text     = u"\"line1\nline2\"",
        .startPos = doc.lineToOffset(1),
        .endPos   = doc.lineToOffset(2) - 1,
        .children = {},
    };
    const JsonNode root{
        .kind     = JsonNodeKind::Object,
        .key      = std::nullopt,
        .text     = u"",
        .startPos = 0,
        .endPos   = doc.lineToOffset(3),
        .children = {leaf},
    };

    const auto regions = buildJsonFoldRegions(root, doc);
    // Only the root Object produces a region - the String leaf never does,
    // regardless of how many lines its own raw text happens to span.
    ASSERT_EQ(regions.size(), 1U);
    EXPECT_EQ(regions[0].headerLine, 0U);
}

TEST(AppJsonFoldBridgeTest, NestedTreeFlattensAllLevelsIncludingSingleLineExclusion) {
    Document doc;
    doc.insertText(0,
                    u"{\n"                // line 0 (root, multi-line)
                    u"  \"a\": [1, 2],\n"  // line 1 (array, single-line -> excluded)
                    u"  \"b\": {\n"        // line 2 (nested object, multi-line)
                    u"    \"c\": 1\n"      // line 3
                    u"  }\n"               // line 4
                    u"}\n");               // line 5

    const JsonNode arrayElem0{
        .kind = JsonNodeKind::Number, .key = std::nullopt, .text = u"1", .startPos = 0, .endPos = 0, .children = {}};
    const JsonNode singleLineArray{
        .kind     = JsonNodeKind::Array,
        .key      = u"a",
        .text     = u"",
        .startPos = doc.lineToOffset(1),
        .endPos   = doc.lineToOffset(2) - 1,  // still on line 1
        .children = {arrayElem0},
    };

    const JsonNode innerLeaf{
        .kind = JsonNodeKind::Number, .key = u"c", .text = u"1", .startPos = doc.lineToOffset(3), .endPos = doc.lineToOffset(3) + 5, .children = {}};
    const JsonNode nestedObject{
        .kind     = JsonNodeKind::Object,
        .key      = u"b",
        .text     = u"",
        .startPos = doc.lineToOffset(2),
        .endPos   = doc.lineToOffset(5),  // just past the '}' on line 4
        .children = {innerLeaf},
    };

    const JsonNode root{
        .kind     = JsonNodeKind::Object,
        .key      = std::nullopt,
        .text     = u"",
        .startPos = 0,
        .endPos   = doc.lineToOffset(6),  // just past the closing '}' on line 5
        .children = {singleLineArray, nestedObject},
    };

    const auto regions = buildJsonFoldRegions(root, doc);

    // root (multi-line, headerLine 0) + b (multi-line, headerLine 2)
    // included; a (single-line array) excluded - 2 entries total.
    ASSERT_EQ(regions.size(), 2U);
    const bool hasRoot = std::ranges::any_of(regions, [](const FoldRegion& r) { return r.headerLine == 0U; });
    const bool hasB    = std::ranges::any_of(regions, [](const FoldRegion& r) { return r.headerLine == 2U; });
    EXPECT_TRUE(hasRoot);
    EXPECT_TRUE(hasB);
}

}  // namespace
