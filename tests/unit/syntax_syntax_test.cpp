#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "neomifes/syntax/syntax.h"

namespace {

using neomifes::syntax::Language;
using neomifes::syntax::parse;
using neomifes::syntax::parseC;
using neomifes::syntax::parseCpp;
using neomifes::syntax::parseCss;
using neomifes::syntax::parseGo;
using neomifes::syntax::parseHtml;
using neomifes::syntax::parseJava;
using neomifes::syntax::parseJavaScript;
using neomifes::syntax::parseJson;
using neomifes::syntax::parsePython;
using neomifes::syntax::parseRust;
using neomifes::syntax::parseShell;
using neomifes::syntax::parseToml;
using neomifes::syntax::parseXml;
using neomifes::syntax::parseYaml;
using neomifes::syntax::Token;
using neomifes::syntax::TokenKind;

TEST(SyntaxParseCppTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseCpp(u"").empty());
}

TEST(SyntaxParseCppTest, WhitespaceOnlyProducesNoTokens) {
    EXPECT_TRUE(parseCpp(u"   \n\t\n  ").empty());
}

TEST(SyntaxParseCppTest, ClassifiesTypeIdentifierNumberAndPunctuation) {
    const std::vector<Token> tokens = parseCpp(u"int x = 42;");
    // "int" is tree-sitter-cpp's named `primitive_type` leaf (Type), not an
    // anonymous keyword token - verified via a standalone probe before
    // writing this assertion (see ADR-014's "実装上の注意点").
    // int(Type) x(Variable) =(Punctuation) 42(Number) ;(Punctuation)
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Type);
    EXPECT_EQ(tokens[0].range.start, 0u);
    EXPECT_EQ(tokens[0].range.end, 3u);
    EXPECT_EQ(tokens[1].kind, TokenKind::Variable);
    EXPECT_EQ(tokens[2].kind, TokenKind::Punctuation);
    EXPECT_EQ(tokens[3].kind, TokenKind::Number);
    EXPECT_EQ(tokens[3].range.start, 8u);
    EXPECT_EQ(tokens[3].range.end, 10u);
    EXPECT_EQ(tokens[4].kind, TokenKind::Punctuation);
}

TEST(SyntaxParseCppTest, ClassifiesLineComment) {
    const std::vector<Token> tokens = parseCpp(u"// hello\n");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.start, 0u);
    EXPECT_EQ(tokens[0].range.end, 8u);
}

TEST(SyntaxParseCppTest, ClassifiesBlockComment) {
    const std::vector<Token> tokens = parseCpp(u"/* a\nb */");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
}

TEST(SyntaxParseCppTest, ClassifiesStringLiteralIncludingQuotesAndEscape) {
    const std::vector<Token> tokens = parseCpp(u"\"hi\\n\"");
    // opening quote, content "hi", escape "\n", closing quote - all String
    ASSERT_EQ(tokens.size(), 4u);
    for (const Token& token : tokens) {
        EXPECT_EQ(token.kind, TokenKind::String);
    }
    EXPECT_EQ(tokens.front().range.start, 0u);
    EXPECT_EQ(tokens.back().range.end, 6u);
}

TEST(SyntaxParseCppTest, ClassifiesCharLiteral) {
    const std::vector<Token> tokens = parseCpp(u"'a'");
    ASSERT_EQ(tokens.size(), 3u);  // open quote, 'a', close quote
    for (const Token& token : tokens) {
        EXPECT_EQ(token.kind, TokenKind::String);
    }
}

TEST(SyntaxParseCppTest, ClassifiesPreprocessorInclude) {
    const std::vector<Token> tokens = parseCpp(u"#include <cstdio>\n");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Preprocessor);  // "#include" anonymous token
    EXPECT_EQ(tokens[1].kind, TokenKind::String);         // system_lib_string "<cstdio>"
}

TEST(SyntaxParseCppTest, ClassifiesPreprocessorDefine) {
    const std::vector<Token> tokens = parseCpp(u"#define FOO 1\n");
    // "#define"(Preprocessor) FOO(identifier -> Variable) 1(preproc_arg ->
    // Preprocessor) - the macro name is a plain named `identifier` leaf in
    // tree-sitter-cpp's grammar, not part of a single opaque preprocessor
    // span; verified via a standalone probe (see ADR-014).
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Preprocessor);
    EXPECT_EQ(tokens[1].kind, TokenKind::Variable);
    EXPECT_EQ(tokens[2].kind, TokenKind::Preprocessor);
}

TEST(SyntaxParseCppTest, ClassifiesTypeIdentifierForClassName) {
    const std::vector<Token> tokens = parseCpp(u"class Foo {};");
    ASSERT_GE(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Keyword);  // class
    EXPECT_EQ(tokens[1].kind, TokenKind::Type);      // Foo (type_identifier)
}

TEST(SyntaxParseCppTest, ClassifiesFieldIdentifierAsVariable) {
    const std::vector<Token> tokens = parseCpp(u"struct S { int x; };");
    const auto it = std::ranges::find_if(tokens, [](const Token& t) { return t.kind == TokenKind::Variable; });
    ASSERT_NE(it, tokens.end());
}

TEST(SyntaxParseCppTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    // Unbalanced braces / stray tokens - tree-sitter never fails to produce a
    // tree (see syntax.h), this only checks the wrapper doesn't crash and
    // still returns something rather than silently swallowing everything.
    const std::vector<Token> tokens = parseCpp(u"int main( {{{ ???");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseCppTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    // "// 日本語コメント\n" - the comment token's range must cover the full
    // UTF-16 code-unit span including the multibyte (surrogate-free, BMP)
    // Japanese text, not a truncated/garbled subset.
    const std::u16string source = u"// 日本語コメント\n";
    const std::vector<Token> tokens = parseCpp(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.start, 0u);
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);  // excludes trailing \n
}

