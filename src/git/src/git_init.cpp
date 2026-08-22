#include "neomifes/git/git_init.h"

#include <git2.h>

namespace neomifes::git {

bool initializeLibgit2() noexcept {
    // git_libgit2_init() returns the number of initializations (>=1) on
    // success, or a negative git_error_code on failure - this has been
    // libgit2's stable, documented contract since well before v1.9.7 (the
    // version this project vendors, ADR-022), unlike the buffer-diff API a
    // later commit in this WI probes before using.
    return ::git_libgit2_init() >= 0;
}

void shutdownLibgit2() noexcept {
    ::git_libgit2_shutdown();
}

}  // namespace neomifes::git
