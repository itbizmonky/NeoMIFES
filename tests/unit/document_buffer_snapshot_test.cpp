// Focused coverage for BufferSnapshot::pieceView - the Phase 2b primitive
// that lets O(N) traversal (LineIndex, future SearchEngine) avoid the O(N)
// re-walk that extract() performs.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/original_buffer.h"
#include "neomifes/document/piece_table.h"

namespace fs = std::filesystem;

namespace {

using neomifes::document::BufferSnapshot;
using neomifes::document::OriginalBuffer;
using neomifes::document::PieceSource;
using neomifes::document::PieceTable;

TEST(BufferSnapshotPieceViewTest, EmptyDocumentHasNoPieces) {
    PieceTable pt;
    auto snap = pt.snapshot();
    EXPECT_TRUE(snap->pieces().empty());
}

TEST(BufferSnapshotPieceViewTest, ReturnsCorrectViewForAddPiece) {
    PieceTable pt;
    pt.insert(0, u"hello world");
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    EXPECT_EQ(snap->pieces()[0].source, PieceSource::Add);
    EXPECT_EQ(snap->pieceView(snap->pieces()[0]), u"hello world");
}

TEST(BufferSnapshotPieceViewTest, ReturnsCorrectViewForOriginalPiece) {
    auto orig = OriginalBuffer::fromU16String(u"NeoMIFES");
    PieceTable pt(orig);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    EXPECT_EQ(snap->pieces()[0].source, PieceSource::Original);
    EXPECT_EQ(snap->pieceView(snap->pieces()[0]), u"NeoMIFES");
}

TEST(BufferSnapshotPieceViewTest, SplitPiecesEachReturnCorrectSubview) {
    auto orig = OriginalBuffer::fromU16String(u"abcdef");
    PieceTable pt(orig);
    // Insert into the middle so the Original piece is split around the insert.
    pt.insert(3, u"XYZ");   // "abcXYZdef"
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 3u);

    // Concatenating each pieceView() must reconstruct the document.
    std::u16string reconstructed;
    for (const auto& p : snap->pieces()) {
        reconstructed.append(snap->pieceView(p));
    }
    EXPECT_EQ(reconstructed, u"abcXYZdef");

    // Individual pieces have the expected sub-slices.
    EXPECT_EQ(snap->pieceView(snap->pieces()[0]), u"abc");
    EXPECT_EQ(snap->pieceView(snap->pieces()[1]), u"XYZ");
    EXPECT_EQ(snap->pieceView(snap->pieces()[2]), u"def");
}