TEST(SyntaxParseCppTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens = parseCpp(u"void bar() { x += 1; }");
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start)
            << "token " << i - 1 << " overlaps token " << i;
    }
}

// Phase 7d. Every expectation below was cross-checked against real
// tree-sitter-python v0.25.0 parser output via a standalone probe before
// being written (CLAUDE.md rule 3) - see the Phase 7d plan's Context
// section for the probe methodology, mirroring Phase 7a's for C++ above.

TEST(SyntaxParsePythonTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parsePython(u"").empty());
}

TEST(SyntaxParsePythonTest, WhitespaceOnlyProducesNoTokens) {
    // Also exercises the "root node itself has zero children" case, unique
    // to Python's grammar among the two languages this module supports: an
    // empty/whitespace-only module's root `module` node is itself a
    // zero-width NAMED leaf (unlike C++'s translation_unit), which
    // appendLeafToken()'s zero-width guard must still correctly skip.
    EXPECT_TRUE(parsePython(u"   \n\t\n  ").empty());
}

TEST(SyntaxParsePythonTest, ClassifiesLoneComment) {
    const std::vector<Token> tokens = parsePython(u"# just a comment\n");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.start, 0u);
    EXPECT_EQ(tokens[0].range.end, 16u);  // excludes trailing \n
}

TEST(SyntaxParsePythonTest, ClassifiesSimpleAssignment) {
    const std::vector<Token> tokens = parsePython(u"x = 42\n");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Variable);  // x - identifier, not a separate Type node
    EXPECT_EQ(tokens[1].kind, TokenKind::Punctuation);  // =
    EXPECT_EQ(tokens[2].kind, TokenKind::Number);       // 42
    EXPECT_EQ(tokens[2].range.start, 4u);
    EXPECT_EQ(tokens[2].range.end, 6u);
}

TEST(SyntaxParsePythonTest, ClassifiesStringWithEscapeSequenceAsOneMergedToken) {
    // s = "hi\n" - until Phase 7n1 this was a KNOWN, ACCEPTED gap: when
    // string_content contains an escape_sequence child, tree-sitter-python
    // does NOT emit a separate leaf for the plain-text run sharing that
    // string_content span ("hi" here) - only the escape_sequence itself is
    // a leaf, so the original child_count()==0-only walk left "hi"
    // uncolored. Phase 7n1's isAtomicNode() generalization (added for
    // tree-sitter-rust's non-leaf line_comment/block_comment - see
    // namedLeafKindsForRust()'s comment) treats ANY named node whose type
    // has a LeafKindTable entry as atomic, and "string_content" already had
    // one (for the no-escape-sequence case below) - so this non-leaf
    // string_content now also stops descent and colors its WHOLE span
    // ("hi\n", escape sequence included) as one String token instead of
    // leaving a gap. An accidental but genuine improvement, not a deliberate
    // Phase 7n1 goal - documented here so it isn't mistaken for a regression.
    const std::vector<Token> tokens = parsePython(u"s = \"hi\\n\"\n");
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Variable);   // s
    EXPECT_EQ(tokens[1].kind, TokenKind::Punctuation);  // =
    EXPECT_EQ(tokens[2].kind, TokenKind::String);    // opening quote (string_start)
    EXPECT_EQ(tokens[2].range.start, 4u);
    EXPECT_EQ(tokens[2].range.end, 5u);
    EXPECT_EQ(tokens[3].kind, TokenKind::String);    // "hi\n" (string_content, now merged whole)
    EXPECT_EQ(tokens[3].range.start, 5u);
    EXPECT_EQ(tokens[3].range.end, 9u);
    EXPECT_EQ(tokens[4].kind, TokenKind::String);    // closing quote (string_end)
}

TEST(SyntaxParsePythonTest, ClassifiesFStringInterpolationLeavingInterpolatedExprUnstyled) {
    // t = f"hi {x}" - the interpolated identifier `x` is a plain `identifier`
    // leaf (Variable), NOT part of the string - it and the surrounding '{'/
    // '}' punctuation are visually distinct from the String-colored parts,
    // matching how VSCode/most editors render f-string interpolation.
    const std::vector<Token> tokens = parsePython(u"t = f\"hi {x}\"\n");
    ASSERT_EQ(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Variable);   // t
    EXPECT_EQ(tokens[1].kind, TokenKind::Punctuation);  // =
    EXPECT_EQ(tokens[2].kind, TokenKind::String);    // f" (string_start)
    EXPECT_EQ(tokens[3].kind, TokenKind::String);    // "hi " (string_content, no escape -> a leaf)
    EXPECT_EQ(tokens[3].range.start, 6u);
    EXPECT_EQ(tokens[3].range.end, 9u);
    EXPECT_EQ(tokens[4].kind, TokenKind::Punctuation);  // {
    EXPECT_EQ(tokens[5].kind, TokenKind::Variable);   // x
    EXPECT_EQ(tokens[6].kind, TokenKind::Punctuation);  // }
    EXPECT_EQ(tokens[7].kind, TokenKind::String);    // " (string_end)
}

