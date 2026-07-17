#include "neomifes/platform/clipboard.h"

#include <algorithm>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::platform {

namespace {

// RAII for the OpenClipboard/CloseClipboard pair. Deliberately not a
// HandleGuard instantiation (handle_guard.h) - CloseClipboard takes no
// handle, since the "clipboard" is a process-wide lock rather than an owned
// object.
class ClipboardScope {
public:
    explicit ClipboardScope(HWND owner) noexcept : m_open(::OpenClipboard(owner) != 0) {}

    ClipboardScope(const ClipboardScope&)            = delete;
    ClipboardScope& operator=(const ClipboardScope&) = delete;
    ClipboardScope(ClipboardScope&&)                 = delete;
    ClipboardScope& operator=(ClipboardScope&&)      = delete;

    ~ClipboardScope() {
        if (m_open) {
            ::CloseClipboard();
        }
    }

    [[nodiscard]] bool isOpen() const noexcept { return m_open; }

private:
    bool m_open;
};

}  // namespace

bool setClipboardText(HWND owner, std::u16string_view text) noexcept {
    const ClipboardScope scope(owner);
    if (!scope.isOpen()) {
        return false;
    }

    // +1 for the null terminator CF_UNICODETEXT requires.
    const SIZE_T byteCount = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL      mem       = ::GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (mem == nullptr) {
        return false;
    }
    void* locked = ::GlobalLock(mem);
    if (locked == nullptr) {
        ::GlobalFree(mem);
        return false;
    }
    auto*                    dest    = static_cast<wchar_t*>(locked);
    const std::wstring_view srcView = util::toWstringView(text);
    std::ranges::copy(srcView, dest);
    dest[text.size()] = L'\0';
    ::GlobalUnlock(mem);

    if (!::EmptyClipboard()) {
        ::GlobalFree(mem);
        return false;
    }
    // Ownership of `mem` transfers to the clipboard on success - it must not
    // be GlobalFree'd in that case. SetClipboardData failing means the
    // clipboard never took ownership, so we must free it ourselves.
    if (::SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
        ::GlobalFree(mem);
        return false;
    }
    return true;
}

std::optional<std::u16string> getClipboardText(HWND owner) {
    const ClipboardScope scope(owner);
    if (!scope.isOpen()) {
        return std::nullopt;
    }
    HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (data == nullptr) {
        return std::nullopt;
    }
    const void* locked = ::GlobalLock(data);
    if (locked == nullptr) {
        return std::nullopt;
    }
    // GlobalLock succeeding on a CF_UNICODETEXT handle guarantees a
    // null-terminated wide string per the Win32 clipboard contract.
    std::u16string result(util::fromWchar(static_cast<const wchar_t*>(locked)));
    ::GlobalUnlock(data);
    return result;
}

}  // namespace neomifes::platform
