#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "neomifes/document/text_pos.h"
#include "neomifes/syntax/outline.h"

namespace {

using neomifes::document::TextPos;
using neomifes::document::TextRange;
using neomifes::syntax::extractOutline;
using neomifes::syntax::findBreadcrumbPath;
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

// Phase 7h (Breadcrumb): findBreadcrumbPath(). Fixtures use explicit
// designated-initializer OutlineNode{} construction (every field specified)
// per this codebase's clang-cl "missing designated field initializer"
// convention.

TEST(FindBreadcrumbPathTest, EmptyTreeProducesEmptyPath) {
    EXPECT_TRUE(findBreadcrumbPath(0, {}).empty());
}

TEST(FindBreadcrumbPathTest, PosOutsideEveryNodeProducesEmptyPath) {
    const std::vector<OutlineNode> tree{OutlineNode{
        .name             = u"foo",
        .pos              = 4,
        .containingRange  = TextRange{.start = 0, .end = 10},
        .symbolKind       = SymbolKind::Function,
        .children         = {},
    }};
    EXPECT_TRUE(findBreadcrumbPath(20, tree).empty());
}

TEST(FindBreadcrumbPathTest, PosInsideSingleTopLevelNodeProducesOneElementPath) {
    const std::vector<OutlineNode> tree{OutlineNode{
        .name             = u"foo",
        .pos              = 4,
        .containingRange  = TextRange{.start = 0, .end = 10},
        .symbolKind       = SymbolKind::Function,
        .children         = {},
    }};
    const auto path = findBreadcrumbPath(5, tree);
    ASSERT_EQ(path.size(), 1u);
    EXPECT_EQ(path[0]->name, u"foo");
}

TEST(FindBreadcrumbPathTest, ContainingRangeStartIsInclusiveEndIsExclusive) {
    const std::vector<OutlineNode> tree{OutlineNode{
        .name             = u"foo",
        .pos              = 4,
        .containingRange  = TextRange{.start = 5, .end = 10},
        .symbolKind       = SymbolKind::Function,
        .children         = {},
    }};
    EXPECT_EQ(findBreadcrumbPath(5, tree).size(), 1u);   // start: inclusive
    EXPECT_TRUE(findBreadcrumbPath(4, tree).empty());    // just before start
    EXPECT_TRUE(findBreadcrumbPath(10, tree).empty());   // end: exclusive
    EXPECT_EQ(findBreadcrumbPath(9, tree).size(), 1u);   // just before end
}

TEST(FindBreadcrumbPathTest, CorrectSiblingIsSelectedAmongMultiple) {
    const std::vector<OutlineNode> tree{
        OutlineNode{.name             = u"first",
                    .pos              = 0,
                    .containingRange  = TextRange{.start = 0, .end = 10},
                    .symbolKind       = SymbolKind::Function,
                    .children         = {}},
        OutlineNode{.name             = u"second",
                    .pos              = 10,
                    .containingRange  = TextRange{.start = 10, .end = 20},
                    .symbolKind       = SymbolKind::Function,
                    .children         = {}},
    };
    const auto path = findBreadcrumbPath(15, tree);
    ASSERT_EQ(path.size(), 1u);
    EXPECT_EQ(path[0]->name, u"second");
}

TEST(FindBreadcrumbPathTest, ThreeLevelNestingReturnsOutermostFirst) {
    // namespace outer { class Widget { int getValue() { return 0; } }; }
    const std::vector<OutlineNode> outline = extractOutline(
        u"namespace outer {\n"
        u"    class Widget {\n"
        u"    public:\n"
        u"        int getValue() { return 0; }\n"
        u"    };\n"
        u"}\n",
        Language::Cpp);
    ASSERT_EQ(outline.size(), 1u);
    ASSERT_EQ(outline[0].children.size(), 1u);
    ASSERT_EQ(outline[0].children[0].children.size(), 1u);

    const TextPos posInsideMethodBody = outline[0].children[0].children[0].containingRange.end - 2;
    const auto path = findBreadcrumbPath(posInsideMethodBody, outline);
    ASSERT_EQ(path.size(), 3u);
    EXPECT_EQ(path[0]->name, u"outer");
    EXPECT_EQ(path[1]->name, u"Widget");
    EXPECT_EQ(path[2]->name, u"getValue");
}

// Phase 7n1: C/JavaScript/Java/Go/Rust/Json get an empty SymbolTable (no
// symbol-extraction logic implemented yet, see outline.cpp's
// emptySymbolTable() comment) - these pin down the SAFE side of that
// decision: extractOutline() must not crash, and must not route these
// languages' source through Cpp's or Python's grammar/table (the bug this
// batch's tsLanguageFor()/symbolTableFor() centralization fixed - see
// outline.cpp's extractOutline() comment). Definition-bearing source is
// used deliberately (not empty text) so an accidental fall-through to
// Cpp/Python's tables, which happen to share some node-type names like
// "identifier", would be caught here rather than passing vacuously.
TEST(SyntaxOutlineTest, NewPhase7n1LanguagesProduceNoNodesWithoutCrashingOrMisroutingGrammar) {
    EXPECT_TRUE(extractOutline(u"int foo(int x) { return x; }\n", Language::C).empty());
    EXPECT_TRUE(extractOutline(u"function foo(x) { return x; }\n", Language::JavaScript).empty());
    EXPECT_TRUE(extractOutline(u"class Foo { int bar() { return 1; } }\n", Language::Java).empty());
    EXPECT_TRUE(extractOutline(u"package main\nfunc foo() int { return 1 }\n", Language::Go).empty());
    EXPECT_TRUE(extractOutline(u"fn foo() -> i32 { 1 }\n", Language::Rust).empty());
    EXPECT_TRUE(extractOutline(u"{\"a\": 1}", Language::Json).empty());
}

// Phase 7r: same safe-degradation contract extended to Html/Css/Shell/Yaml/
// Toml/Xml (see outline.cpp's emptySymbolTable() comment).
TEST(SyntaxOutlineTest, NewPhase7rLanguagesProduceNoNodesWithoutCrashingOrMisroutingGrammar) {
    EXPECT_TRUE(extractOutline(u"<div><span>text</span></div>\n", Language::Html).empty());
    EXPECT_TRUE(extractOutline(u".foo { color: red; }\n", Language::Css).empty());
    EXPECT_TRUE(extractOutline(u"foo() { echo hi; }\n", Language::Shell).empty());
    EXPECT_TRUE(extractOutline(u"key:\n  nested: value\n", Language::Yaml).empty());
    EXPECT_TRUE(extractOutline(u"[section]\nkey = 1\n", Language::Toml).empty());
    EXPECT_TRUE(extractOutline(u"<root><child>text</child></root>\n", Language::Xml).empty());
}

// Phase 7s: same safe-degradation contract extended to TypeScript/Tsx/Php/
// Markdown (see outline.cpp's emptySymbolTable() comment).
TEST(SyntaxOutlineTest, NewPhase7sLanguagesProduceNoNodesWithoutCrashingOrMisroutingGrammar) {
    EXPECT_TRUE(extractOutline(u"class Foo { getX(): number { return 1; } }\n", Language::TypeScript).empty());
    EXPECT_TRUE(extractOutline(u"function Comp() { return <div>hi</div>; }\n", Language::Tsx).empty());
    EXPECT_TRUE(extractOutline(u"<?php\nfunction foo() { return 1; }\n", Language::Php).empty());
    EXPECT_TRUE(extractOutline(u"# Heading\n\nSome text.\n", Language::Markdown).empty());
}

// Phase 7x: same safe-degradation contract extended to PowerShell/Ini/Batch
// (see outline.cpp's emptySymbolTable() comment).
TEST(SyntaxOutlineTest, NewPhase7xLanguagesProduceNoNodesWithoutCrashingOrMisroutingGrammar) {
    EXPECT_TRUE(extractOutline(u"function Foo {\n}\n", Language::PowerShell).empty());
    EXPECT_TRUE(extractOutline(u"[section]\nkey=value\n", Language::Ini).empty());
    EXPECT_TRUE(extractOutline(u"set VAR=hi\necho %VAR%\n", Language::Batch).empty());
}

// Phase 7y: same safe-degradation contract extended to Sql (see outline.cpp's
// emptySymbolTable() comment).
TEST(SyntaxOutlineTest, SqlProducesNoNodesWithoutCrashingOrMisroutingGrammar) {
    EXPECT_TRUE(extractOutline(u"SELECT id, name FROM users;\n", Language::Sql).empty());
}

}  // namespace