TEST(SyntaxParsePythonTest, ClassifiesTrueFalseNoneEllipsisAsKeyword) {
    // Colored the same as `true`/`false`/`this`/`null` are for C++ above -
    // these are named leaf "constant" nodes (true/false/none/ellipsis in
    // tree-sitter-python's grammar), not anonymous alphabetic tokens, so
    // they need their own namedLeafKindsForPython() entries (verified via
    // probe; ellipsis's shape was confirmed to be a named leaf exactly like
    // the other three, not falling through to Text).
    const std::vector<Token> tokens = parsePython(u"a = True\nb = False\nc = None\nd = ...\n");
    ASSERT_EQ(tokens.size(), 12u);
    EXPECT_EQ(tokens[2].kind, TokenKind::Keyword);  // True
    EXPECT_EQ(tokens[5].kind, TokenKind::Keyword);  // False
    EXPECT_EQ(tokens[8].kind, TokenKind::Keyword);  // None
    EXPECT_EQ(tokens[11].kind, TokenKind::Keyword);  // ...
    EXPECT_EQ(tokens[11].range.start, 32u);
    EXPECT_EQ(tokens[11].range.end, 35u);
}

TEST(SyntaxParsePythonTest, ClassifiesDecoratorDefDocstringAndNumbers) {
    const std::u16string source =
        u"# leading comment\n"
        u"@decorator\n"
        u"def foo(x: int, y=42) -> bool:\n"
        u"    \"\"\"docstring\"\"\"\n"
        u"    return x + y * 3.14\n";
    const std::vector<Token> tokens = parsePython(source);
    ASSERT_EQ(tokens.size(), 26u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);       // # leading comment
    EXPECT_EQ(tokens[1].kind, TokenKind::Punctuation);   // @
    EXPECT_EQ(tokens[2].kind, TokenKind::Variable);      // decorator
    EXPECT_EQ(tokens[3].kind, TokenKind::Keyword);       // def
    EXPECT_EQ(tokens[4].kind, TokenKind::Variable);      // foo
    // "int" in `x: int` is a plain identifier - Python's grammar has no
    // separate type-annotation node at the leaf level (see
    // namedLeafKindsForPython()'s comment) - so it colors as Variable, not
    // Type, unlike C++'s primitive_type/type_identifier.
    EXPECT_EQ(tokens[8].kind, TokenKind::Variable);      // int
    EXPECT_EQ(tokens[12].kind, TokenKind::Number);       // 42
    EXPECT_EQ(tokens[14].kind, TokenKind::Punctuation);  // ->
    EXPECT_EQ(tokens[17].kind, TokenKind::String);       // """ (string_start)
    EXPECT_EQ(tokens[18].kind, TokenKind::String);       // docstring (string_content)
    EXPECT_EQ(tokens[19].kind, TokenKind::String);       // """ (string_end)
    EXPECT_EQ(tokens[20].kind, TokenKind::Keyword);      // return
    EXPECT_EQ(tokens[25].kind, TokenKind::Number);       // 3.14
    EXPECT_EQ(tokens.back().range.end, source.size() - 1);  // excludes trailing \n
}

TEST(SyntaxParsePythonTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parsePython(u"def foo(:\n    ???\n");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParsePythonTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"# 日本語コメント\n";
    const std::vector<Token> tokens = parsePython(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.start, 0u);
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);  // excludes trailing \n
}

TEST(SyntaxParsePythonTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens =
        parsePython(u"async def foo():\n"
                    u"    await bar()\n"
                    u"g = lambda x: x + 1\n"
                    u"if (n := 10) > 5:\n"
                    u"    pass\n"
                    u"squares = [x * x for x in range(10) if x % 2 == 0]\n"
                    u"ok = a and not b or c is None\n"
                    u"m = a @ b\n"
                    u"e = ...\n");
    ASSERT_FALSE(tokens.empty());
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start)
            << "token " << i - 1 << " overlaps token " << i;
    }
}

// Phase 7n1 (batch 1: C/JavaScript/Java/Go/Rust/Json). Every expectation
// below was cross-checked against real tree-sitter output for that grammar
// via a standalone probe before being written (CLAUDE.md rule 3), same
// methodology as Phase 7a/7d above.

TEST(SyntaxParseCTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseC(u"").empty());
}

TEST(SyntaxParseCTest, ClassifiesTypeIdentifierNumberAndPunctuation) {
    // C shares tree-sitter-cpp's primitive_type/identifier/number_literal
    // node names for this shape (verified via probe) - same token sequence
    // as SyntaxParseCppTest.ClassifiesTypeIdentifierNumberAndPunctuation.
    const std::vector<Token> tokens = parseC(u"int x = 42;");
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Type);
    EXPECT_EQ(tokens[1].kind, TokenKind::Variable);
    EXPECT_EQ(tokens[2].kind, TokenKind::Punctuation);
    EXPECT_EQ(tokens[3].kind, TokenKind::Number);
    EXPECT_EQ(tokens[4].kind, TokenKind::Punctuation);
}

TEST(SyntaxParseCTest, ClassifiesLineAndBlockComment) {
    EXPECT_EQ(parseC(u"// hello\n").size(), 1u);
    EXPECT_EQ(parseC(u"// hello\n")[0].kind, TokenKind::Comment);
    const std::vector<Token> block = parseC(u"/* a\nb */");
    ASSERT_EQ(block.size(), 1u);
    EXPECT_EQ(block[0].kind, TokenKind::Comment);
}

TEST(SyntaxParseCTest, ClassifiesPreprocessorInclude) {
    const std::vector<Token> tokens = parseC(u"#include <stdio.h>\n");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Preprocessor);  // "#include" anonymous token
    EXPECT_EQ(tokens[1].kind, TokenKind::String);         // system_lib_string
}

TEST(SyntaxParseCTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseC(u"int main( {{{ ???");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseCTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"// 日本語コメント\n";
    const std::vector<Token> tokens = parseC(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);  // excludes trailing \n
}

TEST(SyntaxParseCTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens = parseC(u"int main(void) { return 0; }");
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseJavaScriptTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseJavaScript(u"").empty());
}

