#include "neomifes/platform/file_mapping.h"

namespace neomifes::platform {

std::variant<FileMapping, FileMappingError>
FileMapping::open(const std::filesystem::path& path) {
    // FILE_SHARE_DELETE is required so the file can still be deleted or
    // renamed by another process (or the user, via Explorer/git/etc.) while
    // NeoMIFES has it mapped open - without it, Windows blocks delete/rename
    // for as long as the mapping (i.e. the whole document session) is alive,
    // which is both a real editor usability problem and, incidentally, what
    // broke every file-loader test's fs::remove(path) cleanup in CI.
    FileHandle file{::CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};
    if (!file) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return FileMappingError::NotFound;
        }
        if (err == ERROR_ACCESS_DENIED) {
            return FileMappingError::PermissionDenied;
        }
        return FileMappingError::IoFailure;
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file.get(), &size)) {
        return FileMappingError::IoFailure;
    }
    if (size.QuadPart == 0) {
        return FileMappingError::EmptyFile;
    }

    // dwMaximumSize{Low,High} = 0 maps the mapping object to the file's
    // current size.
    KernelHandle mapping{::CreateFileMappingW(
        file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr)};
    if (!mapping) {
        return FileMappingError::IoFailure;
    }

    MappedView view{::MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0)};
    if (!view) {
        return FileMappingError::IoFailure;
    }

    FileMapping result;
    result.m_file    = std::move(file);
    result.m_mapping = std::move(mapping);
    result.m_view    = std::move(view);
    result.m_size    = static_cast<std::uint64_t>(size.QuadPart);
    return result;
}

}  // namespace neomifes::platform
