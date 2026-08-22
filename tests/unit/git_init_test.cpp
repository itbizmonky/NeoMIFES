#include <gtest/gtest.h>

#include "neomifes/git/git_init.h"

namespace {

// WI-17a smoke test (ADR-022): proves libgit2 is correctly vendored, linked,
// and callable from the real build - not just from the standalone scratchpad
// probe run before this WI's plan was written. Init/shutdown is internally
// reference-counted by libgit2 itself (its own documented contract), so
// calling it once here alongside whatever main.cpp does at runtime is safe -
// this test process never links against main.cpp, so there is no risk of a
// double-init within the same process here.
TEST(GitInitTest, InitializeThenShutdownSucceeds) {
    EXPECT_TRUE(neomifes::git::initializeLibgit2());
    neomifes::git::shutdownLibgit2();
}

}  // namespace
