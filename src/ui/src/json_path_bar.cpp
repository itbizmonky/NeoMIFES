#include "neomifes/ui/json_path_bar.h"

#include <commctrl.h>

#include <cstddef>
#include <string>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

constexpr int      kEditId     = 11001;
constexpr UINT_PTR kSubclassId = 1;

// Layout constants in DIPs (96-DPI baseline) - same positioning family as
// GotoLineBar/CommandPalette (centered near the top of the parent), just a
// bit wider to fit a realistic JSONPath expression like "$.users[*].name".
constexpr float kWidthDips     = 360.0F;
constexpr float kHeightDips    = 28.0F;
constexpr float kTopMarginDips = 60.0F;
constexpr float kFontSizeDips  = 14.0F;

}  // namespace

bool JsonPathBar::create(HWND parent, HINSTANCE hInstance, const JsonPathBarConfig& config) {
    m_config = config;

    HWND edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10,
                                  10, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kEditId)),
                                  hInstance, nullptr);
    if (edit == nullptr) {
        return false;
    }
    m_hwndEdit.reset(edit);

    if (::SetWindowSubclass(m_hwndEdit.get(), &JsonPathBar::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void JsonPathBar::show() noexcept {
    if (!m_hwndEdit) {
        return;
    }
    ::SetWindowTextW(m_hwndEdit.get(), L"");
    ::ShowWindow(m_hwndEdit.get(), SW_SHOW);
    ::SetFocus(m_hwndEdit.get());
}

void JsonPathBar::hide() noexcept {
    if (!m_hwndEdit) {
        return;
    }
    ::ShowWindow(m_hwndEdit.get(), SW_HIDE);
}

bool JsonPathBar::isVisible() const noexcept {
    return static_cast<bool>(m_hwndEdit) && ::IsWindowVisible(m_hwndEdit.get()) != FALSE;
}

void JsonPathBar::onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept {
    if (!m_hwndEdit) {
        return;
    }
    ensureFont(dpiScale);

    const auto widthPx      = static_cast<int>(kWidthDips * dpiScale);
    const auto heightPx     = static_cast<int>(kHeightDips * dpiScale);
    const auto topMarginPx  = static_cast<int>(kTopMarginDips * dpiScale);

    const int startX = (static_cast<int>(parentWidth) - widthPx) / 2;
    ::SetWindowPos(m_hwndEdit.get(), nullptr, startX, topMarginPx, widthPx, heightPx,
                  SWP_NOZORDER | SWP_NOACTIVATE);
}

void JsonPathBar::ensureFont(float dpiScale) noexcept {
    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT font = ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               kPitchAndFamily, L"Segoe UI");
    if (font == nullptr) {
        return;
    }
    m_font.reset(reinterpret_cast<HGDIOBJ>(font));
    if (m_hwndEdit) {
        ::SendMessageW(m_hwndEdit.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

std::u16string JsonPathBar::readEditText() const {
    if (!m_hwndEdit) {
        return {};
    }
    const int length = ::GetWindowTextLengthW(m_hwndEdit.get());
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(m_hwndEdit.get(), buffer.data(), length + 1);
    }
    return std::u16string(neomifes::util::fromWstringView(buffer));
}

bool JsonPathBar::handleSubclassKeyDown(UINT vkCode) noexcept {
    // While an IME composition is active, Enter/Escape belong to the IME -
    // same guard as GotoLineBar/FindBar/CommandPalette.
    if (m_composing) {
        return false;
    }
    switch (vkCode) {
        case VK_RETURN:
            if (m_config.onSubmit) {
                m_config.onSubmit(readEditText());
            }
            return true;
        case VK_ESCAPE:
            if (m_config.onClosed) {
                m_config.onClosed();
            }
            return true;
        default:
            return false;
    }
}

LRESULT JsonPathBar::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_IME_STARTCOMPOSITION:
            m_composing = true;
            break;
        case WM_IME_ENDCOMPOSITION:
            m_composing = false;
            break;
        case WM_KEYDOWN:
            if (handleSubclassKeyDown(static_cast<UINT>(wParam))) {
                return 0;
            }
            break;
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &JsonPathBar::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK JsonPathBar::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<JsonPathBar*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
