#include <gtest/gtest.h>

#include <filesystem>

#include "neomifes/app/syntax_language.h"

namespace {

using neomifes::app::isCppSourceFile;

TEST(IsCppSourceFileTest, RecognizesAllCppSourceExtensions) {
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.cpp")));
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.cc")));
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.cxx")));
}

TEST(IsCppSourceFileTest, RecognizesAllCppHeaderExtensions) {
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.h")));
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.hpp")));
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.hxx")));
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.hh")));
}

TEST(IsCppSourceFileTest, IsCaseInsensitive) {
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.CPP")));
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.Hpp")));
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"foo.HXX")));
}

TEST(IsCppSourceFileTest, WorksWithFullAbsolutePath) {
    EXPECT_TRUE(isCppSourceFile(std::filesystem::path(L"C:\\src\\neomifes\\main.cpp")));
}

TEST(IsCppSourceFileTest, RejectsNonCppExtensions) {
    EXPECT_FALSE(isCppSourceFile(std::filesystem::path(L"foo.txt")));
    EXPECT_FALSE(isCppSourceFile(std::filesystem::path(L"foo.md")));
    EXPECT_FALSE(isCppSourceFile(std::filesystem::path(L"foo.py")));
    EXPECT_FALSE(isCppSourceFile(std::filesystem::path(L"foo.ts")));
}

TEST(IsCppSourceFileTest, RejectsFileNameWithNoExtension) {
    EXPECT_FALSE(isCppSourceFile(std::filesystem::path(L"Makefile")));
}

TEST(IsCppSourceFileTest, RejectsEmptyPath) {
    EXPECT_FALSE(isCppSourceFile(std::filesystem::path()));
}

}  // namespace
