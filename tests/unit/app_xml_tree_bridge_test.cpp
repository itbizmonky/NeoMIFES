#include <gtest/gtest.h>

#include "neomifes/app/xml_tree_bridge.h"

namespace {

using neomifes::app::buildXmlTreeItems;
using neomifes::ui::OutlineItem;
using neomifes::xmltree::XmlAttribute;
using neomifes::xmltree::XmlNode;
using neomifes::xmltree::XmlNodeKind;

TEST(AppXmlTreeBridgeTest, SelfClosingElementWithoutAttributesUsesShortTagLabel) {
    const XmlNode node{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"br",
        .attributes  = {},
        .selfClosing = true,
        .text        = u"",
        .startPos    = 0,
        .endPos      = 4,
        .children    = {},
    };

    const OutlineItem item = buildXmlTreeItems(node);
    EXPECT_EQ(item.name, u"<br/>");
    EXPECT_EQ(item.targetPos, 0U);
    EXPECT_TRUE(item.children.empty());
}

TEST(AppXmlTreeBridgeTest, SelfClosingElementWithAttributesUsesRawSourceSpelling) {
    const XmlNode node{
        .kind = XmlNodeKind::Element,
        .tagName = u"img",
        .attributes =
            {
                XmlAttribute{.name = u"src", .value = u"\"a.png\"", .startPos = 5, .endPos = 17},
                XmlAttribute{.name = u"alt", .value = u"'x'", .startPos = 18, .endPos = 25},
            },
        .selfClosing = true,
        .text        = u"",
        .startPos    = 0,
        .endPos      = 27,
        .children    = {},
    };

    const OutlineItem item = buildXmlTreeItems(node);
    EXPECT_EQ(item.name, u"<img src=\"a.png\" alt='x'/>");
}

TEST(AppXmlTreeBridgeTest, NonSelfClosingElementWithChildrenAppendsCountSuffix) {
    const XmlNode leaf{
        .kind = XmlNodeKind::Text, .tagName = u"", .attributes = {}, .selfClosing = false, .text = u"hi",
        .startPos = 6, .endPos = 8, .children = {},
    };
    const XmlNode node{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"a",
        .attributes  = {},
        .selfClosing = false,
        .text        = u"",
        .startPos    = 0,
        .endPos      = 12,
        .children    = {leaf},
    };

    const OutlineItem item = buildXmlTreeItems(node);
    EXPECT_EQ(item.name, u"<a> {1}");
    ASSERT_EQ(item.children.size(), 1U);
    EXPECT_EQ(item.children[0].name, u"hi");
    EXPECT_EQ(item.children[0].targetPos, 6U);
}

TEST(AppXmlTreeBridgeTest, CommentCdataProcessingInstructionEntityReferenceUseRawTextAsIs) {
    const XmlNode comment{.kind = XmlNodeKind::Comment, .text = u"<!--c-->", .startPos = 0, .endPos = 8};
    const XmlNode cdata{.kind = XmlNodeKind::Cdata, .text = u"<![CDATA[x]]>", .startPos = 0, .endPos = 13};
    const XmlNode pi{.kind = XmlNodeKind::ProcessingInstruction, .text = u"<?t d?>", .startPos = 0, .endPos = 7};
    const XmlNode entity{.kind = XmlNodeKind::EntityReference, .text = u"&amp;", .startPos = 0, .endPos = 5};

    EXPECT_EQ(buildXmlTreeItems(comment).name, u"<!--c-->");
    EXPECT_EQ(buildXmlTreeItems(cdata).name, u"<![CDATA[x]]>");
    EXPECT_EQ(buildXmlTreeItems(pi).name, u"<?t d?>");
    EXPECT_EQ(buildXmlTreeItems(entity).name, u"&amp;");
}

TEST(AppXmlTreeBridgeTest, ErrorNodeGetsParseErrorPrefix) {
    const XmlNode node{.kind = XmlNodeKind::Error, .text = u"<a><b></a>", .startPos = 0, .endPos = 10};

    const OutlineItem item = buildXmlTreeItems(node);
    EXPECT_EQ(item.name, u"[parse error] <a><b></a>");
}

TEST(AppXmlTreeBridgeTest, LongTextIsTruncatedWithEllipsis) {
    const std::u16string longText(80, u'x');
    const XmlNode         node{.kind = XmlNodeKind::Text, .text = longText, .startPos = 0, .endPos = 80};

    const OutlineItem item = buildXmlTreeItems(node);
    EXPECT_EQ(item.name.size(), 61U);  // 60 chars + ellipsis
    EXPECT_EQ(item.name.back(), u'…');
}

TEST(AppXmlTreeBridgeTest, EmbeddedNewlinesAndTabsCollapseToSpaces) {
    const XmlNode node{.kind = XmlNodeKind::Text, .text = u"line1\nline2\tend", .startPos = 0, .endPos = 15};

    const OutlineItem item = buildXmlTreeItems(node);
    EXPECT_EQ(item.name, u"line1 line2 end");
}

TEST(AppXmlTreeBridgeTest, WhitespaceOnlyTextGetsPlaceholderLabel) {
    const XmlNode node{.kind = XmlNodeKind::Text, .text = u"\n  \t\n", .startPos = 0, .endPos = 5};

    const OutlineItem item = buildXmlTreeItems(node);
    EXPECT_EQ(item.name, u"(whitespace)");
    // The node still carries its real position - it remains a real,
    // clickable jump target even though its label is a placeholder.
    EXPECT_EQ(item.targetPos, 0U);
}

TEST(AppXmlTreeBridgeTest, NestedTreeMapsAcrossThreeLevelsPreservingSiblingOrder) {
    const XmlNode innerLeaf{.kind = XmlNodeKind::Text, .text = u"c", .startPos = 40, .endPos = 41};
    const XmlNode inner{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"b",
        .selfClosing = false,
        .startPos    = 20,
        .endPos      = 45,
        .children    = {innerLeaf},
    };
    const XmlNode comment{.kind = XmlNodeKind::Comment, .text = u"<!--d-->", .startPos = 50, .endPos = 58};
    const XmlNode root{
        .kind        = XmlNodeKind::Element,
        .tagName     = u"root",
        .selfClosing = false,
        .startPos    = 0,
        .endPos      = 60,
        .children    = {inner, comment},
    };

    const OutlineItem item = buildXmlTreeItems(root);
    EXPECT_EQ(item.name, u"<root> {2}");
    ASSERT_EQ(item.children.size(), 2U);

    EXPECT_EQ(item.children[0].name, u"<b> {1}");
    EXPECT_EQ(item.children[0].targetPos, 20U);
    ASSERT_EQ(item.children[0].children.size(), 1U);
    EXPECT_EQ(item.children[0].children[0].name, u"c");
    EXPECT_EQ(item.children[0].children[0].targetPos, 40U);
    EXPECT_TRUE(item.children[0].children[0].children.empty());

    EXPECT_EQ(item.children[1].name, u"<!--d-->");
    EXPECT_EQ(item.children[1].targetPos, 50U);
}

}  // namespace
