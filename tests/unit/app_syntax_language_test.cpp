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
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.md")), std::nullopt);
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"foo.ts")), std::nullopt);
}

TEST(DetectLanguageTest, RejectsFileNameWithNoExtension) {
    EXPECT_EQ(detectLanguage(std::filesystem::path(L"Makefile")), std::nullopt);
}

TEST(DetectLanguageTest, RejectsEmptyPath) {
    EXPECT_EQ(detectLanguage(std::filesystem::path()), std::nullopt);
}

}  // namespace
