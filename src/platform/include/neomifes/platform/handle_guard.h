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
// callers should be aware. Use INVALID_HANDLE_VALUE variant for file handles.
using KernelHandle    = HandleGuard<HANDLE, CloseHandleDeleter, nullptr>;
using ModuleHandle    = HandleGuard<HMODULE, FreeLibraryDeleter, nullptr>;
using WindowHandle    = HandleGuard<HWND, DestroyWindowDeleter, nullptr>;
using GdiObjectHandle = HandleGuard<HGDIOBJ, DeleteObjectDeleter, nullptr>;

// CreateFileW returns INVALID_HANDLE_VALUE (not nullptr) on failure - a
// distinct sentinel from the other HANDLE-family wrappers above.
using FileHandle = HandleGuard<HANDLE, CloseHandleDeleter, INVALID_HANDLE_VALUE>;
// MapViewOfFile* returns LPVOID, released via UnmapViewOfFile (not CloseHandle).
using MappedView = HandleGuard<LPVOID, UnmapViewDeleter, nullptr>;

}  // namespace neomifes::platform
