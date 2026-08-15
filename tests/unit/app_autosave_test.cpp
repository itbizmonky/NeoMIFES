// app_autosave_test - headless tests for autosave.h (WI-11) against real
// temp-directory filesystem I/O (no HWND/Win32 UI involved - performAutoSave()/
// clearAutoSave()/scanForRecoverableAutoSaves() are pure filesystem + Document
// logic, same "document::saveFile() itself is pure I/O and tested this way"
// precedent WI-01's document_save_roundtrip_test.cpp established).

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "neomifes/app/autosave.h"
#include "neomifes/app/editor_session.h"
#include "neomifes/core/autosave_index.h"
#include "neomifes/document/document.h"

namespace fs = std::filesystem;

namespace {

using neomifes::app::autosaveFilePathFor;
using neomifes::app::autosaveHashFor;
using neomifes::app::clearAutoSave;
using neomifes::app::DocumentFileState;
using neomifes::app::EditorSession;
using neomifes::app::performAutoSave;
using neomifes::app::scanForRecoverableAutoSaves;
using neomifes::core::AutosaveIndex;
using neomifes::document::Document;

fs::path uniqueTempDir() {
    fs::path dir = fs::temp_directory_path() / (std::string("nmfs_autosave_") + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::unique_ptr<EditorSession> makeNamedSession(const fs::path& path, std::u16string_view content) {
    Document doc;
    doc.insertText(0, content);
    doc.markSaved();  // simulate "just loaded from `path`, currently clean"
    return std::make_unique<EditorSession>(std::move(doc), DocumentFileState{}, std::optional<fs::path>(path));
}

// --- autosaveHashFor()/autosaveFilePathFor() ---------------------------------

TEST(AutosaveTest, HashForIsDeterministicForTheSamePath) {
    const auto a = autosaveHashFor("C:/some/document.txt");
    const auto b = autosaveHashFor("C:/some/document.txt");
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.size(), 16u);  // 64-bit hash, hex-encoded
}

TEST(AutosaveTest, HashForDiffersForDifferentPaths) {
    EXPECT_NE(autosaveHashFor("C:/a.txt"), autosaveHashFor("C:/b.txt"));
}

TEST(AutosaveTest, FilePathForUsesTheHashAsStemWithTmpExtension) {
    const fs::path        dir     = "C:/appdata/autosave";
    const fs::path        docPath = "C:/some/document.txt";
    const std::u16string  hash    = autosaveHashFor(docPath);
    const std::wstring    hashW(hash.begin(), hash.end());  // ASCII-only hex digits, value-preserving
    const fs::path         expected = dir / fs::path(hashW + L".tmp");
    EXPECT_EQ(autosaveFilePathFor(dir, docPath), expected);
}

// --- performAutoSave() --------------------------------------------------------

TEST(AutosaveTest, PerformAutoSaveSkipsUntitledSession) {
    const fs::path dir = uniqueTempDir();
    EditorSession  session;  // untitled
    session.document().insertText(0, u"hello");
    AutosaveIndex index;
    performAutoSave(session, dir, index, dir / "index.json");
    EXPECT_TRUE(index.entries().empty());
    EXPECT_TRUE(fs::is_empty(dir));
    fs::remove_all(dir);
}

TEST(AutosaveTest, PerformAutoSaveSkipsWhenSessionIsNotDirty) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    writeFile(realPath, "original");
    auto session = makeNamedSession(realPath, u"original");
    ASSERT_FALSE(session->isDirty());

    AutosaveIndex index;
    performAutoSave(*session, dir, index, dir / "index.json");
    EXPECT_TRUE(index.entries().empty());
    EXPECT_FALSE(fs::exists(autosaveFilePathFor(dir, realPath)));

    fs::remove_all(dir);
}

TEST(AutosaveTest, PerformAutoSaveWritesCurrentContentAndNeverTouchesTheRealFile) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    writeFile(realPath, "the last explicitly saved content");
    auto session = makeNamedSession(realPath, u"the last explicitly saved content");
    session->document().insertText(session->document().length(), u" plus new unsaved edits");
    ASSERT_TRUE(session->isDirty());

    const fs::path indexPath = dir / "index.json";
    AutosaveIndex   index;
    performAutoSave(*session, dir, index, indexPath);

    // The real file: byte-for-byte untouched (the DoD-critical assertion).
    EXPECT_EQ(readFile(realPath), "the last explicitly saved content");
    // The autosave file: holds the current in-memory content.
    const fs::path autosavePath = autosaveFilePathFor(dir, realPath);
    ASSERT_TRUE(fs::exists(autosavePath));
    EXPECT_EQ(readFile(autosavePath), "the last explicitly saved content plus new unsaved edits");
    // The document still reports unsaved changes relative to realPath.
    EXPECT_TRUE(session->isDirty());
    // The index was updated AND persisted to disk immediately (not batched).
    ASSERT_EQ(index.entries().size(), 1u);
    EXPECT_EQ(index.entries()[0].originalPath, realPath);
    EXPECT_TRUE(fs::exists(indexPath));
    const AutosaveIndex reloaded = AutosaveIndex::loadFrom(indexPath);
    ASSERT_EQ(reloaded.entries().size(), 1u);
    EXPECT_EQ(reloaded.entries()[0].originalPath, realPath);

