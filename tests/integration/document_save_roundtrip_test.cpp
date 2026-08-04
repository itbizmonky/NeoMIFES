// document_save_roundtrip_test - exercises document::saveFile() (WI-01)
// against the REAL filesystem: open -> edit -> save -> reopen round trips,
// Save-As-to-a-new-path (Finding 1 regression), multi-encoding/line-ending
// round trips, the hybrid chunking path's two degenerate-input regression
// cases (Finding 2: a huge single line and a CR-only file, both of which
// collapse Document::lineCount() to 1 regardless of size), and that a
// blocked save leaves the original file byte-for-byte untouched.

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/document/file_saver.h"
#include "neomifes/encoding/encoding.h"

namespace fs = std::filesystem;

namespace {

using neomifes::document::Document;
using neomifes::document::LoadResult;
using neomifes::document::loadFile;
using neomifes::document::saveFile;
using neomifes::document::SaveError;
using neomifes::encoding::decode;
using neomifes::encoding::detectBom;
using neomifes::encoding::detectLineEnding;
using neomifes::encoding::Encoding;
using neomifes::encoding::LineEnding;
using neomifes::encoding::withBom;

fs::path uniqueTempPath(const std::string& suffix) {
    return fs::temp_directory_path() / (std::string("nmfs_save_") + std::to_string(std::rand()) + suffix);
}

std::string readWholeFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::byte> toBytes(const std::string& s) {
    std::vector<std::byte> bytes(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        bytes[i] = static_cast<std::byte>(s[i]);
    }
    return bytes;
}

TEST(DocumentSaveRoundtripTest, OpenEditSaveToSamePathReopenMatchesContent) {
    auto path = uniqueTempPath(".txt");
    {
        std::ofstream out(path, std::ios::binary);
        out << "line1\nline2\n";
    }
    auto loaded = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(loaded));
    auto& r = std::get<LoadResult>(loaded);
    r.document->insertText(r.document->length(), u"line3\n");

    const auto err = saveFile(*r.document, path, Encoding::Utf8, LineEnding::Lf, /*writeBom=*/false);
    EXPECT_FALSE(err.has_value());
    EXPECT_FALSE(r.document->isDirty());

    auto reloaded = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(reloaded));
    EXPECT_EQ(std::get<LoadResult>(reloaded).document->toU16String(), u"line1\nline2\nline3\n");

    fs::remove(path);
}

TEST(DocumentSaveRoundtripTest, SavingToANewNonexistentPathCreatesTheFile) {
    // Finding 1 regression: ReplaceFileW cannot target a file that doesn't
    // exist yet - saveFile() must fall back to MoveFileExW.
    auto path = uniqueTempPath(".txt");
    ASSERT_FALSE(fs::exists(path));

    Document doc;
    doc.insertText(0, u"brand new file\n");
    const auto err = saveFile(doc, path, Encoding::Utf8, LineEnding::Lf, /*writeBom=*/false);
    EXPECT_FALSE(err.has_value());
    ASSERT_TRUE(fs::exists(path));
    EXPECT_EQ(readWholeFile(path), "brand new file\n");

    fs::remove(path);
}

TEST(DocumentSaveRoundtripTest, SaveAsToADifferentExistingPathOverwritesIt) {
    auto saveAsPath = uniqueTempPath("_saveas.txt");
    {
        std::ofstream out(saveAsPath, std::ios::binary);
        out << "old content that should be replaced";
    }

    Document doc;
    doc.insertText(0, u"new content via save as\n");
    const auto err = saveFile(doc, saveAsPath, Encoding::Utf8, LineEnding::Lf, /*writeBom=*/false);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(readWholeFile(saveAsPath), "new content via save as\n");

    fs::remove(saveAsPath);
}

// --- Multi-encoding round trips ---------------------------------------------
// Decodes the written bytes directly via encoding::decode() (not through
// loadFile()'s auto-detection, which is a separate feature with its own
// documented ambiguity cases for BOM-less legacy encodings) - this isolates
// the round trip to exactly what saveFile()/encoding::encode() are
// responsible for.

void expectEncodingRoundTrips(Encoding enc, std::u16string_view text) {
    auto path = uniqueTempPath(".txt");
    Document doc;
    doc.insertText(0, text);
    const auto err = saveFile(doc, path, enc, LineEnding::Lf, /*writeBom=*/true);
    ASSERT_FALSE(err.has_value());

    const std::vector<std::byte> bytes = toBytes(readWholeFile(path));
    const Encoding                bomVariant = withBom(enc, true);
    if (bomVariant != enc) {  // has a real BOM variant (Unicode transformation formats)
        const auto bom = detectBom(bytes);
        ASSERT_TRUE(bom.has_value());
        EXPECT_EQ(bom->encoding, bomVariant);
    }

    const auto decoded = decode(bytes, bomVariant);
    ASSERT_TRUE(std::holds_alternative<std::u16string>(decoded));
    EXPECT_EQ(std::get<std::u16string>(decoded), text);

    fs::remove(path);
}

TEST(DocumentSaveRoundtripTest, RoundTripsUtf8) {
    expectEncodingRoundTrips(Encoding::Utf8, u"hello こんにちは\n");
}

TEST(DocumentSaveRoundtripTest, RoundTripsUtf16Le) {
    expectEncodingRoundTrips(Encoding::Utf16Le, u"hello こんにちは\n");
}

