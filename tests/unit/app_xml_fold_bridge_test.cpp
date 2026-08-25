#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "neomifes/app/xml_fold_bridge.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/document/document.h"

namespace {

using neomifes::app::buildXmlFoldRegions;
using neomifes::core::FoldRegion;
using neomifes::document::Document;
using neomifes::xmltree::XmlNode;
using neomifes::xmltree::XmlNodeKind;

TEST(AppXmlFoldBridgeTest, LeafRootProducesNoRegions) {
    Document doc;
    doc.insertText(0, u"hi");
    const XmlNode node{.kind = XmlNodeKind::Text, .text = u"hi", .startPos = 0, .endPos = 2};
    EXPECT_TRUE(buildXmlFoldRegions(node, doc).empty());
}

TEST(AppXmlFoldBridgeTest, SingleLineElementIsExcluded) {
    Document doc;
    doc.insertText(0, u"<a>x</a>\n");
    const XmlNode node{.kind = XmlNodeKind::Element, .tagName = u"a", .selfClosing = false, .startPos = 0,
                       .endPos = 8};
    EXPECT_TRUE(buildXmlFoldRegions(node, doc).empty());
}

TEST(AppXmlFoldBridgeTest, SelfClosingElementSpanningMultipleLinesIsExcluded) {
    // <foo
    //   a="1"/>
    Document doc;
    doc.insertText(0, u"<foo\n  a=\"1\"/>\n");
    const XmlNode node{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"foo",
        .selfClosing = true,  // the guard under direct test here
        .startPos    = 0,
        .endPos      = doc.lineToOffset(1) + 10,  // past the '/>' on line 1
    };
    EXPECT_TRUE(buildXmlFoldRegions(node, doc).empty());
}

TEST(AppXmlFoldBridgeTest, MultiLineElementProducesRegionWithCorrectHeaderAndEndLines) {
    Document doc;
    doc.insertText(0,
                    u"<a>\n"    // line 0 (header)
                    u"  x\n"    // line 1
                    u"</a>\n"); // line 2 (closing tag)
    const XmlNode node{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"a",
        .selfClosing = false,
        .startPos    = 0,
        .endPos      = doc.lineToOffset(3),  // just past </a> on line 2
    };

    const auto regions = buildXmlFoldRegions(node, doc);
    ASSERT_EQ(regions.size(), 1U);
    EXPECT_EQ(regions[0].headerLine, 0U);
    EXPECT_EQ(regions[0].endLineInclusive, 2U);
    EXPECT_FALSE(regions[0].folded);
}

TEST(AppXmlFoldBridgeTest, MultiLineCommentAndTextLeavesNeverProduceRegions) {
    Document doc;
    doc.insertText(0,
                    u"<a>\n"
                    u"<!--line1\nline2-->\n"
                    u"</a>\n");
    const XmlNode comment{
        .kind = XmlNodeKind::Comment, .text = u"<!--line1\nline2-->", .startPos = doc.lineToOffset(1),
        .endPos = doc.lineToOffset(2) - 1,
    };
    const XmlNode root{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"a",
        .selfClosing = false,
        .startPos    = 0,
        .endPos      = doc.lineToOffset(3),
        .children    = {comment},
    };

    const auto regions = buildXmlFoldRegions(root, doc);
    // Only the root Element produces a region - the multi-line Comment leaf
    // never does, regardless of how many lines its own raw text spans.
    ASSERT_EQ(regions.size(), 1U);
    EXPECT_EQ(regions[0].headerLine, 0U);
}

TEST(AppXmlFoldBridgeTest, ErrorNodeProducesNoRegion) {
    Document doc;
    doc.insertText(0, u"<a>\nbroken\n");
    const XmlNode node{.kind = XmlNodeKind::Error, .text = u"<a>\nbroken\n", .startPos = 0, .endPos = 11};
    EXPECT_TRUE(buildXmlFoldRegions(node, doc).empty());
}

TEST(AppXmlFoldBridgeTest, NestedTreeFlattensAllLevelsIncludingSingleLineExclusion) {
    Document doc;
    doc.insertText(0,
                    u"<root>\n"        // line 0 (root, multi-line)
                    u"  <a>x</a>\n"    // line 1 (single-line -> excluded)
                    u"  <b>\n"         // line 2 (nested, multi-line)
                    u"    y\n"         // line 3
                    u"  </b>\n"        // line 4
                    u"</root>\n");     // line 5

    const XmlNode singleLineChild{
        .kind = XmlNodeKind::Element, .tagName = u"a", .selfClosing = false, .startPos = doc.lineToOffset(1),
        .endPos = doc.lineToOffset(2) - 1,
    };
    const XmlNode nestedLeaf{.kind = XmlNodeKind::Text, .text = u"y", .startPos = doc.lineToOffset(3),
                             .endPos = doc.lineToOffset(3) + 1};
    const XmlNode nestedElement{
        .kind = XmlNodeKind::Element, .tagName = u"b", .selfClosing = false, .startPos = doc.lineToOffset(2),
        .endPos = doc.lineToOffset(5), .children = {nestedLeaf},
    };
    const XmlNode root{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"root",
        .selfClosing = false,
        .startPos    = 0,
        .endPos      = doc.lineToOffset(6),
        .children    = {singleLineChild, nestedElement},
    };

    const auto regions = buildXmlFoldRegions(root, doc);

    // root (multi-line, headerLine 0) + b (multi-line, headerLine 2)
    // included; a (single-line) excluded - 2 entries total.
    ASSERT_EQ(regions.size(), 2U);
    const bool hasRoot = std::ranges::any_of(regions, [](const FoldRegion& r) { return r.headerLine == 0U; });
    const bool hasB    = std::ranges::any_of(regions, [](const FoldRegion& r) { return r.headerLine == 2U; });
    EXPECT_TRUE(hasRoot);
    EXPECT_TRUE(hasB);
}

}  // namespace
