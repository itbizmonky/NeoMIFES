// Verifies fnv1aHash64() is deterministic (same input -> same output, on
// every call, in this process and by construction across separate process
// runs too - no per-run seeding exists in the implementation) and
// distinguishes different inputs well enough for its actual use (a
// filesystem-safe autosave filename, not a security hash).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "neomifes/util/hash.h"

using namespace std::string_view_literals;

namespace {

TEST(HashTest, SameInputProducesSameHashAcrossRepeatedCalls) {
    constexpr auto kText = u"C:\\Users\\example\\Documents\\notes.txt"sv;
    const std::uint64_t first  = neomifes::util::fnv1aHash64(kText);
    const std::uint64_t second = neomifes::util::fnv1aHash64(kText);
    const std::uint64_t third  = neomifes::util::fnv1aHash64(std::u16string(kText));
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, third);
}

TEST(HashTest, DifferentInputsProduceDifferentHashes) {
    const std::uint64_t a = neomifes::util::fnv1aHash64(u"C:\\a\\file1.txt"sv);
    const std::uint64_t b = neomifes::util::fnv1aHash64(u"C:\\a\\file2.txt"sv);
    const std::uint64_t c = neomifes::util::fnv1aHash64(u"C:\\b\\file1.txt"sv);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(b, c);
}

TEST(HashTest, EmptyStringIsSafeAndDeterministic) {
    const std::uint64_t first  = neomifes::util::fnv1aHash64(u""sv);
    const std::uint64_t second = neomifes::util::fnv1aHash64(std::u16string_view{});
    EXPECT_EQ(first, second);
}

TEST(HashTest, IsUsableInAConstantExpression) {
    // Pins that the function is genuinely constexpr, not merely marked so.
    constexpr std::uint64_t kValue = neomifes::util::fnv1aHash64(u"constexpr"sv);
    static_assert(kValue != 0);
    EXPECT_NE(kValue, 0u);
}

}  // namespace
