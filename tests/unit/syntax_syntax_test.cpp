#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "neomifes/syntax/syntax.h"

namespace {

using neomifes::syntax::Language;
using neomifes::syntax::parse;
using neomifes::syntax::parseCpp;
using neomifes::syntax::parsePython;
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

TEST(SyntaxParsePythonTest, ClassifiesStringWithEscapeSequenceLeavesPlainRunUncolored) {
    // s = "hi\n" - a KNOWN, ACCEPTED gap (probed and confirmed via a full-
    // tree dump, not guessed): when string_content contains an
    // escape_sequence child, tree-sitter-python does NOT emit a separate
    // leaf for the plain-text run sharing that string_content span ("hi"
    // here) - only the escape_sequence itself is a leaf. This walker only
    // ever visits leaves (child_count()==0), so "hi" gets no token at all
    // (falls back to the default/uncolored brush) while the quotes and the
    // escape sequence still color correctly. Plain strings with NO escape
    // sequence are unaffected (string_content itself is the leaf there -
    // see ClassifiesFStringInterpolation's "hi " below).
    const std::vector<Token> tokens = parsePython(u"s = \"hi\\n\"\n");
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Variable);   // s
    EXPECT_EQ(tokens[1].kind, TokenKind::Punctuation);  // =
    EXPECT_EQ(tokens[2].kind, TokenKind::String);    // opening quote (string_start)
    EXPECT_EQ(tokens[2].range.start, 4u);
    EXPECT_EQ(tokens[2].range.end, 5u);
    EXPECT_EQ(tokens[3].kind, TokenKind::String);    // \n (escape_sequence)
    EXPECT_EQ(tokens[3].range.start, 7u);
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

TEST(SyntaxParseDispatcherTest, ParseWithCppLanguageMatchesParseCpp) {
    const std::u16string source = u"int x = 42; // comment\n";
    EXPECT_EQ(parse(source, Language::Cpp), parseCpp(source));
}

TEST(SyntaxParseDispatcherTest, ParseWithPythonLanguageMatchesParsePython) {
    const std::u16string source = u"def foo(): return 42\n";
    EXPECT_EQ(parse(source, Language::Python), parsePython(source));
}

}  // namespace
