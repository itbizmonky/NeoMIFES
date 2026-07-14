#include <gtest/gtest.h>

#include "neomifes/util/version.h"

namespace {

TEST(UtilVersionTest, ProductNameIsNeoMifes) {
    EXPECT_EQ(neomifes::util::productName(), "NeoMIFES");
}

TEST(UtilVersionTest, VersionStringMatchesConstants) {
    EXPECT_EQ(neomifes::util::versionString(), neomifes::util::kVersionString);
    EXPECT_FALSE(neomifes::util::versionString().empty());
}

TEST(UtilVersionTest, MajorIsZeroForPhaseZeroPointFive) {
    EXPECT_EQ(neomifes::util::kVersionMajor, 0u);
}

}  // namespace
