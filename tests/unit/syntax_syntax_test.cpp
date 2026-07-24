#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "neomifes/syntax/syntax.h"

namespace {

using neomifes::syntax::parseCpp;
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

}  // namespace
