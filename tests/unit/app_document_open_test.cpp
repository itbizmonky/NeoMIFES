#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <variant>

#include "neomifes/app/document_open.h"
#include "neomifes/core/bookmark_manager.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"
#include "neomifes/encoding/encoding.h"

namespace fs = std::filesystem;

namespace {

using neomifes::app::LoadedFileMeta;
using neomifes::app::openDocumentAt;
using neomifes::core::BookmarkManager;
using neomifes::core::CommandDispatcher;
using neomifes::core::InsertTextCommand;
using neomifes::core::SelectionModel;
using neomifes::core::Viewport;
using neomifes::document::Document;
using neomifes::document::LoadError;
using neomifes::document::TextPos;
using neomifes::encoding::Encoding;
using neomifes::encoding::LineEnding;

// Same idiom as document_file_loader_test.cpp's tempFileWith() (no shared
// test-util header exists in this codebase for this).
fs::path tempFileWith(const std::string& bytes) {
    fs::path p =
        fs::temp_directory_path() / (std::string("nmfs_doc_open_") + std::to_string(std::rand()) + ".txt");
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

// Bundles every piece of session state openDocumentAt() touches, mirroring
// app_editor_input_test.cpp's Env fixture.
struct Env {
    Document                      doc;
    SelectionModel                 selection{0};
    CommandDispatcher              dispatcher{doc, selection};
    Viewport                       viewport;
    BookmarkManager                bookmarks;
    std::optional<TextPos>         altCursorAnchor;
    std::optional<TextPos>         rectangularAnchor;
    std::optional<std::uint32_t>  freeCursorVirtualColumns;
};

TEST(DocumentOpenTest, FailsAndLeavesEverythingUntouchedForMissingFile) {
    Env env;
    env.doc.insertText(0, u"original");
    env.selection.moveAllTo(3);
    env.bookmarks.toggle(0);
    env.altCursorAnchor = 1;

    const fs::path missing = fs::temp_directory_path() / "nmfs_doc_open_does_not_exist.txt";
    const auto     result  = openDocumentAt(missing, std::nullopt, std::nullopt, env.doc,
                                          env.dispatcher, env.selection, env.viewport, env.bookmarks,
                                          env.altCursorAnchor, env.rectangularAnchor,
                                          env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result), LoadError::NotFound);
    EXPECT_EQ(env.doc.toU16String(), u"original");
    EXPECT_EQ(env.selection.primaryCursor().position, 3U);
    EXPECT_TRUE(env.bookmarks.isBookmarked(0));
    EXPECT_EQ(env.altCursorAnchor, 1U);
}

TEST(DocumentOpenTest, ReplacesDocumentContentOnSuccess) {
    Env env;
    env.doc.insertText(0, u"old content");
    const fs::path file = tempFileWith("new content");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_EQ(env.doc.toU16String(), u"new content");
    fs::remove(file);
}

// WI-02: on success, openDocumentAt() now returns LoadedFileMeta (hadBom/
// encoding/lineEnding) instead of a bare nullopt - these must reflect the
// actual loaded file's characteristics (main.cpp's DocumentFileState relies
// on this for Ctrl+S to reuse the same encoding/line-ending/BOM).
TEST(DocumentOpenTest, LoadedFileMetaReflectsPlainUtf8NoBomLfFile) {
    Env env;
    const fs::path file = tempFileWith("line1\nline2\n");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    const auto& meta = std::get<LoadedFileMeta>(result);
    EXPECT_FALSE(meta.hadBom);
    EXPECT_EQ(meta.encoding, Encoding::Utf8);
    EXPECT_EQ(meta.lineEnding, LineEnding::Lf);
    fs::remove(file);
}

TEST(DocumentOpenTest, LoadedFileMetaReflectsUtf8BomCrlfFile) {
    Env env;
    const fs::path file = tempFileWith("\xEF\xBB\xBFline1\r\nline2\r\n");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    const auto& meta = std::get<LoadedFileMeta>(result);
    EXPECT_TRUE(meta.hadBom);
    EXPECT_EQ(meta.encoding, Encoding::Utf8Bom);
    EXPECT_EQ(meta.lineEnding, LineEnding::Crlf);
    fs::remove(file);
}

TEST(DocumentOpenTest, ClearsUndoHistory) {
    Env env;
    env.doc.insertText(0, u"a");
    env.dispatcher.dispatch(std::make_unique<InsertTextCommand>(1, u"b"));
    ASSERT_TRUE(env.dispatcher.canUndo());
    const fs::path file = tempFileWith("new content");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_FALSE(env.dispatcher.canUndo());
    fs::remove(file);
}

TEST(DocumentOpenTest, ClearsBookmarks) {
    Env env;
    env.doc.insertText(0, u"a\nb\nc");
    env.bookmarks.toggle(1);
    ASSERT_TRUE(env.bookmarks.isBookmarked(1));
    const fs::path file = tempFileWith("new content");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_TRUE(env.bookmarks.lines().empty());
    fs::remove(file);
}

TEST(DocumentOpenTest, ResetsAltCursorAndRectangularAnchors) {
    Env env;
    env.doc.insertText(0, u"a");
    env.altCursorAnchor     = 0;
    env.rectangularAnchor   = 0;
    const fs::path file = tempFileWith("new content");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_FALSE(env.altCursorAnchor.has_value());
    EXPECT_FALSE(env.rectangularAnchor.has_value());
    fs::remove(file);
}

TEST(DocumentOpenTest, ResetsFreeCursorVirtualColumns) {
    Env env;
    env.doc.insertText(0, u"a");
    env.freeCursorVirtualColumns = 3;
    const fs::path file = tempFileWith("new content");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_FALSE(env.freeCursorVirtualColumns.has_value());
    fs::remove(file);
}

TEST(DocumentOpenTest, MovesSelectionToDocumentStartWhenNoTargetGiven) {
    Env env;
    env.doc.insertText(0, u"old");
    env.selection.moveAllTo(2);
    const fs::path file = tempFileWith("line0\nline1\nline2");

    const auto result = openDocumentAt(file, std::nullopt, std::nullopt, env.doc, env.dispatcher,
                                       env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_EQ(env.selection.primaryCursor().position, 0U);
    fs::remove(file);
}

TEST(DocumentOpenTest, MovesSelectionToGivenLineAndColumn) {
    Env env;
    env.doc.insertText(0, u"old");
    const fs::path file = tempFileWith("line0\nline1\nline2");  // line1 starts at offset 6

    const auto result = openDocumentAt(file, /*targetLine=*/1, /*targetColumn=*/2, env.doc,
                                       env.dispatcher, env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_EQ(env.selection.primaryCursor().position, 8U);  // offset 6 ('l') + column 2
    fs::remove(file);
}

TEST(DocumentOpenTest, ClampsOutOfRangeTargetLineToLastLine) {
    Env env;
    env.doc.insertText(0, u"old");
    const fs::path file = tempFileWith("line0\nline1");  // only 2 lines (0-1)

    const auto result = openDocumentAt(file, /*targetLine=*/99, /*targetColumn=*/0, env.doc,
                                       env.dispatcher, env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_EQ(env.selection.primaryCursor().position, 6U);  // start of line1 (last line)
    fs::remove(file);
}

TEST(DocumentOpenTest, ClampsOutOfRangeTargetColumnToLineEnd) {
    Env env;
    env.doc.insertText(0, u"old");
    const fs::path file = tempFileWith("ab\ncd");  // line0 = "ab", length 2

    const auto result = openDocumentAt(file, /*targetLine=*/0, /*targetColumn=*/99, env.doc,
                                       env.dispatcher, env.selection, env.viewport, env.bookmarks,
                                       env.altCursorAnchor, env.rectangularAnchor,
                                       env.freeCursorVirtualColumns);

    ASSERT_TRUE(std::holds_alternative<LoadedFileMeta>(result));
    EXPECT_EQ(env.selection.primaryCursor().position, 2U);  // end of "ab", before '\n'
    fs::remove(file);
}

}  // namespace
