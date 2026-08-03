#pragma once

// ToastState (Phase 8e) - minimal, Win32-independent state layer behind
// NeoMifesCoreApi::showToast (see plugin_sdk.h). Holds at most ONE pending
// message: last write wins, no queueing (CLAUDE.md rule 10 - the only
// consumer today is a headless integration test; queueing is deferred
// until a real UI widget needs it). Actual Win32 rendering (a visible
// popup window, auto-dismiss timer) is intentionally NOT built yet - see
// ADR-019: this phase stays headless like every other Phase 8 sub-phase,
// main.cpp is not wired to a live instance of this class.

#include <string>
#include <string_view>

namespace neomifes::ui {

class ToastState {
public:
    ToastState() noexcept = default;

    // Sets the current message and marks it visible. Overwrites any
    // previously still-pending message (last write wins).
    void show(std::u16string_view message) {
        m_message = message;
        m_visible = true;
    }

    // Clears the current message; isVisible() becomes false afterward.
    void hide() noexcept {
        m_message.clear();
        m_visible = false;
    }

    [[nodiscard]] bool isVisible() const noexcept { return m_visible; }

    // Only meaningful while isVisible() is true; empty otherwise.
    [[nodiscard]] const std::u16string& message() const noexcept { return m_message; }

private:
    std::u16string m_message;
    bool           m_visible = false;
};

}  // namespace neomifes::ui