TEST(SyntaxParseJavaScriptTest, ClassifiesLineAndBlockComment) {
    EXPECT_EQ(parseJavaScript(u"// hello\n")[0].kind, TokenKind::Comment);
    EXPECT_EQ(parseJavaScript(u"/* a\nb */")[0].kind, TokenKind::Comment);
}

TEST(SyntaxParseJavaScriptTest, ClassifiesStringWithEscapeSequence) {
    // "hi\n" -> string(open) string_fragment(hi) escape_sequence(\n) string(close)
    const std::vector<Token> tokens = parseJavaScript(u"\"hi\\n\"");
    ASSERT_EQ(tokens.size(), 4u);
    for (const Token& token : tokens) {
        EXPECT_EQ(token.kind, TokenKind::String);
    }
}

TEST(SyntaxParseJavaScriptTest, ClassifiesNumberAndIdentifier) {
    const std::vector<Token> tokens = parseJavaScript(u"let n = 42.5;");
    // let(Keyword, anonymous+alpha) n(Variable) =(Punctuation) 42.5(Number) ;(Punctuation)
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Keyword);
    EXPECT_EQ(tokens[1].kind, TokenKind::Variable);
    EXPECT_EQ(tokens[3].kind, TokenKind::Number);
}

TEST(SyntaxParseJavaScriptTest, ClassifiesTrueFalseNullUndefinedThisAsKeyword) {
    // Named leaf nodes (not anonymous), unlike most other keywords in this
    // grammar - verified via probe (see namedLeafKindsForJavaScript()).
    const std::vector<Token> t = parseJavaScript(u"true;false;null;undefined;this;");
    ASSERT_EQ(t.size(), 10u);
    EXPECT_EQ(t[0].kind, TokenKind::Keyword);
    EXPECT_EQ(t[2].kind, TokenKind::Keyword);
    EXPECT_EQ(t[4].kind, TokenKind::Keyword);
    EXPECT_EQ(t[6].kind, TokenKind::Keyword);
    EXPECT_EQ(t[8].kind, TokenKind::Keyword);
}

TEST(SyntaxParseJavaScriptTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseJavaScript(u"function( {{{ ???");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseJavaScriptTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"// 日本語コメント\n";
    const std::vector<Token> tokens = parseJavaScript(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);
}

TEST(SyntaxParseJavaScriptTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens = parseJavaScript(u"function f(a) { return a + 1; }");
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseJavaTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseJava(u"").empty());
}

TEST(SyntaxParseJavaTest, ClassifiesLineAndBlockCommentAsDistinctNodeTypes) {
    // Unlike Cpp/C/JavaScript/Go's single shared "comment" node type, Java's
    // grammar splits line/block comments into two distinct named leaf types
    // (verified via probe) - both still map to TokenKind::Comment.
    EXPECT_EQ(parseJava(u"// hello\n")[0].kind, TokenKind::Comment);
    EXPECT_EQ(parseJava(u"/* a\nb */")[0].kind, TokenKind::Comment);
}

TEST(SyntaxParseJavaTest, ClassifiesStringCharAndIntLiteral) {
    const std::vector<Token> str = parseJava(u"\"hi\\n\"");
    ASSERT_EQ(str.size(), 4u);  // open quote, string_fragment, escape_sequence, close quote
    for (const Token& token : str) {
        EXPECT_EQ(token.kind, TokenKind::String);
    }
    // Java's character_literal is ONE leaf (unlike C/C++'s quote+char+quote
    // decomposition) - verified via probe.
    const std::vector<Token> ch = parseJava(u"'x'");
    ASSERT_EQ(ch.size(), 1u);
    EXPECT_EQ(ch[0].kind, TokenKind::String);
}

TEST(SyntaxParseJavaTest, ClassifiesTypeIdentifierTrueFalseNullThis) {
    const std::vector<Token> tokens = parseJava(u"String s = null; boolean b = true; Object o = this;");
    const auto typeIt = std::ranges::find(tokens, TokenKind::Type, &Token::kind);
    ASSERT_NE(typeIt, tokens.end());
    const auto keywordCount =
        std::ranges::count(tokens, TokenKind::Keyword, &Token::kind);
    EXPECT_GE(keywordCount, 3);  // at least null/true/this
}

TEST(SyntaxParseJavaTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseJava(u"class Foo { void bar( {{{ ??? }");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseJavaTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"// 日本語コメント\n";
    const std::vector<Token> tokens = parseJava(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);
}

TEST(SyntaxParseJavaTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens = parseJava(u"public class Foo { int x = 1; }");
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseGoTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseGo(u"").empty());
}

TEST(SyntaxParseGoTest, ClassifiesComment) {
    EXPECT_EQ(parseGo(u"// hello\n")[0].kind, TokenKind::Comment);
    EXPECT_EQ(parseGo(u"/* a\nb */")[0].kind, TokenKind::Comment);
}

TEST(SyntaxParseGoTest, ClassifiesInterpretedAndRawStringLiterals) {
    const std::vector<Token> interpreted = parseGo(u"\"hi\\n\"");
    ASSERT_EQ(interpreted.size(), 4u);  // open quote, content, escape_sequence, close quote
    for (const Token& token : interpreted) {
        EXPECT_EQ(token.kind, TokenKind::String);
    }
    const std::vector<Token> raw = parseGo(u"`raw`");
    ASSERT_EQ(raw.size(), 3u);  // open backtick, raw_string_literal_content, close backtick
    for (const Token& token : raw) {
        EXPECT_EQ(token.kind, TokenKind::String);
    }
}

