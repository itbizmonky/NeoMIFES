#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "neomifes/syntax/outline.h"

namespace {

using neomifes::syntax::extractOutline;
using neomifes::syntax::Language;
using neomifes::syntax::OutlineNode;
using neomifes::syntax::SymbolKind;

// Every expectation below was cross-checked against real tree-sitter-cpp
// v0.23.4 / tree-sitter-python v0.25.0 output via a standalone probe
// (ts_probe_outline) before being written (CLAUDE.md rule 3) - see the
// Phase 7f plan's Context section for the probe methodology.

TEST(SyntaxOutlineTest, EmptyTextProducesNoNodes) {
    EXPECT_TRUE(extractOutline(u"", Language::Cpp).empty());
    EXPECT_TRUE(extractOutline(u"", Language::Python).empty());
}

TEST(SyntaxOutlineTest, TextWithNoDefinitionsProducesNoNodes) {
    EXPECT_TRUE(extractOutline(u"int x = 42;\n", Language::Cpp).empty());
    EXPECT_TRUE(extractOutline(u"x = 42\n", Language::Python).empty());
}

TEST(SyntaxOutlineTest, CppFreeFunctionProducesOneFunctionNode) {
    const std::vector<OutlineNode> outline = extractOutline(u"int foo(int x) {\n    return x;\n}\n", Language::Cpp);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"foo");
    EXPECT_EQ(outline[0].symbolKind, SymbolKind::Function);
    EXPECT_EQ(outline[0].pos, 4u);
    EXPECT_EQ(outline[0].containingRange.start, 0u);
    EXPECT_TRUE(outline[0].children.empty());
}

TEST(SyntaxOutlineTest, CppClassWithMemberFunctionNestsCorrectly) {
    const std::vector<OutlineNode> outline =
        extractOutline(u"class Widget {\npublic:\n    int getValue() { return 0; }\n};\n", Language::Cpp);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"Widget");
    EXPECT_EQ(outline[0].symbolKind, SymbolKind::Class);
    ASSERT_EQ(outline[0].children.size(), 1u);
    EXPECT_EQ(outline[0].children[0].name, u"getValue");
    EXPECT_EQ(outline[0].children[0].symbolKind, SymbolKind::Function);
}

TEST(SyntaxOutlineTest, CppStructProducesOneStructNode) {
    const std::vector<OutlineNode> outline = extractOutline(u"struct Point { int x; };\n", Language::Cpp);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"Point");
    EXPECT_EQ(outline[0].symbolKind, SymbolKind::Struct);
}

TEST(SyntaxOutlineTest, CppNamespaceWithNestedClassProducesTwoLevels) {
    const std::vector<OutlineNode> outline =
        extractOutline(u"namespace outer {\n    class Inner {};\n}\n", Language::Cpp);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"outer");
    EXPECT_EQ(outline[0].symbolKind, SymbolKind::Namespace);
    ASSERT_EQ(outline[0].children.size(), 1u);
    EXPECT_EQ(outline[0].children[0].name, u"Inner");
    EXPECT_EQ(outline[0].children[0].symbolKind, SymbolKind::Class);
}

TEST(SyntaxOutlineTest, CppPointerAndReferenceReturnTypesResolveNameCorrectly) {
    // Verifies the declarator-unwrap loop follows pointer_declarator/
    // reference_declarator through to the nested function_declarator's
    // identifier, not just the simple no-wrapper case.
    const std::vector<OutlineNode> outline = extractOutline(
        u"int* getPtr() { return nullptr; }\n"
        u"int& getRef(int& x) { return x; }\n",
        Language::Cpp);
    ASSERT_EQ(outline.size(), 2u);
    EXPECT_EQ(outline[0].name, u"getPtr");
    EXPECT_EQ(outline[1].name, u"getRef");
}

TEST(SyntaxOutlineTest, CppQualifiedOutOfLineDefinitionResolvesUnqualifiedName) {
    // Widget::doThing() - the declarator unwraps to a qualified_identifier,
    // whose own "name" field holds just "doThing" (not "Widget::doThing").
    const std::vector<OutlineNode> outline = extractOutline(u"void Widget::doThing() {}\n", Language::Cpp);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"doThing");
}

TEST(SyntaxOutlineTest, CppMalformedInputDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({ [[maybe_unused]] auto result = extractOutline(u"int foo( {{{ ???\n", Language::Cpp); });
}

TEST(SyntaxOutlineTest, PythonFunctionProducesOneFunctionNode) {
    const std::vector<OutlineNode> outline = extractOutline(u"def foo(x):\n    return x\n", Language::Python);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"foo");
    EXPECT_EQ(outline[0].symbolKind, SymbolKind::Function);
}

TEST(SyntaxOutlineTest, PythonClassWithMethodNestsCorrectly) {
    const std::vector<OutlineNode> outline = extractOutline(
        u"class Widget:\n    def get_value(self):\n        return 0\n", Language::Python);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"Widget");
    EXPECT_EQ(outline[0].symbolKind, SymbolKind::Class);
    ASSERT_EQ(outline[0].children.size(), 1u);
    EXPECT_EQ(outline[0].children[0].name, u"get_value");
    EXPECT_EQ(outline[0].children[0].symbolKind, SymbolKind::Function);
}

TEST(SyntaxOutlineTest, PythonNestedFunctionClosureNestsCorrectly) {
    const std::vector<OutlineNode> outline = extractOutline(
        u"def outer():\n    def inner():\n        pass\n    return inner\n", Language::Python);
    ASSERT_EQ(outline.size(), 1u);
    EXPECT_EQ(outline[0].name, u"outer");
    ASSERT_EQ(outline[0].children.size(), 1u);
    EXPECT_EQ(outline[0].children[0].name, u"inner");
}

TEST(SyntaxOutlineTest, PythonMalformedInputDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(
        { [[maybe_unused]] auto result = extractOutline(u"def foo(:\n    ???\n", Language::Python); });
}

}  // namespace
