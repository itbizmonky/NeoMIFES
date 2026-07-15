#pragma once

// RAII wrappers for Win32 handle types.
// Kept header-only and dependency-free (only <windows.h>) so any layer can use them.

#include <windows.h>

#include <utility>

namespace neomifes::platform {

// Generic RAII wrapper. The Deleter is a stateless functor that closes the handle.
// Handle==HANDLE, HMODULE, HWND, HDC, HFONT, etc.
template <typename Handle, typename Deleter, Handle InvalidValue = Handle{}>
class HandleGuard {
public:
    HandleGuard() noexcept = default;
    explicit HandleGuard(Handle h) noexcept : m_handle(h) {}

    HandleGuard(const HandleGuard&)            = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    HandleGuard(HandleGuard&& other) noexcept
        : m_handle(std::exchange(other.m_handle, InvalidValue)) {}

    HandleGuard& operator=(HandleGuard&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.m_handle, InvalidValue));
        }
        return *this;
    }

    ~HandleGuard() { reset(); }

    void reset(Handle h = InvalidValue) noexcept {
        if (m_handle != InvalidValue) {
            Deleter{}(m_handle);
        }
        m_handle = h;
    }

    [[nodiscard]] Handle get() const noexcept { return m_handle; }
    [[nodiscard]] Handle release() noexcept {
        return std::exchange(m_handle, InvalidValue);
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return m_handle != InvalidValue;
    }

private:
    Handle m_handle = InvalidValue;
};

// Deleters ------------------------------------------------------------------
struct CloseHandleDeleter {
    void operator()(HANDLE h) const noexcept { ::CloseHandle(h); }
};
struct FreeLibraryDeleter {
    void operator()(HMODULE h) const noexcept { ::FreeLibrary(h); }
};
struct DestroyWindowDeleter {
    void operator()(HWND h) const noexcept { ::DestroyWindow(h); }
};
struct DeleteObjectDeleter {
    void operator()(HGDIOBJ h) const noexcept { ::DeleteObject(h); }
};
struct UnmapViewDeleter {
    void operator()(LPVOID p) const noexcept { ::UnmapViewOfFile(p); }
};

// Type aliases --------------------------------------------------------------
// NOTE: The invalid value for HANDLE-family varies; we pick the common one and
// callers should be aware. Use FileHandle (below) for file handles.
using KernelHandle    = HandleGuard<HANDLE, CloseHandleDeleter, nullptr>;
using ModuleHandle    = HandleGuard<HMODULE, FreeLibraryDeleter, nullptr>;
using WindowHandle    = HandleGuard<HWND, DestroyWindowDeleter, nullptr>;
using GdiObjectHandle = HandleGuard<HGDIOBJ, DeleteObjectDeleter, nullptr>;
// MapViewOfFile* returns LPVOID, released via UnmapViewOfFile (not CloseHandle).
using MappedView = HandleGuard<LPVOID, UnmapViewDeleter, nullptr>;

// FileHandle - RAII wrapper for HANDLE values from CreateFileW, whose invalid
// sentinel is INVALID_HANDLE_VALUE rather than nullptr like the HandleGuard
// aliases above. Deliberately NOT a HandleGuard<HANDLE, ..., INVALID_HANDLE_VALUE>
// instantiation: INVALID_HANDLE_VALUE expands to ((HANDLE)(LONG_PTR)-1), an
// integer-to-pointer conversion that MSVC accepts as a non-type template
// argument but Clang correctly rejects (such conversions are not permitted in
// core constant expressions per the standard) - confirmed locally, this broke
// clang-tidy while building fine under the real MSVC compiler. A default
// member initializer (used here) has no such restriction since it is
// evaluated at construction time, not as a compile-time constant.
class FileHandle {
public:
    FileHandle() noexcept = default;
    explicit FileHandle(HANDLE h) noexcept : m_handle(h) {}

    FileHandle(const FileHandle&)            = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    FileHandle(FileHandle&& other) noexcept
        : m_handle(std::exchange(other.m_handle, INVALID_HANDLE_VALUE)) {}

    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.m_handle, INVALID_HANDLE_VALUE));
        }
        return *this;
    }

    ~FileHandle() { reset(); }

    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
        if (m_handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_handle);
        }
        m_handle = h;
    }

    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return m_handle != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

}  // namespace neomifes::platform
