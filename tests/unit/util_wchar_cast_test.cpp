// Verifies the char16_t <-> wchar_t helpers preserve pointer identity,
// bit values, and view sizes. The static_asserts in the header cover
// sizeof/alignof at compile time; here we exercise the runtime behaviour.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "neomifes/util/wchar_cast.h"

using namespace std::string_view_literals;

namespace {

TEST(WcharCastTest, PointerIdentityPreserved) {
    char16_t buf[] = u"hello";
    // Round-trip through wchar_t* must yield the same address bits.
    const void* asWchar    = neomifes::util::toWchar(buf);
    const void* asChar16   = neomifes::util::fromWchar(neomifes::util::toWchar(buf));
    EXPECT_EQ(asWchar,   static_cast<const void*>(buf));
    EXPECT_EQ(asChar16,  static_cast<const void*>(buf));
}

TEST(WcharCastTest, ConstPointerOverloadCompilesAndPreserves) {
    const char16_t buf[] = u"world";
    const wchar_t* w = neomifes::util::toWchar(buf);
    EXPECT_EQ(static_cast<const void*>(w), static_cast<const void*>(buf));

    const wchar_t wraw[] = L"abcd";
    const char16_t* c = neomifes::util::fromWchar(wraw);
    EXPECT_EQ(static_cast<const void*>(c), static_cast<const void*>(wraw));
}

TEST(WcharCastTest, StringViewRoundTripPreservesContent) {
    const std::u16string src = u"ネイティブ text";
    const std::wstring_view w = neomifes::util::toWstringView(src);
    ASSERT_EQ(w.size(), src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint16_t>(w[i]),
                  static_cast<std::uint16_t>(src[i])) << "index " << i;
    }

    const std::u16string_view back = neomifes::util::fromWstringView(w);
    EXPECT_EQ(back.size(),                                    src.size());
    EXPECT_EQ(static_cast<const void*>(back.data()),
              static_cast<const void*>(src.data()));
}

TEST(WcharCastTest, EmptyViewIsSafe) {
    const std::u16string_view empty;
    const std::wstring_view w = neomifes::util::toWstringView(empty);
    EXPECT_EQ(w.size(), 0u);
}

TEST(WcharCastTest, PreservesSurrogatePairs) {
    // U+1F600 GRINNING FACE splits into D83D DE00 in UTF-16.
    const std::u16string src(u"\xD83D\xDE00");
    const std::wstring_view w = neomifes::util::toWstringView(src);
    ASSERT_EQ(w.size(), 2u);
    EXPECT_EQ(static_cast<std::uint16_t>(w[0]), 0xD83Du);
    EXPECT_EQ(static_cast<std::uint16_t>(w[1]), 0xDE00u);
}

}  // namespace
