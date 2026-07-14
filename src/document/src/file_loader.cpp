#include "neomifes/document/file_loader.h"

#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>

#include "neomifes/document/document.h"
#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

namespace {

// UTF-8 -> UTF-16 conversion with a minimal state machine.
// Returns false and stops at the first invalid sequence encountered.
bool decodeUtf8(const std::vector<char>& src, std::u16string& out) {
    out.clear();
    out.reserve(src.size());   // ASCII-heavy input; grow later if needed.

    std::size_t i = 0;
    while (i < src.size()) {
        const auto b0 = static_cast<unsigned char>(src[i]);
        std::uint32_t cp    = 0;
        std::size_t   extra = 0;

        if (b0 < 0x80) {
            cp = b0; extra = 0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F; extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F; extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07; extra = 3;
        } else {
            return false;
        }
        if (i + extra >= src.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto bk = static_cast<unsigned char>(src[i + k]);
            if ((bk & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (bk & 0x3F);
        }
        i += extra + 1;

        // Reject surrogates and out-of-range code points.
        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            return false;
        }

        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            const std::uint32_t adj  = cp - 0x10000;
            const char16_t hi = static_cast<char16_t>(0xD800 + (adj >> 10));
            const char16_t lo = static_cast<char16_t>(0xDC00 + (adj & 0x3FF));
            out.push_back(hi);
            out.push_back(lo);
        }
    }
    return true;
}

}  // namespace

std::variant<LoadResult, LoadError>
loadUtf8File(const std::filesystem::path& path, std::uint64_t maxBytes) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return LoadError::NotFound;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return LoadError::IoFailure;
    }
    if (size > maxBytes) {
        return LoadError::TooLarge;
    }

    std::FILE* fp = nullptr;
#ifdef _WIN32
    if (::_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr) {
        return LoadError::PermissionDenied;
    }
#else
    fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return LoadError::PermissionDenied;
    }
#endif

    std::vector<char> raw(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(raw.data(), 1, raw.size(), fp);
    std::fclose(fp);
    if (got != raw.size()) {
        return LoadError::IoFailure;
    }

    // Strip UTF-8 BOM (EF BB BF) if present.
    bool hadBom = false;
    if (raw.size() >= 3
        && static_cast<unsigned char>(raw[0]) == 0xEF
        && static_cast<unsigned char>(raw[1]) == 0xBB
        && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(raw.begin(), raw.begin() + 3);
        hadBom = true;
    }

    std::u16string decoded;
    if (!decodeUtf8(raw, decoded)) {
        return LoadError::InvalidUtf8;
    }

    LoadResult result;
    auto orig      = OriginalBuffer::fromU16String(std::move(decoded));
    result.document   = std::make_unique<Document>(std::move(orig));
    result.hadBom     = hadBom;
    result.byteLength = size;
    return result;
}

}  // namespace neomifes::document