TEST(SyntaxParseGoTest, ClassifiesPackageAndTypeIdentifierAsType) {
    const std::vector<Token> tokens = parseGo(u"package main\ntype Point struct { X int }\n");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[1].kind, TokenKind::Type);  // package_identifier "main"
    const auto typeIt = std::ranges::find(tokens, TokenKind::Type, &Token::kind);
    ASSERT_NE(typeIt, tokens.end());
}

TEST(SyntaxParseGoTest, ClassifiesTrueFalseNilAsKeyword) {
    const std::vector<Token> tokens = parseGo(u"package main\nvar x = true\nvar y = false\nvar z = nil\n");
    const auto keywordCount = std::ranges::count(tokens, TokenKind::Keyword, &Token::kind);
    EXPECT_GE(keywordCount, 3);
}

TEST(SyntaxParseGoTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseGo(u"func main( {{{ ???");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseGoTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"// 日本語コメント\n";
    const std::vector<Token> tokens = parseGo(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);
}

TEST(SyntaxParseGoTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens = parseGo(u"package main\nfunc main() { x := 1 }\n");
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseRustTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseRust(u"").empty());
}

TEST(SyntaxParseRustTest, ClassifiesLineAndBlockCommentAsOneAtomicTokenEach) {
    // The critical regression test for this batch (see
    // namedLeafKindsForRust()'s and isAtomicNode()'s comments): tree-sitter-
    // rust's line_comment/block_comment are NOT leaf nodes (they wrap
    // anonymous "//"/"/*"+"*/"" delimiter children with the body text
    // uncaptured by any child) - without the isAtomicNode() generalization,
    // this would produce a stray Punctuation token for just the delimiter
    // and silently drop the comment body from the token stream entirely.
    const std::vector<Token> line = parseRust(u"// hello\n");
    ASSERT_EQ(line.size(), 1u);
    EXPECT_EQ(line[0].kind, TokenKind::Comment);
    EXPECT_EQ(line[0].range.start, 0u);
    EXPECT_EQ(line[0].range.end, 8u);  // covers the WHOLE "// hello", not just "//"

    const std::vector<Token> block = parseRust(u"/* a\nb */");
    ASSERT_EQ(block.size(), 1u);
    EXPECT_EQ(block[0].kind, TokenKind::Comment);
    EXPECT_EQ(block[0].range.start, 0u);
    EXPECT_EQ(block[0].range.end, 9u);  // covers the WHOLE "/* a\nb */"
}

TEST(SyntaxParseRustTest, ClassifiesStringCharAndNumberLiterals) {
    const std::vector<Token> str = parseRust(u"\"hi\\n\"");
    ASSERT_EQ(str.size(), 4u);  // open quote, string_content, escape_sequence, close quote
    for (const Token& token : str) {
        EXPECT_EQ(token.kind, TokenKind::String);
    }
    const std::vector<Token> ch = parseRust(u"'x'");
    ASSERT_EQ(ch.size(), 1u);  // char_literal is a single leaf (verified via probe)
    EXPECT_EQ(ch[0].kind, TokenKind::String);
    const std::vector<Token> num = parseRust(u"let n: i32 = 42;");
    const auto numberIt = std::ranges::find(num, TokenKind::Number, &Token::kind);
    ASSERT_NE(numberIt, num.end());
    const auto typeIt = std::ranges::find(num, TokenKind::Type, &Token::kind);
    ASSERT_NE(typeIt, num.end());  // i32 (primitive_type)
}

TEST(SyntaxParseRustTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseRust(u"fn main( {{{ ???");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseRustTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"// 日本語コメント\n";
    const std::vector<Token> tokens = parseRust(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    // The atomic-comment-node span includes the trailing \n here since
    // line_comment's own node range (unlike C++/Python's leaf `comment`)
    // extends to just before the newline the same way - confirmed via probe
    // that this matches the other languages' "excludes trailing \n" shape.
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);
}

TEST(SyntaxParseRustTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens = parseRust(u"// leading comment\nfn main() { let x = 1; }\n");
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start)
            << "token " << i - 1 << " overlaps token " << i;
    }
}

TEST(SyntaxParseJsonTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseJson(u"").empty());
}

TEST(SyntaxParseJsonTest, ClassifiesStringNumberAndLiterals) {
    const std::vector<Token> tokens =
        parseJson(u"{\"a\": \"b\", \"n\": 42, \"ok\": true, \"bad\": false, \"z\": null}");
    const auto stringIt = std::ranges::find(tokens, TokenKind::String, &Token::kind);
    ASSERT_NE(stringIt, tokens.end());
    const auto numberIt = std::ranges::find(tokens, TokenKind::Number, &Token::kind);
    ASSERT_NE(numberIt, tokens.end());
    const auto keywordCount = std::ranges::count(tokens, TokenKind::Keyword, &Token::kind);
    EXPECT_EQ(keywordCount, 3);  // true, false, null
}

TEST(SyntaxParseJsonTest, ClassifiesNestedArrayAndObject) {
    const std::vector<Token> tokens = parseJson(u"{\"list\": [1, 2, {\"n\": 3}]}");
    const auto numberCount = std::ranges::count(tokens, TokenKind::Number, &Token::kind);
    EXPECT_EQ(numberCount, 3);  // 1, 2, 3
}

TEST(SyntaxParseJsonTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseJson(u"{\"a\": ???");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseJsonTest, HandlesJapaneseStringContentWithCorrectUtf16Ranges) {
    // JSON has no comments - exercise the same UTF-16 range correctness
    // concern via a string value instead.
    const std::u16string source = u"{\"s\": \"日本語\"}";
    const std::vector<Token> tokens = parseJson(source);
    const auto stringIt = std::ranges::find_if(
        tokens, [&](const Token& t) { return t.kind == TokenKind::String && t.range.end - t.range.start == 3; });
    ASSERT_NE(stringIt, tokens.end());
}