TEST(DocumentSaveRoundtripTest, RoundTripsShiftJis) {
    expectEncodingRoundTrips(Encoding::ShiftJis, u"hello こんにちは\n");
}

TEST(DocumentSaveRoundtripTest, RoundTripsEucJp) {
    expectEncodingRoundTrips(Encoding::EucJp, u"hello こんにちは\n");
}

TEST(DocumentSaveRoundtripTest, RoundTripsIso2022Jp) {
    expectEncodingRoundTrips(Encoding::Iso2022Jp, u"hello こんにちは\n");
}

// --- Line-ending round trips -------------------------------------------------

void expectLineEndingRoundTrips(LineEnding target, const std::string& expectedBytesFragment) {
    auto path = uniqueTempPath(".txt");
    Document doc;
    doc.insertText(0, u"line1\nline2\nline3");
    const auto err = saveFile(doc, path, Encoding::Utf8, target, /*writeBom=*/false);
    ASSERT_FALSE(err.has_value());

    const std::string bytes = readWholeFile(path);
    EXPECT_NE(bytes.find(expectedBytesFragment), std::string::npos);

    auto reloaded = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(reloaded));
    const auto detected = detectLineEnding(std::get<LoadResult>(reloaded).document->toU16String());
    ASSERT_TRUE(detected.has_value());
    EXPECT_EQ(*detected, target);

    fs::remove(path);
}

TEST(DocumentSaveRoundtripTest, LineEndingRoundTripsCrlf) {
    expectLineEndingRoundTrips(LineEnding::Crlf, "line1\r\nline2\r\nline3");
}

TEST(DocumentSaveRoundtripTest, LineEndingRoundTripsLf) {
    expectLineEndingRoundTrips(LineEnding::Lf, "line1\nline2\nline3");
}

TEST(DocumentSaveRoundtripTest, LineEndingRoundTripsCr) {
    expectLineEndingRoundTrips(LineEnding::Cr, "line1\rline2\rline3");
}

// --- Finding 2 regression: hybrid chunking on degenerate line structure ----

TEST(DocumentSaveRoundtripTest, HugeSingleLineWithNoEmbeddedNewlineRoundTrips) {
    // A document with one line far exceeding kMaxChunkCodeUnits (2^20) must
    // still be saved correctly via the sub-chunking path in writeChunks()/
    // nextSubChunkEnd() - naive line-boundary-only chunking would collapse
    // to one chunk spanning the whole document here.
    auto path = uniqueTempPath(".txt");
    Document doc;
    const std::u16string hugeLine(3'000'000, u'x');
    doc.insertText(0, hugeLine);
    ASSERT_EQ(doc.lineCount(), 1u);

    const auto err = saveFile(doc, path, Encoding::Utf8, LineEnding::Lf, /*writeBom=*/false);
    ASSERT_FALSE(err.has_value());

    auto reloaded = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(reloaded));
    EXPECT_EQ(std::get<LoadResult>(reloaded).document->toU16String(), hugeLine);

    fs::remove(path);
}

TEST(DocumentSaveRoundtripTest, CrOnlyTerminatedFileRoundTrips) {
    // Document::lineCount() counts only '\n', so a CR-only file has
    // lineCount()==1 for its entire content regardless of size - must still
    // chunk correctly via the code-unit cap, not the line-boundary walk.
    auto path = uniqueTempPath(".txt");
    Document doc;
    std::u16string content;
    content.reserve(90000);
    for (int i = 0; i < 5000; ++i) {
        content += u"line content here\r";
    }
    doc.insertText(0, content);
    ASSERT_EQ(doc.lineCount(), 1u);

    const auto err = saveFile(doc, path, Encoding::Utf8, LineEnding::Lf, /*writeBom=*/false);
    ASSERT_FALSE(err.has_value());

    auto reloaded = loadFile(path);
    ASSERT_TRUE(std::holds_alternative<LoadResult>(reloaded));
    // Saved with LineEnding::Lf, so every '\r' becomes '\n'.
    std::u16string expected;
    expected.reserve(content.size());
    for (const char16_t c : content) {
        expected += (c == u'\r') ? u'\n' : c;
    }
    EXPECT_EQ(std::get<LoadResult>(reloaded).document->toU16String(), expected);

    fs::remove(path);
}

TEST(DocumentSaveRoundtripTest, FailedSaveLeavesTheOriginalFileUntouched) {
    auto              path            = uniqueTempPath(".txt");
    const std::string originalContent = "original content, must survive";
    {
        std::ofstream out(path, std::ios::binary);
        out << originalContent;
    }

    // Lock the target with NO sharing at all, so ReplaceFileW's internal
    // rename-to-backup step fails with a sharing violation - the "target
    // exists but is blocked" scenario replaceIntoPlace() must leave
    // untouched (verified via probe during design; see file_saver.cpp's
    // replaceIntoPlace()).
    const HANDLE lock =
        ::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE);

    Document doc;
    doc.insertText(0, u"new content that should never land");
    const auto err = saveFile(doc, path, Encoding::Utf8, LineEnding::Lf, /*writeBom=*/false);
    EXPECT_TRUE(err.has_value());
    EXPECT_EQ(*err, SaveError::ReplaceFailed);

    ::CloseHandle(lock);

    EXPECT_EQ(readWholeFile(path), originalContent);
    fs::remove(path);
}

}  // namespace
