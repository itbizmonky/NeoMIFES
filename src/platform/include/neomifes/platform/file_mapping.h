#pragma once

// FileMapping - RAII wrapper for a read-only memory-mapped file view.
//
// Wraps CreateFileW + CreateFileMappingW + MapViewOfFile so a caller gets a
// stable const-byte view into the file's contents for the lifetime of the
// FileMapping object. On x64, mapping even a 10GB file does not itself
// consume 10GB of RAM - the OS pages content in lazily as it is actually
// touched, only that resident portion counts against process working set.
// This is the property Document Engine's Lazy Decode (see
// neomifes::document::OriginalBuffer) depends on: we never need to read()
// the whole file into a heap buffer just to open it.

#include "neomifes/platform/handle_guard.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <variant>

namespace neomifes::platform {

enum class FileMappingError {
    NotFound,
    PermissionDenied,
    IoFailure,
    EmptyFile,
};

class FileMapping {
public:
    FileMapping() noexcept = default;

    FileMapping(const FileMapping&)                = delete;
    FileMapping& operator=(const FileMapping&)     = delete;
    FileMapping(FileMapping&&) noexcept            = default;
    FileMapping& operator=(FileMapping&&) noexcept = default;
    ~FileMapping() = default;  // member RAII wrappers unmap/close in reverse declaration order

    // Opens `path` read-only and maps the entire file into this process's
    // address space. Shares READ, WRITE, and DELETE with other processes -
    // the editor never takes an exclusive lock on documents it has open, and
    // FILE_SHARE_DELETE specifically means the file can still be deleted or
    // renamed (by the user, git, an external tool, etc.) while NeoMIFES has
    // it mapped; without it Windows blocks delete/rename for as long as the
    // mapping is alive.
    [[nodiscard]] static std::variant<FileMapping, FileMappingError>
        open(const std::filesystem::path& path);

    [[nodiscard]] std::span<const std::byte> data() const noexcept {
        if (m_view.get() == nullptr) {
            return {};
        }
        return {static_cast<const std::byte*>(m_view.get()),
                static_cast<std::size_t>(m_size)};
    }

    // Ties validity to m_view rather than trusting m_size directly: the
    // defaulted move constructor/assignment resets m_view (a HandleGuard)
    // to its empty state on the moved-from object, but m_size is a plain
    // uint64_t with ordinary copy semantics - it would otherwise keep its
    // stale value after a move, reporting a nonzero size for a mapping that
    // no longer has a live view.
    [[nodiscard]] std::uint64_t size() const noexcept {
        return (m_view.get() != nullptr) ? m_size : 0;
    }

private:
    // Declaration order matters: destructors run in reverse, so m_view
    // (UnmapViewOfFile) unmaps before m_mapping/m_file close.
    FileHandle    m_file;
    KernelHandle  m_mapping;
    MappedView    m_view;
    std::uint64_t m_size = 0;
};

}  // namespace neomifes::platform