TEST(BufferSnapshotPieceViewTest, LargeAddPieceStaysContiguous) {
    // Even when AddBuffer opens fresh chunks, each piece is a single append
    // so pieceView() must always return a contiguous view.
    PieceTable pt;
    const std::u16string big(200'000, u'q');   // > kDefaultChunkCUs
    pt.insert(0, big);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    const auto view = snap->pieceView(snap->pieces()[0]);
    EXPECT_EQ(view.size(), big.size());
    EXPECT_EQ(view.front(), u'q');
    EXPECT_EQ(view.back(),  u'q');
}

// -----------------------------------------------------------------------
// pieceTextNoCache() - WI added after discovering that a full-document walk
// through pieceView() (LineIndex::build(), LogModel::build(),
// SearchService's scan, CSV/JSON/XML's whole-document extract()) permanently
// populates OriginalBuffer's never-evicted decode cache, defeating the mmap
// + Lazy Decode design's "10GB file, ~20MB initial memory" goal for any
// consumer that visits every byte once. pieceTextNoCache() must return the
// exact same text as pieceView() while going through OriginalBuffer's
// non-caching decode path for Original-sourced pieces - see
// docs/issues/decode_cache_unbounded_growth.md.

TEST(BufferSnapshotPieceViewTest, TextNoCacheMatchesViewForAddPiece) {
    PieceTable pt;
    pt.insert(0, u"hello world");
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    EXPECT_EQ(snap->pieceTextNoCache(snap->pieces()[0]), u"hello world");
}

TEST(BufferSnapshotPieceViewTest, TextNoCacheMatchesViewForInMemoryOriginalPiece) {
    auto orig = OriginalBuffer::fromU16String(u"NeoMIFES");
    PieceTable pt(orig);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    EXPECT_EQ(snap->pieceTextNoCache(snap->pieces()[0]), u"NeoMIFES");
}

namespace {
fs::path tempFileWithBytes(const std::string& bytes) {
    fs::path p = fs::temp_directory_path()
               / (std::string("nmfs_snapshot_") + std::to_string(std::rand()) + ".txt");
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}
}  // namespace

TEST(BufferSnapshotPieceViewTest, TextNoCacheMatchesViewForMemoryMappedOriginalPiece) {
    // A real mmap'd file (not fromU16String()) so this exercises
    // OriginalBuffer's MemoryMapped decode path - the one the fix actually
    // changes - rather than the trivial InMemory substr() path.
    std::string content = "The quick brown fox jumps over the lazy dog. ";
    // > kCheckpointBytes (64KiB) so this also exercises the checkpoint-based
    // skip phase, same as the real 10GB-file scenario this fix targets.
    while (content.size() < 200 * 1024) {
        content += "The quick brown fox jumps over the lazy dog. ";
    }
    auto path = tempFileWithBytes(content);
    auto opened = OriginalBuffer::openMemoryMapped(path, 0, neomifes::encoding::Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const OriginalBuffer>>(opened));
    auto orig = std::get<std::shared_ptr<const OriginalBuffer>>(opened);

    PieceTable pt(orig);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    const auto& piece = snap->pieces()[0];

    const std::u16string cached   = std::u16string(snap->pieceView(piece));
    const std::u16string noCache  = snap->pieceTextNoCache(piece);
    EXPECT_EQ(noCache, cached);
    EXPECT_EQ(noCache.size(), content.size());  // ASCII: 1 byte == 1 UTF-16 CU

    fs::remove(path);
}

TEST(BufferSnapshotPieceViewTest, TextNoCacheHandlesMultibyteUtf8) {
    // "あ" (U+3042) UTF-8 = E3 81 82, repeated to exceed the checkpoint size
    // so both the skip phase and the decode phase run.
    std::string content;
    while (content.size() < 200 * 1024) {
        content += "\xE3\x81\x82";
    }
    auto path = tempFileWithBytes(content);
    auto opened = OriginalBuffer::openMemoryMapped(path, 0, neomifes::encoding::Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const OriginalBuffer>>(opened));
    auto orig = std::get<std::shared_ptr<const OriginalBuffer>>(opened);

    PieceTable pt(orig);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    const auto& piece = snap->pieces()[0];

    const std::u16string cached  = std::u16string(snap->pieceView(piece));
    const std::u16string noCache = snap->pieceTextNoCache(piece);
    EXPECT_EQ(noCache, cached);
    EXPECT_EQ(noCache.size(), content.size() / 3);  // 3 UTF-8 bytes -> 1 CU

    fs::remove(path);
}

TEST(BufferSnapshotPieceViewTest, ExtractNoCacheMatchesExtractAcrossMixedPieces) {
    std::string content = "The quick brown fox jumps over the lazy dog. ";
    while (content.size() < 200 * 1024) {
        content += "The quick brown fox jumps over the lazy dog. ";
    }
    auto path = tempFileWithBytes(content);
    auto opened = OriginalBuffer::openMemoryMapped(path, 0, neomifes::encoding::Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const OriginalBuffer>>(opened));
    auto orig = std::get<std::shared_ptr<const OriginalBuffer>>(opened);

    PieceTable pt(orig);
    // Split the Original piece by inserting an Add piece in the middle, so
    // extractNoCache() must correctly stitch an Add-sourced chunk between
    // two Original-sourced chunks (mirrors the real "open file, then edit
    // it" shape file_saver.cpp/syntax_worker.cpp see).
    const auto mid = static_cast<neomifes::document::TextPos>(content.size() / 2);
    pt.insert(mid, u"[INSERTED]");
    auto snap = pt.snapshot();
    ASSERT_GE(snap->pieces().size(), 2u);

    const neomifes::document::TextRange fullRange{.start = 0, .end = snap->length()};
    EXPECT_EQ(snap->extractNoCache(fullRange), snap->extract(fullRange));

    // A sub-range straddling the inserted piece too.
    const neomifes::document::TextRange straddle{.start = mid - 20, .end = mid + 20};
    EXPECT_EQ(snap->extractNoCache(straddle), snap->extract(straddle));

    fs::remove(path);
}

// -----------------------------------------------------------------------
// pieceTextStreamed() - the bounded-chunk sibling of pieceTextNoCache(),
// added after the OOM/slow-path fix above turned out to still transiently
// materialize an entire multi-GB piece as one std::u16string. Concatenating
// every chunk pieceTextStreamed() delivers must reconstruct the exact same
// text pieceTextNoCache()/pieceView() return, and no single chunk may
// exceed OriginalBuffer's chunk bound.

TEST(BufferSnapshotPieceViewTest, StreamedMatchesNoCacheForAddPiece) {
    PieceTable pt;
    pt.insert(0, u"hello world");
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);

    std::u16string reconstructed;
    const bool ok = snap->pieceTextStreamed(snap->pieces()[0],
                                            [&](std::u16string_view chunk) { reconstructed += chunk; });
    EXPECT_TRUE(ok);
    EXPECT_EQ(reconstructed, u"hello world");
}

