#include "neomifes/util/version.h"

namespace neomifes::util {

std::string_view productName() noexcept {
    return kProductName;
}

std::string_view versionString() noexcept {
    return kVersionString;
}

}  // namespace neomifes::util
