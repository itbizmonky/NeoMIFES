#pragma once

// JsonPathBar - "JSON: Evaluate JSONPath" command's single WC_EDIT overlay
// (WI-15e). Deliberately a near-copy of GotoLineBar (goto_line_bar.h) rather
// than a generalized shared base - same "one text field, submit raw text on
// Enter" shape (no debounce/live-preview: an in-progress JSONPath expression
// isn't evaluated until Enter, avoiding a stream of error dialogs while the
// user is still typing). Win32-mechanics-only, same "knows nothing about
// core::/document::/jsontree::" separation as GotoLineBar/FindDialog -
// json_path.h's actual parsing/evaluation happens in the caller
// (src/app/normal_mode_wiring.cpp).

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

struct JsonPathBarConfig {
    // Enter - the edit's current raw text, unparsed (same "push the current
    // text" shape as GotoLineBarConfig::onSubmit - the caller parses via
    // jsontree::parseJsonPath()). Not fired while an IME composition is in
    // progress.
    std::function<void(std::u16string_view input)> onSubmit;
    // Escape. The caller is responsible for restoring focus to the document
    // editing area, same contract as GotoLineBarConfig::onClosed.
    std::function<void()> onClosed;
};

class JsonPathBar {
public:
    JsonPathBar()  = default;
    ~JsonPathBar() = default;

    JsonPathBar(const JsonPathBar&)            = delete;
    JsonPathBar& operator=(const JsonPathBar&) = delete;
    JsonPathBar(JsonPathBar&&)                 = delete;
    JsonPathBar& operator=(JsonPathBar&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const JsonPathBarConfig& config);

    // Clears the edit, shows it, focuses it. Re-invoking while already open
    // resets to this same state, matching GotoLineBar::show()'s convention.
    void show() noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    [[nodiscard]] bool handleSubclassKeyDown(UINT vkCode) noexcept;
    void ensureFont(float dpiScale) noexcept;
    [[nodiscard]] std::u16string readEditText() const;

    neomifes::platform::WindowHandle    m_hwndEdit;
    neomifes::platform::GdiObjectHandle m_font;
    // Same "CJK IME一級市民" guard as GotoLineBar/FindDialog/CommandPalette -
    // Enter/Escape must not be intercepted mid-composition.
    bool m_composing = false;
    JsonPathBarConfig m_config;
};

}  // namespace neomifes::ui