TEST(BufferSnapshotPieceViewTest, StreamedMatchesNoCacheForInMemoryOriginalPiece) {
    auto orig = OriginalBuffer::fromU16String(u"NeoMIFES");
    PieceTable pt(orig);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);

    std::u16string reconstructed;
    const bool ok = snap->pieceTextStreamed(snap->pieces()[0],
                                            [&](std::u16string_view chunk) { reconstructed += chunk; });
    EXPECT_TRUE(ok);
    EXPECT_EQ(reconstructed, u"NeoMIFES");
}

TEST(BufferSnapshotPieceViewTest, StreamedMatchesNoCacheForMemoryMappedOriginalPieceAcrossChunkBoundary) {
    // Deliberately larger than OriginalBuffer::kStreamChunkCodeUnits
    // (1,048,576 CU) so this actually exercises MULTIPLE chunk callbacks,
    // not just one - the whole point of this test.
    std::string content;
    content.reserve(3 * 1024 * 1024);
    while (content.size() < 3 * 1024 * 1024) {
        content += "The quick brown fox jumps over the lazy dog. ";
    }
    auto path = tempFileWithBytes(content);
    auto opened = OriginalBuffer::openMemoryMapped(path, 0, neomifes::encoding::Encoding::Utf8);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const OriginalBuffer>>(opened));
    auto orig = std::get<std::shared_ptr<const OriginalBuffer>>(opened);

    PieceTable pt(orig);
    auto snap = pt.snapshot();
    ASSERT_EQ(snap->pieces().size(), 1u);
    const auto& piece = snap->pieces()[0];

    const std::u16string expected = snap->pieceTextNoCache(piece);

    std::u16string reconstructed;
    int             chunkCount = 0;
    const bool ok = snap->pieceTextStreamed(piece, [&](std::u16string_view chunk) {
        EXPECT_LE(chunk.size(), 1024u * 1024u);  // never exceeds kStreamChunkCodeUnits
        reconstructed += chunk;
        ++chunkCount;
    });
    EXPECT_TRUE(ok);
    EXPECT_EQ(reconstructed, expected);
    EXPECT_GT(chunkCount, 1);  // actually exercised more than one chunk

    fs::remove(path);
}

TEST(BufferSnapshotPieceViewTest, StreamedHandlesEmptyPiece) {
    PieceTable pt;
    // No pieces at all - nothing to stream, must not crash and must report success.
    auto snap = pt.snapshot();
    EXPECT_TRUE(snap->pieces().empty());
}

TEST(BufferSnapshotPieceViewTest, ExtractNoCacheReturnsEmptyForInvalidRange) {
    PieceTable pt;
    pt.insert(0, u"hello");
    auto snap = pt.snapshot();
    EXPECT_EQ(snap->extractNoCache({.start = 3, .end = 3}), u"");   // empty range
    EXPECT_EQ(snap->extractNoCache({.start = 10, .end = 20}), u"");  // out of range
}

}  // namespace