    fs::remove_all(dir);
}

// --- clearAutoSave() -----------------------------------------------------------

TEST(AutosaveTest, ClearAutoSaveRemovesTheTmpFileAndTheIndexEntry) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    writeFile(realPath, "original");
    auto session = makeNamedSession(realPath, u"original");
    session->document().insertText(session->document().length(), u" edited");

    const fs::path indexPath = dir / "index.json";
    AutosaveIndex   index;
    performAutoSave(*session, dir, index, indexPath);
    ASSERT_TRUE(fs::exists(autosaveFilePathFor(dir, realPath)));
    ASSERT_EQ(index.entries().size(), 1u);

    clearAutoSave(*session, dir, index, indexPath);
    EXPECT_FALSE(fs::exists(autosaveFilePathFor(dir, realPath)));
    EXPECT_TRUE(index.entries().empty());
    EXPECT_TRUE(AutosaveIndex::loadFrom(indexPath).entries().empty());

    fs::remove_all(dir);
}

TEST(AutosaveTest, ClearAutoSaveIsANoOpForAnUntitledSession) {
    const fs::path      dir = uniqueTempDir();
    const EditorSession session;
    AutosaveIndex        index;
    EXPECT_NO_THROW(clearAutoSave(session, dir, index, dir / "index.json"));
    EXPECT_TRUE(index.entries().empty());
    fs::remove_all(dir);
}

TEST(AutosaveTest, ClearAutoSaveOfAnEntryThatWasNeverWrittenIsANoOp) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    auto            session = makeNamedSession(realPath, u"content");
    AutosaveIndex   index;
    EXPECT_NO_THROW(clearAutoSave(*session, dir, index, dir / "index.json"));
    fs::remove_all(dir);
}

// --- scanForRecoverableAutoSaves() ---------------------------------------------

void setLastWriteTimeOffsetFromNow(const fs::path& path, std::chrono::seconds offset) {
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(path, now + offset);
}

TEST(AutosaveTest, ScanFindsCandidateWhenAutosaveIsNewerThanTheRealFile) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    writeFile(realPath, "real");
    setLastWriteTimeOffsetFromNow(realPath, std::chrono::seconds(-10));

    AutosaveIndex index;
    auto           session = makeNamedSession(realPath, u"real");
    session->document().insertText(session->document().length(), u" edited");
    performAutoSave(*session, dir, index, dir / "index.json");
    setLastWriteTimeOffsetFromNow(autosaveFilePathFor(dir, realPath), std::chrono::seconds(0));

    const auto candidates = scanForRecoverableAutoSaves(dir, index);
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0].originalPath, realPath);
    EXPECT_EQ(candidates[0].autosaveTmpPath, autosaveFilePathFor(dir, realPath));

    fs::remove_all(dir);
}

TEST(AutosaveTest, ScanFindsCandidateWhenTheRealFileNoLongerExists) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    writeFile(realPath, "real");

    AutosaveIndex index;
    auto           session = makeNamedSession(realPath, u"real");
    session->document().insertText(session->document().length(), u" edited");
    performAutoSave(*session, dir, index, dir / "index.json");

    fs::remove(realPath);  // simulate the original file being deleted/moved

    const auto candidates = scanForRecoverableAutoSaves(dir, index);
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0].originalPath, realPath);

    fs::remove_all(dir);
}

TEST(AutosaveTest, ScanExcludesEntryWhenTheRealFileIsNewerThanTheAutosave) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    writeFile(realPath, "real");

    AutosaveIndex index;
    auto           session = makeNamedSession(realPath, u"real");
    session->document().insertText(session->document().length(), u" edited");
    performAutoSave(*session, dir, index, dir / "index.json");
    setLastWriteTimeOffsetFromNow(autosaveFilePathFor(dir, realPath), std::chrono::seconds(-10));
    // The user saved the real file for real AFTER the autosave fired.
    writeFile(realPath, "real, saved again");
    setLastWriteTimeOffsetFromNow(realPath, std::chrono::seconds(0));

    EXPECT_TRUE(scanForRecoverableAutoSaves(dir, index).empty());

    fs::remove_all(dir);
}

TEST(AutosaveTest, ScanSkipsAnIndexEntryWhoseTmpFileIsAlreadyGone) {
    const fs::path dir      = uniqueTempDir();
    const fs::path realPath = dir / "doc.txt";
    AutosaveIndex   index;
    index.record(autosaveHashFor(realPath), realPath);  // index entry with no matching .tmp on disk

    EXPECT_TRUE(scanForRecoverableAutoSaves(dir, index).empty());
    fs::remove_all(dir);
}

}  // namespace
