#pragma once

#include <cstdint>
#include <string_view>

namespace neomifes::util {

inline constexpr std::uint32_t kVersionMajor = 0;
inline constexpr std::uint32_t kVersionMinor = 0;
inline constexpr std::uint32_t kVersionPatch = 1;

inline constexpr std::string_view kVersionString = "0.0.1";
inline constexpr std::string_view kProductName   = "NeoMIFES";

[[nodiscard]] std::string_view productName() noexcept;
[[nodiscard]] std::string_view versionString() noexcept;

}  // namespace neomifes::util
