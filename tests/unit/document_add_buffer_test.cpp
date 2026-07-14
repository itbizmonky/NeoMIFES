#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "neomifes/document/add_buffer.h"

namespace {

using neomifes::document::AddBuffer;

TEST(AddBufferTest, EmptyOnCreate) {
    AddBuffer b;
    EXPECT_EQ(b.size(),          0u);
    EXPECT_EQ(b.chunkCount(),    0u);
    EXPECT_EQ(b.view(0, 0),      u"");
}

TEST(AddBufferTest, AppendReturnsGlobalOffset) {
    AddBuffer b;
    EXPECT_EQ(b.append(u"hello"),  0u);
    EXPECT_EQ(b.append(u" world"), 5u);
    EXPECT_EQ(b.size(), 11u);
    EXPECT_EQ(b.view(0, 5),  u"hello");
    EXPECT_EQ(b.view(5, 6),  u" world");
    EXPECT_EQ(b.view(0, 11), u"hello world");
}

TEST(AddBufferTest, EmptyAppendReturnsCurrentSize) {
    AddBuffer b;
    EXPECT_EQ(b.append(u""), 0u);
    EXPECT_EQ(b.size(),      0u);
    EXPECT_EQ(b.chunkCount(), 0u);
    b.append(u"abc");
    EXPECT_EQ(b.append(u""), 3u);
    EXPECT_EQ(b.size(),      3u);
}

TEST(AddBufferTest, OutOfRangeViewReturnsEmpty) {
    AddBuffer b;
    b.append(u"abc");
    EXPECT_EQ(b.view(0, 100), u"");
    EXPECT_EQ(b.view(100, 1), u"");
    EXPECT_EQ(b.view(3, 1),   u"");   // exactly past end
}

// ---- Chunked-storage behaviour ---------------------------------------------

TEST(AddBufferTest, StaysInOneChunkForModestAppends) {
    AddBuffer b;
    for (int i = 0; i < 100; ++i) {
        b.append(u"tiny");
    }
    EXPECT_EQ(b.chunkCount(), 1u);
    EXPECT_EQ(b.size(),       400u);
}

TEST(AddBufferTest, AppendLargerThanChunkGetsOversizedChunk) {
    AddBuffer b;
    // Give the default chunk a token payload first so the oversized append
    // must open a fresh chunk instead of resizing the empty one.
    b.append(u"seed");
    EXPECT_EQ(b.chunkCount(), 1u);

    const std::u16string big(AddBuffer::kDefaultChunkCUs + 128, u'X');
    const auto offset = b.append(big);

    EXPECT_EQ(b.chunkCount(), 2u);
    EXPECT_EQ(offset,         4u);
    EXPECT_EQ(b.view(offset, big.size()).size(), big.size());
    EXPECT_EQ(b.view(0, 4),   u"seed");
}

TEST(AddBufferTest, PointerStabilityAcrossAppends) {
    // The core invariant of the chunked design: an offset returned by an
    // earlier append() must keep pointing at the same char16_t bytes after
    // arbitrary further appends, even ones that add new chunks.
    AddBuffer b;
    const auto firstOffset = b.append(u"anchor");
    const char16_t* anchorPtr = b.view(firstOffset, 6).data();

    for (int i = 0; i < 20; ++i) {
        b.append(std::u16string(AddBuffer::kDefaultChunkCUs / 2, u'a'));
    }
    // Force a fresh chunk by an oversized append.
    b.append(std::u16string(AddBuffer::kDefaultChunkCUs + 1, u'Z'));

    EXPECT_EQ(b.view(firstOffset, 6).data(), anchorPtr);
    EXPECT_EQ(b.view(firstOffset, 6),        u"anchor");
}

TEST(AddBufferTest, RangeSpanningChunksReturnsEmpty) {
    AddBuffer b;
    // Fill the first chunk close to full so the next append opens a new chunk.
    b.append(std::u16string(AddBuffer::kDefaultChunkCUs - 4, u'a'));
    b.append(u"tail");   // fits in chunk 1
    // Now open chunk 2 explicitly.
    b.append(u"head");
    ASSERT_EQ(b.chunkCount(), 2u);

    const std::uint64_t chunk1End   = AddBuffer::kDefaultChunkCUs;
    const std::uint64_t straddling  = chunk1End - 2;   // last 2 in chunk 1
    // Request 4 CUs starting 2 before chunk boundary - would cross into chunk 2.
    EXPECT_EQ(b.view(straddling, 4), u"");
    // Requesting purely within chunk 1 works.
    EXPECT_EQ(b.view(straddling, 2).size(), 2u);
    // Requesting purely within chunk 2 works.
    EXPECT_EQ(b.view(chunk1End, 4), u"head");
}

TEST(AddBufferTest, ReserveHintIsHonouredWhenEmpty) {
    AddBuffer b;
    b.reserve(AddBuffer::kDefaultChunkCUs * 4);
    // reserve() may pre-create an empty first chunk; either way an append
    // that fits should not open a second chunk.
    b.append(std::u16string(AddBuffer::kDefaultChunkCUs * 2, u'x'));
    EXPECT_EQ(b.chunkCount(), 1u);
}

}  // namespace
