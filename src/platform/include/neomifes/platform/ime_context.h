#pragma once

// RAII wrapper for HIMC (Win32 Input Method Manager context handles, WI-06).
// Kept header-only and dependency-free (only <windows.h>/<imm.h>), matching
// handle_guard.h's own header-only precedent - not built on HandleGuard<>
// itself because ImmReleaseContext(HWND, HIMC) needs BOTH the owning window
// and the context handle to release, unlike HandleGuard<>'s Deleter contract
// (a stateless functor taking only the handle).

#include <windows.h>

#include <imm.h>

#include <utility>

namespace neomifes::platform {

class ImeContext {
public:
    explicit ImeContext(HWND hwnd) noexcept : m_hwnd(hwnd), m_himc(::ImmGetContext(hwnd)) {}

    ImeContext(const ImeContext&)            = delete;
    ImeContext& operator=(const ImeContext&) = delete;

    ImeContext(ImeContext&& other) noexcept
        : m_hwnd(other.m_hwnd), m_himc(std::exchange(other.m_himc, nullptr)) {}

    ImeContext& operator=(ImeContext&& other) noexcept {
        if (this != &other) {
            reset();
            m_hwnd = other.m_hwnd;
            m_himc = std::exchange(other.m_himc, nullptr);
        }
        return *this;
    }

    ~ImeContext() { reset(); }

    void reset() noexcept {
        if (m_himc != nullptr) {
            ::ImmReleaseContext(m_hwnd, m_himc);
            m_himc = nullptr;
        }
    }

    [[nodiscard]] HIMC get() const noexcept { return m_himc; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_himc != nullptr; }

private:
    HWND m_hwnd = nullptr;
    HIMC m_himc = nullptr;
};

}  // namespace neomifes::platform
