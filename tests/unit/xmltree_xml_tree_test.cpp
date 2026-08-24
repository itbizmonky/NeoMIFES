#include <gtest/gtest.h>

#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/xmltree/xml_tree.h"

namespace {

using neomifes::document::Document;
using neomifes::document::TextRange;
using neomifes::xmltree::parseXmlTree;
using neomifes::xmltree::XmlNode;
using neomifes::xmltree::XmlNodeKind;
using neomifes::xmltree::XmlTree;

[[nodiscard]] Document makeDoc(std::u16string_view text) {
    Document doc;
    doc.insertText(0, text);
    return doc;
}

// Builds `<a><a>...<a>x</a>...</a></a>` with `depth` levels of nesting - used
// by the deep-nesting regression test below. Factored out (along with
// assertDeepNestingShape() below) purely to keep that TEST's own cognitive
// complexity under clang-tidy's threshold; gtest's assertion macros already
// expand to control flow that counts heavily against the enclosing
// function, so moving the two `for` loops out of the TEST body directly
// addresses the warning (same "extract to reduce TEST body complexity"
// precedent as csvmode_csv_model_test.cpp's expectRowsMatch()).
[[nodiscard]] std::u16string buildDeepNestingXml(int depth) {
    std::u16string text;
    text.reserve((static_cast<std::size_t>(depth) * 7) + 1);
    for (int i = 0; i < depth; ++i) {
        text += u"<a>";
    }
    text += u"x";
    for (int i = 0; i < depth; ++i) {
        text += u"</a>";
    }
    return text;
}

// Walks `depth` levels of single-child `<a>` elements starting at `root`,
// asserting each is named "a" and has exactly one child, then checks the
// innermost node's own single child is the Text leaf "x".
void assertDeepNestingShape(const XmlNode& root, int depth) {
    const XmlNode* node = &root;
    for (int i = 0; i < depth - 1; ++i) {
        ASSERT_TRUE(node->tagName == u"a" && node->children.size() == 1U) << "shape mismatch at depth " << i;
        node = node->children.data();
    }
    ASSERT_EQ(node->tagName, u"a");
    ASSERT_EQ(node->children.size(), 1U);
    EXPECT_EQ(node->children[0].kind, XmlNodeKind::Text);
    EXPECT_EQ(node->children[0].text, u"x");
}

// --- Structural correctness -------------------------------------------------

TEST(XmlTreeTest, NestedElementWithAttributeAndTextHasExpectedShape) {
    // <root><child a="1">hi</child></root>
    const Document doc  = makeDoc(u"<root><child a=\"1\">hi</child></root>");
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_FALSE(tree.hasErrors);
    EXPECT_EQ(tree.root.kind, XmlNodeKind::Element);
    EXPECT_EQ(tree.root.tagName, u"root");
    EXPECT_FALSE(tree.root.selfClosing);
    ASSERT_EQ(tree.root.children.size(), 1U);

    const XmlNode& child = tree.root.children[0];
    EXPECT_EQ(child.kind, XmlNodeKind::Element);
    EXPECT_EQ(child.tagName, u"child");
    EXPECT_FALSE(child.selfClosing);
    ASSERT_EQ(child.attributes.size(), 1U);
    EXPECT_EQ(child.attributes[0].name, u"a");
    EXPECT_EQ(child.attributes[0].value, u"\"1\"");
    ASSERT_EQ(child.children.size(), 1U);
    EXPECT_EQ(child.children[0].kind, XmlNodeKind::Text);
    EXPECT_EQ(child.children[0].text, u"hi");
}

TEST(XmlTreeTest, SelfClosingAndExplicitlyEmptyElementsAreDistinguished) {
    // <a><b/><c></c></a>
    const Document doc  = makeDoc(u"<a><b/><c></c></a>");
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_FALSE(tree.hasErrors);
    ASSERT_EQ(tree.root.children.size(), 2U);

    const XmlNode& b = tree.root.children[0];
    EXPECT_EQ(b.tagName, u"b");
    EXPECT_TRUE(b.selfClosing);
    EXPECT_TRUE(b.children.empty());

    const XmlNode& c = tree.root.children[1];
    EXPECT_EQ(c.tagName, u"c");
    // Explicitly closed but empty - structurally different from b above
    // (STag+ETag with no content node at all) despite both having zero
    // children; selfClosing is what actually distinguishes them.
    EXPECT_FALSE(c.selfClosing);
    EXPECT_TRUE(c.children.empty());
}

TEST(XmlTreeTest, AttributesSupportBothQuoteStylesAndKeepEntitiesUndecoded) {
    // <tag a='single' b="double" c="x&amp;y"></tag>
    const Document doc  = makeDoc(u"<tag a='single' b=\"double\" c=\"x&amp;y\"></tag>");
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_FALSE(tree.hasErrors);
    ASSERT_EQ(tree.root.attributes.size(), 3U);
    EXPECT_EQ(tree.root.attributes[0].name, u"a");
    EXPECT_EQ(tree.root.attributes[0].value, u"'single'");
    EXPECT_EQ(tree.root.attributes[1].name, u"b");
    EXPECT_EQ(tree.root.attributes[1].value, u"\"double\"");
    EXPECT_EQ(tree.root.attributes[2].name, u"c");
    // Raw span, quotes included, entity reference left undecoded.
    EXPECT_EQ(tree.root.attributes[2].value, u"\"x&amp;y\"");
    EXPECT_TRUE(tree.root.children.empty());
}

TEST(XmlTreeTest, MixedContentPreservesOrderAndKindsIncludingCommentCdataEntity) {
    // <a>hello<b/>world<!--cmt--><![CDATA[raw<>]]></a>
    const Document doc  = makeDoc(u"<a>hello<b/>world<!--cmt--><![CDATA[raw<>]]></a>");
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_FALSE(tree.hasErrors);
    ASSERT_EQ(tree.root.children.size(), 5U);

    EXPECT_EQ(tree.root.children[0].kind, XmlNodeKind::Text);
    EXPECT_EQ(tree.root.children[0].text, u"hello");

    EXPECT_EQ(tree.root.children[1].kind, XmlNodeKind::Element);
    EXPECT_EQ(tree.root.children[1].tagName, u"b");
    EXPECT_TRUE(tree.root.children[1].selfClosing);

    EXPECT_EQ(tree.root.children[2].kind, XmlNodeKind::Text);
    EXPECT_EQ(tree.root.children[2].text, u"world");

    EXPECT_EQ(tree.root.children[3].kind, XmlNodeKind::Comment);
    EXPECT_EQ(tree.root.children[3].text, u"<!--cmt-->");

    EXPECT_EQ(tree.root.children[4].kind, XmlNodeKind::Cdata);
    EXPECT_EQ(tree.root.children[4].text, u"<![CDATA[raw<>]]>");
}

TEST(XmlTreeTest, EntityReferenceInContentIsItsOwnNode) {
    const Document doc  = makeDoc(u"<a>x&amp;y</a>");
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_FALSE(tree.hasErrors);
    ASSERT_EQ(tree.root.children.size(), 3U);
    EXPECT_EQ(tree.root.children[0].kind, XmlNodeKind::Text);
    EXPECT_EQ(tree.root.children[0].text, u"x");
    EXPECT_EQ(tree.root.children[1].kind, XmlNodeKind::EntityReference);
    EXPECT_EQ(tree.root.children[1].text, u"&amp;");
    EXPECT_EQ(tree.root.children[2].kind, XmlNodeKind::Text);
    EXPECT_EQ(tree.root.children[2].text, u"y");
}

// --- Position accuracy -------------------------------------------------------

TEST(XmlTreeTest, ElementAttributeAndLeafPositionsAreExact) {
    // <root><child a="1">hi</child></root>
    // 0         1         2         3
    // 0123456789012345678901234567890123456
    //           1111111111222222222233333333
    // '<root>' = [0,6); '<child a="1">' = [6,19); 'hi' = [19,21);
    // '</child>' = [21,29); '</root>' = [29,36) - total length 36.
    const Document doc  = makeDoc(u"<root><child a=\"1\">hi</child></root>");
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_FALSE(tree.hasErrors);
    EXPECT_EQ(tree.root.startPos, 0U);
    EXPECT_EQ(tree.root.endPos, 36U);

    ASSERT_EQ(tree.root.children.size(), 1U);
    const XmlNode& child = tree.root.children[0];
    EXPECT_EQ(child.startPos, 6U);
    EXPECT_EQ(child.endPos, 29U);

    ASSERT_EQ(child.attributes.size(), 1U);
    EXPECT_EQ(child.attributes[0].startPos, 13U);
    EXPECT_EQ(child.attributes[0].endPos, 18U);

    ASSERT_EQ(child.children.size(), 1U);
    EXPECT_EQ(child.children[0].startPos, 19U);
    EXPECT_EQ(child.children[0].endPos, 21U);
}

// --- Degenerate / malformed input --------------------------------------------

TEST(XmlTreeTest, EmptyDocumentYieldsErrorRootWithoutCrashing) {
    const Document doc  = makeDoc(u"");
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_TRUE(tree.hasErrors);
    EXPECT_EQ(tree.root.kind, XmlNodeKind::Error);
    EXPECT_EQ(tree.root.startPos, 0U);
    EXPECT_EQ(tree.root.endPos, 0U);
    EXPECT_TRUE(tree.root.text.empty());
}

TEST(XmlTreeTest, MismatchedClosingTagYieldsErrorRootSpanningWholeDocument) {
    const std::u16string text = u"<From>Jani</from>";
    const Document        doc  = makeDoc(text);
    const XmlTree         tree = parseXmlTree(doc);

    EXPECT_TRUE(tree.hasErrors);
    EXPECT_EQ(tree.root.kind, XmlNodeKind::Error);
    EXPECT_EQ(tree.root.startPos, 0U);
    EXPECT_EQ(tree.root.endPos, static_cast<neomifes::document::TextPos>(text.size()));
    EXPECT_EQ(tree.root.text, text);
}

// --- BufferSnapshot overload --------------------------------------------------

TEST(XmlTreeTest, ParseXmlTreeFromBufferSnapshotMatchesDocumentOverload) {
    const Document doc          = makeDoc(u"<a><b>1</b><b>2</b></a>");
    const XmlTree  viaDocument  = parseXmlTree(doc);
    const XmlTree  viaSnapshot  = parseXmlTree(*doc.snapshot());

    EXPECT_EQ(viaDocument.hasErrors, viaSnapshot.hasErrors);
    EXPECT_EQ(viaDocument.root, viaSnapshot.root);
}

// --- Multi-piece boundary regression guard -----------------------------------

TEST(XmlTreeTest, ParseXmlTreeAcrossMultiplePiecesMatchesSinglePieceResult) {
    // Forces the document below to span multiple pieces (not just the one
    // insertText() call every other test in this file uses via makeDoc()) -
    // parseXmlTree() concatenates snapshot->pieces() into one buffer before
    // parsing, so this is a regression guard against that concatenation
    // silently going wrong at a piece boundary. Mirrors
    // JsonTreeTest.ParseJsonTreeAcrossMultiplePiecesMatchesSinglePieceResult
    // (jsontree_json_tree_test.cpp).
    const std::u16string text = u"<person><name>Alice</name><age>30</age></person>";
    Document              doc;
    doc.insertText(0, text);
    constexpr std::uint64_t kSplitPos = 16;  // inside "Alice" (between 'l' and 'i')
    doc.insertText(kSplitPos, u"XXXX");
    doc.eraseRange(TextRange{.start = kSplitPos, .end = kSplitPos + 4});

    // Precondition check: confirm this test actually exercises the
    // multi-piece path rather than silently degrading to a single piece.
    ASSERT_GT(doc.snapshot()->pieces().size(), 1U);

    const XmlTree tree = parseXmlTree(doc);
    EXPECT_FALSE(tree.hasErrors);
    ASSERT_EQ(tree.root.children.size(), 2U);
    EXPECT_EQ(tree.root.children[0].tagName, u"name");
    ASSERT_EQ(tree.root.children[0].children.size(), 1U);
    EXPECT_EQ(tree.root.children[0].children[0].text, u"Alice");
    EXPECT_EQ(tree.root.children[1].tagName, u"age");
    ASSERT_EQ(tree.root.children[1].children.size(), 1U);
    EXPECT_EQ(tree.root.children[1].children[0].text, u"30");
}

// --- Deep nesting regression (stack safety) -----------------------------------

TEST(XmlTreeTest, WellFormedDeeplyNestedDocumentParsesWithoutCrashing) {
    // A standalone probe (ts_probe_xmltree, run before this module was
    // written per CLAUDE.md rule 3) confirmed tree-sitter-xml's own C parser
    // never crashes/stack-overflows regardless of nesting depth (tested to
    // 5000). buildXmlTree() in xml_tree.cpp additionally walks the resulting
    // TSNode tree with an explicit stack (not C++ recursion) so a genuinely
    // deep, WELL-FORMED document (which - unlike that probe's adversarial
    // case - produces a real nested tree-sitter tree, not a flat ERROR node)
    // cannot overflow THIS code's own call stack either - so no
    // kMaxXmlNestingDepth crash-safety guard was added (build_plan.md's
    // WI-15f DoD).
    //
    // A SEPARATE, unrelated limitation was found while writing this test
    // (a second standalone probe, ts_probe_xmldepth, binary-searched it):
    // tree-sitter-xml itself starts silently MISPARSING (ts_node_has_error()
    // becomes true on otherwise well-formed, balanced input) once XML
    // nesting exceeds ~505-509 levels - not a crash, just reduced fidelity
    // beyond that depth (this module's existing "root field unresolved ->
    // XmlNodeKind::Error" fallback already degrades safely for it, same
    // path as any other malformed input). Filed as
    // docs/issues/xmltree_deep_nesting_misparse_limit.md rather than worked
    // around - see that issue for why. This test therefore uses a depth
    // comfortably below that boundary so it actually exercises the
    // stack-safety property under test, without also tripping the separate,
    // already-documented parse-fidelity limit.
    constexpr int kDepth = 450;
    const Document doc  = makeDoc(buildDeepNestingXml(kDepth));
    const XmlTree  tree = parseXmlTree(doc);

    EXPECT_FALSE(tree.hasErrors);
    assertDeepNestingShape(tree.root, kDepth);
}

}  // namespace
