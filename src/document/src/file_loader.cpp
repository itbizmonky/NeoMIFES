#include "neomifes/document/file_loader.h"

#include <cstdio>

#include "neomifes/document/document.h"
#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

namespace {

LoadError mapOriginalBufferError(OriginalBufferError err) noexcept {
    switch (err) {
        case OriginalBufferError::NotFound:         return LoadError::NotFound;
        case OriginalBufferError::PermissionDenied: return LoadError::PermissionDenied;
        case OriginalBufferError::InvalidUtf8:      return LoadError::InvalidUtf8;
        case OriginalBufferError::EmptyFile:
        case OriginalBufferError::IoFailure:
        default:
            return LoadError::IoFailure;
    }
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

    if (size == 0) {
        // Empty file: valid, empty document. OriginalBuffer::openMemoryMapped
        // rejects zero-byte files (mapping a 0-byte file is not meaningful),
        // so handle this case directly without going through mmap at all.
        LoadResult result;
        result.document   = std::make_unique<Document>();
        result.hadBom     = false;
        result.byteLength = 0;
        return result;
    }

    // Peek the first 3 bytes for a UTF-8 BOM with a tiny fopen/fread - simpler
    // than mmap-then-inspect for just 3 bytes, and happens before the
    // (potentially large) mmap is created.
    std::FILE* fp = nullptr;
    if (::_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr) {
        return LoadError::PermissionDenied;
    }
    unsigned char bomBuf[3] = {};
    const std::size_t bomRead = std::fread(bomBuf, 1, 3, fp);
    (void)std::fclose(fp);
    const bool hadBom = (bomRead == 3
                        && bomBuf[0] == 0xEF && bomBuf[1] == 0xBB && bomBuf[2] == 0xBF);
    const std::uint64_t byteOffset = hadBom ? 3 : 0;

    auto opened = OriginalBuffer::openMemoryMapped(path, byteOffset);
    if (std::holds_alternative<OriginalBufferError>(opened)) {
        return mapOriginalBufferError(std::get<OriginalBufferError>(opened));
    }

    LoadResult result;
    result.document   = std::make_unique<Document>(
        std::move(std::get<std::shared_ptr<const OriginalBuffer>>(opened)));
    result.hadBom     = hadBom;
    result.byteLength = size;
    return result;
}

}  // namespace neomifes::document