TEST(SyntaxParseJsonTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::vector<Token> tokens = parseJson(u"{\"a\": [1, 2], \"b\": {\"c\": true}}");
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseDispatcherTest, ParseWithCppLanguageMatchesParseCpp) {
    const std::u16string source = u"int x = 42; // comment\n";
    EXPECT_EQ(parse(source, Language::Cpp), parseCpp(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithPythonLanguageMatchesParsePython) {
    const std::u16string source = u"def foo(): return 42\n";
    EXPECT_EQ(parse(source, Language::Python), parsePython(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithCLanguageMatchesParseC) {
    const std::u16string source = u"int x = 42; // comment\n";
    EXPECT_EQ(parse(source, Language::C), parseC(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithJavaScriptLanguageMatchesParseJavaScript) {
    const std::u16string source = u"function f() { return 42; }\n";
    EXPECT_EQ(parse(source, Language::JavaScript), parseJavaScript(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithJavaLanguageMatchesParseJava) {
    const std::u16string source = u"class Foo { int x = 42; }\n";
    EXPECT_EQ(parse(source, Language::Java), parseJava(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithGoLanguageMatchesParseGo) {
    const std::u16string source = u"package main\nfunc main() {}\n";
    EXPECT_EQ(parse(source, Language::Go), parseGo(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithRustLanguageMatchesParseRust) {
    const std::u16string source = u"fn main() { let x = 42; }\n";
    EXPECT_EQ(parse(source, Language::Rust), parseRust(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithJsonLanguageMatchesParseJson) {
    const std::u16string source = u"{\"a\": 42}";
    EXPECT_EQ(parse(source, Language::Json), parseJson(source));
}

// Phase 7r (batch 2: Html/Css/Shell/Yaml/Toml/Xml). Every expectation below
// was cross-checked against real tree-sitter output for that grammar via a
// standalone probe before being written (CLAUDE.md rule 3); comprehensive
// tests reuse the EXACT probe input string so the recorded token
// count/kinds/ranges are not re-derived speculatively.

TEST(SyntaxParseHtmlTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseHtml(u"").empty());
}

TEST(SyntaxParseHtmlTest, ClassifiesTagAttributeCommentEntityAndScriptBody) {
    const std::u16string source =
        u"<div class=\"a\">\n<!-- comment -->\nText &amp; more\n<script>var x = 1;</script>\n</div>\n";
    const std::vector<Token> tokens = parseHtml(source);
    ASSERT_EQ(tokens.size(), 22u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Punctuation);  // <
    EXPECT_EQ(tokens[1].kind, TokenKind::Type);          // div (tag_name)
    EXPECT_EQ(tokens[1].range.start, 1u);
    EXPECT_EQ(tokens[1].range.end, 4u);
    EXPECT_EQ(tokens[2].kind, TokenKind::Variable);      // class (attribute_name)
    EXPECT_EQ(tokens[5].kind, TokenKind::String);        // a (attribute_value)
    EXPECT_EQ(tokens[8].kind, TokenKind::Comment);       // <!-- comment -->
    EXPECT_EQ(tokens[8].range.start, 16u);
    EXPECT_EQ(tokens[8].range.end, 32u);
    EXPECT_EQ(tokens[9].kind, TokenKind::Text);          // "Text" (unclassified text leaf)
    EXPECT_EQ(tokens[10].kind, TokenKind::String);       // &amp; (entity)
    EXPECT_EQ(tokens[13].kind, TokenKind::Type);         // script (tag_name)
    EXPECT_EQ(tokens[15].kind, TokenKind::Text);         // var x = 1; (raw_text, unclassified)
    EXPECT_EQ(tokens[21].kind, TokenKind::Punctuation);  // final >
}

TEST(SyntaxParseHtmlTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseHtml(u"<div class=<<<");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseHtmlTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"<div><!-- 日本語コメント --></div>";
    const std::vector<Token> tokens = parseHtml(source);
    const auto it = std::ranges::find(tokens, TokenKind::Comment, &Token::kind);
    ASSERT_NE(it, tokens.end());
    EXPECT_EQ(it->range.start, 5u);
    EXPECT_EQ(it->range.end, 21u);
}

TEST(SyntaxParseHtmlTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::u16string source =
        u"<div class=\"a\">\n<!-- comment -->\nText &amp; more\n<script>var x = 1;</script>\n</div>\n";
    const std::vector<Token> tokens = parseHtml(source);
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseCssTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseCss(u"").empty());
}

TEST(SyntaxParseCssTest, ClassifiesSelectorsDeclarationsCommentAndString) {
    const std::u16string source =
        u".foo {\n  color: red; /* comment */\n}\n#bar::before {\n  content: \"hi\";\n}\n";
    const std::vector<Token> tokens = parseCss(source);
    ASSERT_EQ(tokens.size(), 21u);
    EXPECT_EQ(tokens[1].kind, TokenKind::Variable);  // foo (class_name identifier)
    EXPECT_EQ(tokens[3].kind, TokenKind::Variable);  // color (property_name)
    EXPECT_EQ(tokens[5].kind, TokenKind::Text);      // red (plain_value, unclassified)
    EXPECT_EQ(tokens[7].kind, TokenKind::Comment);   // /* comment */
    EXPECT_EQ(tokens[7].range.start, 21u);
    EXPECT_EQ(tokens[7].range.end, 34u);
    EXPECT_EQ(tokens[10].kind, TokenKind::Type);  // bar (id_name)
    EXPECT_EQ(tokens[12].kind, TokenKind::Type);  // before (pseudo-element tag_name)
    EXPECT_EQ(tokens[17].kind, TokenKind::String);  // hi (string_content)
    EXPECT_EQ(tokens[17].range.start, 64u);
    EXPECT_EQ(tokens[17].range.end, 66u);
}

TEST(SyntaxParseCssTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseCss(u".foo { color: <<<");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseCssTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u".foo { /* 日本語コメント */ }";
    const std::vector<Token> tokens = parseCss(source);
    const auto it = std::ranges::find(tokens, TokenKind::Comment, &Token::kind);
    ASSERT_NE(it, tokens.end());
    EXPECT_EQ(it->range.start, 7u);
    EXPECT_EQ(it->range.end, 20u);
}

TEST(SyntaxParseCssTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::u16string source =
        u".foo {\n  color: red; /* comment */\n}\n#bar::before {\n  content: \"hi\";\n}\n";
    const std::vector<Token> tokens = parseCss(source);
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseShellTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseShell(u"").empty());
}

TEST(SyntaxParseShellTest, ClassifiesShebangCommentStringExpansionAndForLoop) {
    const std::u16string source =
        u"#!/bin/bash\n# comment\necho \"hello $USER\"\nfor i in 1 2 3; do\n  echo $i\ndone\nx=42\n";
    const std::vector<Token> tokens = parseShell(source);
    ASSERT_EQ(tokens.size(), 23u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);   // shebang line - also just a "comment" node
    EXPECT_EQ(tokens[0].range.start, 0u);
    EXPECT_EQ(tokens[0].range.end, 11u);
    EXPECT_EQ(tokens[1].kind, TokenKind::Comment);   // # comment
    EXPECT_EQ(tokens[2].kind, TokenKind::Variable);  // echo (command_name -> word)
    EXPECT_EQ(tokens[6].kind, TokenKind::Variable);  // USER (variable_name inside $USER)
    EXPECT_EQ(tokens[8].kind, TokenKind::Keyword);   // for
    EXPECT_EQ(tokens[11].kind, TokenKind::Number);   // 1
    EXPECT_EQ(tokens[12].kind, TokenKind::Number);   // 2
    EXPECT_EQ(tokens[13].kind, TokenKind::Number);   // 3
    EXPECT_EQ(tokens[19].kind, TokenKind::Keyword);  // done
    EXPECT_EQ(tokens[22].kind, TokenKind::Number);   // 42
}

TEST(SyntaxParseShellTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseShell(u"if [ <<<");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseShellTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"# 日本語コメント\n";
    const std::vector<Token> tokens = parseShell(source);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Comment);
    EXPECT_EQ(tokens[0].range.start, 0u);
    EXPECT_EQ(tokens[0].range.end, source.size() - 1);  // excludes trailing \n
}

TEST(SyntaxParseShellTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::u16string source =
        u"#!/bin/bash\n# comment\necho \"hello $USER\"\nfor i in 1 2 3; do\n  echo $i\ndone\nx=42\n";
    const std::vector<Token> tokens = parseShell(source);
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseYamlTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseYaml(u"").empty());
}

TEST(SyntaxParseYamlTest, ClassifiesScalarsCommentSequenceAndTypedValues) {
    // tree-sitter-yaml does not distinguish a mapping key from a plain
    // scalar value at the node-type level - both are "string_scalar"
    // (verified via probe) - so "key" below colors as String, same as
    // "value" (see namedLeafKindsForYaml()'s comment).
    const std::u16string source =
        u"key: value\n# comment\nlist:\n  - item1\n  - item2\nnum: 42\nbool: true\nnil_val: null\n";
    const std::vector<Token> tokens = parseYaml(source);
    ASSERT_EQ(tokens.size(), 19u);
    EXPECT_EQ(tokens[0].kind, TokenKind::String);   // key
    EXPECT_EQ(tokens[2].kind, TokenKind::String);   // value
    EXPECT_EQ(tokens[3].kind, TokenKind::Comment);  // # comment
    EXPECT_EQ(tokens[3].range.start, 11u);
    EXPECT_EQ(tokens[3].range.end, 20u);
    EXPECT_EQ(tokens[7].kind, TokenKind::String);   // item1
    EXPECT_EQ(tokens[9].kind, TokenKind::String);   // item2
    EXPECT_EQ(tokens[12].kind, TokenKind::Number);  // 42
    EXPECT_EQ(tokens[15].kind, TokenKind::Keyword);  // true (boolean_scalar)
    EXPECT_EQ(tokens[18].kind, TokenKind::Keyword);  // null (null_scalar)
}

TEST(SyntaxParseYamlTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseYaml(u"key: : : broken\n");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseYamlTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"key: value\n# 日本語コメント\n";
    const std::vector<Token> tokens = parseYaml(source);
    const auto it = std::ranges::find(tokens, TokenKind::Comment, &Token::kind);
    ASSERT_NE(it, tokens.end());
    EXPECT_EQ(it->range.start, 11u);
    EXPECT_EQ(it->range.end, 20u);
}

TEST(SyntaxParseYamlTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::u16string source =
        u"key: value\n# comment\nlist:\n  - item1\n  - item2\nnum: 42\nbool: true\nnil_val: null\n";
    const std::vector<Token> tokens = parseYaml(source);
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseTomlTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseToml(u"").empty());
}

TEST(SyntaxParseTomlTest, ClassifiesTableKeysStringCommentAndTypedValues) {
    const std::u16string source =
        u"[section]\nkey = \"value\" # comment\nnum = 42\nbool = true\n\n[[array]]\nx = 1\n";
    const std::vector<Token> tokens = parseToml(source);
    ASSERT_EQ(tokens.size(), 19u);
    EXPECT_EQ(tokens[1].kind, TokenKind::Variable);  // section (bare_key)
    EXPECT_EQ(tokens[3].kind, TokenKind::Variable);  // key
    // "string" is a non-leaf node (2 quote-only children, no separate
    // content child - verified via probe) that isAtomicNode() must treat as
    // ONE token, or the quoted text would be silently dropped (see
    // namedLeafKindsForToml()'s comment).
    EXPECT_EQ(tokens[5].kind, TokenKind::String);
    EXPECT_EQ(tokens[5].range.start, 16u);
    EXPECT_EQ(tokens[5].range.end, 23u);
    EXPECT_EQ(tokens[6].kind, TokenKind::Comment);   // # comment
    EXPECT_EQ(tokens[9].kind, TokenKind::Number);    // 42
    EXPECT_EQ(tokens[12].kind, TokenKind::Keyword);  // true
    EXPECT_EQ(tokens[14].kind, TokenKind::Variable);  // array (table_array_element bare_key)
    EXPECT_EQ(tokens[18].kind, TokenKind::Number);   // 1
}

TEST(SyntaxParseTomlTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseToml(u"key = = broken\n");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseTomlTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"key = 1 # 日本語コメント\n";
    const std::vector<Token> tokens = parseToml(source);
    const auto it = std::ranges::find(tokens, TokenKind::Comment, &Token::kind);
    ASSERT_NE(it, tokens.end());
    EXPECT_EQ(it->range.start, 8u);
    EXPECT_EQ(it->range.end, 17u);
}

TEST(SyntaxParseTomlTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::u16string source =
        u"[section]\nkey = \"value\" # comment\nnum = 42\nbool = true\n\n[[array]]\nx = 1\n";
    const std::vector<Token> tokens = parseToml(source);
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseXmlTest, EmptyTextProducesNoTokens) {
    EXPECT_TRUE(parseXml(u"").empty());
}

TEST(SyntaxParseXmlTest, ClassifiesDeclElementAttributeAndComment) {
    const std::u16string source =
        u"<?xml version=\"1.0\"?>\n<root attr=\"val\">\n<!-- comment -->\nText\n</root>\n";
    const std::vector<Token> tokens = parseXml(source);
    ASSERT_EQ(tokens.size(), 20u);
    EXPECT_EQ(tokens[9].kind, TokenKind::Type);  // root (element Name)
    // tree-sitter-xml's grammar reuses "Name" for BOTH element tag names and
    // attribute names (no separate AttributeName node type - verified via
    // probe), so attr also colors as Type - an accepted grammar-level
    // limitation (see namedLeafKindsForXml()'s comment).
    EXPECT_EQ(tokens[10].kind, TokenKind::Type);  // attr (attribute Name)
    // AttValue is a non-leaf node (2 quote-only children, no separate
    // content child) - same situation as TOML's "string" above; without the
    // table entry the quoted "val" text would be silently dropped.
    EXPECT_EQ(tokens[12].kind, TokenKind::String);
    EXPECT_EQ(tokens[12].range.start, 33u);
    EXPECT_EQ(tokens[12].range.end, 38u);
    EXPECT_EQ(tokens[15].kind, TokenKind::Comment);  // <!-- comment -->
    EXPECT_EQ(tokens[15].range.start, 40u);
    EXPECT_EQ(tokens[15].range.end, 56u);
    EXPECT_EQ(tokens[18].kind, TokenKind::Type);  // root (closing tag Name)
}

TEST(SyntaxParseXmlTest, MalformedInputDoesNotCrashAndStillYieldsTokens) {
    const std::vector<Token> tokens = parseXml(u"<root attr=<<<");
    EXPECT_FALSE(tokens.empty());
}

TEST(SyntaxParseXmlTest, HandlesJapaneseCommentTextWithCorrectUtf16Ranges) {
    const std::u16string source = u"<root><!-- 日本語コメント --></root>";
    const std::vector<Token> tokens = parseXml(source);
    const auto it = std::ranges::find(tokens, TokenKind::Comment, &Token::kind);
    ASSERT_NE(it, tokens.end());
    EXPECT_EQ(it->range.start, 6u);
    EXPECT_EQ(it->range.end, 22u);
}

TEST(SyntaxParseXmlTest, TokensAreOrderedLeftToRightAndNonOverlapping) {
    const std::u16string source =
        u"<?xml version=\"1.0\"?>\n<root attr=\"val\">\n<!-- comment -->\nText\n</root>\n";
    const std::vector<Token> tokens = parseXml(source);
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        EXPECT_LE(tokens[i - 1].range.end, tokens[i].range.start);
    }
}

TEST(SyntaxParseDispatcherTest, ParseWithHtmlLanguageMatchesParseHtml) {
    const std::u16string source = u"<div class=\"a\"></div>";
    EXPECT_EQ(parse(source, Language::Html), parseHtml(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithCssLanguageMatchesParseCss) {
    const std::u16string source = u".foo { color: red; }";
    EXPECT_EQ(parse(source, Language::Css), parseCss(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithShellLanguageMatchesParseShell) {
    const std::u16string source = u"echo hello\n";
    EXPECT_EQ(parse(source, Language::Shell), parseShell(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithYamlLanguageMatchesParseYaml) {
    const std::u16string source = u"key: value\n";
    EXPECT_EQ(parse(source, Language::Yaml), parseYaml(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithTomlLanguageMatchesParseToml) {
    const std::u16string source = u"key = 42\n";
    EXPECT_EQ(parse(source, Language::Toml), parseToml(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithXmlLanguageMatchesParseXml) {
    const std::u16string source = u"<root></root>";
    EXPECT_EQ(parse(source, Language::Xml), parseXml(source));
}

}  // namespace
