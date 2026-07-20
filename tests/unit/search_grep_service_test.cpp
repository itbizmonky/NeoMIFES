#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "neomifes/search/grep_service.h"

namespace fs = std::filesystem;

namespace {

using neomifes::document::TextRange;
using neomifes::search::GrepQuery;
using neomifes::search::GrepService;
using neomifes::search::Query;

// Creates a scratch directory tree for one test (mirrors
// document_file_loader_test.cpp's tempFileWith(), generalized to a tree
// since GrepService tests need multiple files across subdirectories) and
// removes it wholesale on destruction.
class TempGrepTree {
public:
    TempGrepTree()
        : m_root(fs::temp_directory_path() /
                 (std::string("nmfs_grep_") + std::to_string(std::rand()))) {
        fs::create_directories(m_root);
    }
    ~TempGrepTree() {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
    TempGrepTree(const TempGrepTree&)            = delete;
    TempGrepTree& operator=(const TempGrepTree&) = delete;
    TempGrepTree(TempGrepTree&&)                 = delete;
    TempGrepTree& operator=(TempGrepTree&&)      = delete;

    // Writes `bytes` to root()/relativePath, creating parent directories as
    // needed. Returns the full path written.
    fs::path writeFile(const std::string& relativePath, const std::string& bytes) const {
        const fs::path full = m_root / relativePath;
        fs::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return full;
    }

    [[nodiscard]] const fs::path& root() const noexcept { return m_root; }

private:
    fs::path m_root;
};

TEST(GrepServiceTest, FindsMatchWithCorrectPathLineColumnAndLineText) {
    const TempGrepTree tree;
    const fs::path file = tree.writeFile("a.txt", "hello world\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"world"}};
    const auto matches = GrepService::findAll(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].path, file);
    EXPECT_EQ(matches[0].line, 0U);
    EXPECT_EQ(matches[0].columnRange, (TextRange{.start = 6, .end = 11}));
    EXPECT_EQ(matches[0].lineText, u"hello world");
}

TEST(GrepServiceTest, SearchesAcrossMultipleRoots) {
    const TempGrepTree treeA;
    const TempGrepTree treeB;
    treeA.writeFile("a.txt", "needle in A\n");
    treeB.writeFile("b.txt", "needle in B\n");

    const GrepQuery query{.roots        = {treeA.root(), treeB.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    EXPECT_EQ(matches.size(), 2U);
}

TEST(GrepServiceTest, TraversesNestedSubdirectories) {
    const TempGrepTree tree;
    const fs::path file = tree.writeFile("sub/dir/deep.txt", "needle here\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].path, file);
}

TEST(GrepServiceTest, IncludeGlobFiltersToMatchingFilenamesOnly) {
    const TempGrepTree tree;
    tree.writeFile("keep.cpp", "needle\n");
    tree.writeFile("skip.h", "needle\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {u"*.cpp"},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].path.filename(), fs::path("keep.cpp"));
}

TEST(GrepServiceTest, ExcludeGlobRemovesMatchingFilenames) {
    const TempGrepTree tree;
    tree.writeFile("keep.cpp", "needle\n");
    tree.writeFile("skip.cpp", "needle\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {u"skip.*"},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].path.filename(), fs::path("keep.cpp"));
}

TEST(GrepServiceTest, ExcludeTakesPrecedenceOverIncludeOnConflict) {
    const TempGrepTree tree;
    tree.writeFile("conflict.cpp", "needle\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {u"*.cpp"},
                    .excludeGlobs = {u"conflict.*"},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    EXPECT_TRUE(matches.empty());
}

TEST(GrepServiceTest, EmptyIncludeGlobsMatchesEveryFile) {
    const TempGrepTree tree;
    tree.writeFile("a.txt", "needle\n");
    tree.writeFile("b.cpp", "needle\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    EXPECT_EQ(matches.size(), 2U);
}

TEST(GrepServiceTest, SkipsNonUtf8FileWithoutCrashingAndKeepsOtherResults) {
    const TempGrepTree tree;
    tree.writeFile("good.txt", "needle\n");
    // 0xFF/0xFE are not valid UTF-8 lead bytes anywhere - loadUtf8File()
    // reports LoadError::InvalidUtf8, so this file must be skipped rather
    // than crashing or aborting the whole query.
    tree.writeFile("bad.bin", "\xFF\xFEneedle");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].path.filename(), fs::path("good.txt"));
}

TEST(GrepServiceTest, NoMatchQueryReturnsEmptyVector) {
    const TempGrepTree tree;
    tree.writeFile("a.txt", "hello world\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"zzz_not_present"}};
    EXPECT_TRUE(GrepService::findAll(query).empty());
}

TEST(GrepServiceTest, QueryCaseSensitivityFlowsThroughUnchanged) {
    const TempGrepTree tree;
    tree.writeFile("a.txt", "Needle\n");

    const GrepQuery caseInsensitive{.roots        = {tree.root()},
                              .includeGlobs = {},
                              .excludeGlobs = {},
                              .query        = Query{.pattern = u"needle", .caseSensitive = false}};
    EXPECT_EQ(GrepService::findAll(caseInsensitive).size(), 1U);

    const GrepQuery caseSensitive{.roots        = {tree.root()},
                            .includeGlobs = {},
                            .excludeGlobs = {},
                            .query        = Query{.pattern = u"needle", .caseSensitive = true}};
    EXPECT_TRUE(GrepService::findAll(caseSensitive).empty());
}

TEST(GrepServiceTest, NonexistentRootIsSkippedButOtherRootsStillProduceResults) {
    const TempGrepTree tree;
    tree.writeFile("a.txt", "needle\n");

    const GrepQuery query{.roots        = {tree.root() / "does_not_exist", tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    EXPECT_EQ(matches.size(), 1U);
}

TEST(GrepServiceTest, RootThatIsARegularFileIsGreppedDirectly) {
    const TempGrepTree tree;
    const fs::path file = tree.writeFile("single.txt", "needle\n");

    const GrepQuery query{.roots        = {file},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].path, file);
}

TEST(GrepServiceTest, MultipleMatchesOnSameLineHaveDistinctNonOverlappingColumnRanges) {
    const TempGrepTree tree;
    tree.writeFile("a.txt", "needle needle\n");

    const GrepQuery query{.roots        = {tree.root()},
                    .includeGlobs = {},
                    .excludeGlobs = {},
                    .query        = Query{.pattern = u"needle"}};
    const auto matches = GrepService::findAll(query);

    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].columnRange, (TextRange{.start = 0, .end = 6}));
    EXPECT_EQ(matches[1].columnRange, (TextRange{.start = 7, .end = 13}));
}

}  // namespace
