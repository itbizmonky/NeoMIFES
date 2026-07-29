#include <gtest/gtest.h>

#include <filesystem>

#include "neomifes/app/syntax_language.h"

namespace {

using neomifes::app::detectLanguage;
using neomifes::syntax::Language;

TEST(DetectLanguageTest, RecognizesAllCppSourceExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.cpp")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.cc")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.cxx")), Language::Cpp);
}

TEST(DetectLanguageTest, RecognizesAllCppHeaderExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.h")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.hpp")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.hxx")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.hh")), Language::Cpp);
}

TEST(DetectLanguageTest, RecognizesAllPythonExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.py")), Language::Python);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.pyw")), Language::Python);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.pyi")), Language::Python);
}

TEST(DetectLanguageTest, IsCaseInsensitive) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.CPP")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.Hpp")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.HXX")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.PY")), Language::Python);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.PyW")), Language::Python);
}

TEST(DetectLanguageTest, WorksWithFullAbsolutePath) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"C:\\src\\neomifes\\main.cpp")), Language::Cpp);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"C:\\src\\neomifes\\script.py")), Language::Python);
}

TEST(DetectLanguageTest, RejectsNonRecognizedExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.txt")), std::nullopt);
    // .md/.ts were rejected here before Phase 7s added Markdown/TypeScript
    // support (see RecognizesAllMarkdownExtensions/RecognizesAllTypeScript
    // Extensions below) - .sql/.ps1 remain unrecognized (no trusted
    // tree-sitter grammar wired up, see Dependencies.cmake's Phase 7r/7s
    // comments on why SQL/PowerShell were excluded).
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.sql")), std::nullopt);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.ps1")), std::nullopt);
}

TEST(DetectLanguageTest, RejectsFileNameWithNoExtension) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"Makefile")), std::nullopt);
}

TEST(DetectLanguageTest, RejectsEmptyPath) {
    EXPECT_EQ(detectLanguage(std::filesystem::path()), std::nullopt);
}

// Phase 7n1.

TEST(DetectLanguageTest, RecognizesC) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.c")), Language::C);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.C")), Language::C);
}

TEST(DetectLanguageTest, RecognizesAllJavaScriptExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.js")), Language::JavaScript);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.mjs")), Language::JavaScript);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.cjs")), Language::JavaScript);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.JS")), Language::JavaScript);
}

TEST(DetectLanguageTest, RecognizesJava) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"Foo.java")), Language::Java);
}

TEST(DetectLanguageTest, RecognizesGo) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.go")), Language::Go);
}

TEST(DetectLanguageTest, RecognizesRust) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.rs")), Language::Rust);
}

TEST(DetectLanguageTest, RecognizesJson) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.json")), Language::Json);
}

TEST(DetectLanguageTest, HeaderExtensionStillMapsToCppNotC) {
    // Deliberate, documented ambiguity (see detectLanguage()'s header
    // comment) - .h stays Cpp even though C projects use it too, preserving
    // the Phase 7d behavior rather than splitting it out to C.
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.h")), Language::Cpp);
}

// Phase 7r.

TEST(DetectLanguageTest, RecognizesAllHtmlExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.html")), Language::Html);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.htm")), Language::Html);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.HTML")), Language::Html);
}

TEST(DetectLanguageTest, RecognizesCss) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.css")), Language::Css);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.CSS")), Language::Css);
}

TEST(DetectLanguageTest, RecognizesAllShellExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.sh")), Language::Shell);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.bash")), Language::Shell);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.SH")), Language::Shell);
}

TEST(DetectLanguageTest, RecognizesAllYamlExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.yaml")), Language::Yaml);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.yml")), Language::Yaml);
}

TEST(DetectLanguageTest, RecognizesToml) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.toml")), Language::Toml);
}

TEST(DetectLanguageTest, RecognizesXml) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.xml")), Language::Xml);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.XML")), Language::Xml);
}

// Phase 7s.

TEST(DetectLanguageTest, RecognizesAllTypeScriptExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.ts")), Language::TypeScript);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.mts")), Language::TypeScript);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.cts")), Language::TypeScript);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.TS")), Language::TypeScript);
}

TEST(DetectLanguageTest, RecognizesTsx) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.tsx")), Language::Tsx);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.TSX")), Language::Tsx);
}

TEST(DetectLanguageTest, RecognizesPhp) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.php")), Language::Php);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.PHP")), Language::Php);
}

TEST(DetectLanguageTest, RecognizesAllMarkdownExtensions) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.md")), Language::Markdown);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.markdown")), Language::Markdown);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.MD")), Language::Markdown);
}

}  // namespace
