#include "neomifes/app/key_chord.h"

#include <array>
#include <cwctype>
#include <vector>

namespace neomifes::app {

namespace {

[[nodiscard]] bool equalsIgnoreCase(std::u16string_view a, std::u16string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towupper(static_cast<wint_t>(a[i])) != std::towupper(static_cast<wint_t>(b[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::u16string_view> splitOnPlus(std::u16string_view text) {
    std::vector<std::u16string_view> tokens;
    std::size_t                      start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == u'+') {
            tokens.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return tokens;
}

// Named non-alphanumeric keys this module recognizes, both directions.
struct NamedKey {
    std::u16string_view name;
    UINT                virtualKey;
};

constexpr std::array<NamedKey, 27> kNamedKeys{{
    {.name = u"F1", .virtualKey = VK_F1},
    {.name = u"F2", .virtualKey = VK_F2},
    {.name = u"F3", .virtualKey = VK_F3},
    {.name = u"F4", .virtualKey = VK_F4},
    {.name = u"F5", .virtualKey = VK_F5},
    {.name = u"F6", .virtualKey = VK_F6},
    {.name = u"F7", .virtualKey = VK_F7},
    {.name = u"F8", .virtualKey = VK_F8},
    {.name = u"F9", .virtualKey = VK_F9},
    {.name = u"F10", .virtualKey = VK_F10},
    {.name = u"F11", .virtualKey = VK_F11},
    {.name = u"F12", .virtualKey = VK_F12},
    {.name = u"Tab", .virtualKey = VK_TAB},
    {.name = u"Insert", .virtualKey = VK_INSERT},
    {.name = u"Delete", .virtualKey = VK_DELETE},
    {.name = u"Home", .virtualKey = VK_HOME},
    {.name = u"End", .virtualKey = VK_END},
    {.name = u"PageUp", .virtualKey = VK_PRIOR},
    {.name = u"PageDown", .virtualKey = VK_NEXT},
    {.name = u"Enter", .virtualKey = VK_RETURN},
    {.name = u"Escape", .virtualKey = VK_ESCAPE},
    {.name = u"Space", .virtualKey = VK_SPACE},
    {.name = u"Up", .virtualKey = VK_UP},
    {.name = u"Down", .virtualKey = VK_DOWN},
    {.name = u"Left", .virtualKey = VK_LEFT},
    {.name = u"Right", .virtualKey = VK_RIGHT},
    {.name = u"Backspace", .virtualKey = VK_BACK},
}};

[[nodiscard]] std::optional<UINT> resolveKeyToken(std::u16string_view token) noexcept {
    if (token.size() == 1) {
        const auto upper = static_cast<char16_t>(std::towupper(static_cast<wint_t>(token[0])));
        if ((upper >= u'A' && upper <= u'Z') || (upper >= u'0' && upper <= u'9')) {
            return static_cast<UINT>(upper);
        }
        return std::nullopt;
    }
    for (const NamedKey& named : kNamedKeys) {
        if (equalsIgnoreCase(token, named.name)) {
            return named.virtualKey;
        }
    }
    return std::nullopt;
}

// Owning (not string_view): the single-alnum-character case has no static
// storage to point into, unlike the named-key table entries.
[[nodiscard]] std::u16string keyTokenFor(UINT virtualKey) {
    if ((virtualKey >= u'A' && virtualKey <= u'Z') || (virtualKey >= u'0' && virtualKey <= u'9')) {
        // Braced-init here would select basic_string's
        // initializer_list<char16_t> constructor instead of the (count,
        // char) one, silently producing a 2-character string
        // {char16_t(1), virtualKey} rather than the intended 1-character
        // one.
        // NOLINTNEXTLINE(modernize-return-braced-init-list)
        return std::u16string(1, static_cast<char16_t>(virtualKey));
    }
    for (const NamedKey& named : kNamedKeys) {
        if (named.virtualKey == virtualKey) {
            return std::u16string(named.name);
        }
    }
    return u"";
}

}  // namespace

std::optional<KeyChord> parseKeyChord(std::u16string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    const std::vector<std::u16string_view> tokens = splitOnPlus(text);
    if (tokens.empty() || tokens.back().empty()) {
        return std::nullopt;
    }

    KeyChord chord;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::u16string_view token = tokens[i];
        if (equalsIgnoreCase(token, u"Ctrl")) {
            chord.ctrl = true;
        } else if (equalsIgnoreCase(token, u"Shift")) {
            chord.shift = true;
        } else if (equalsIgnoreCase(token, u"Alt")) {
            chord.alt = true;
        } else {
            return std::nullopt;  // unrecognized modifier token
        }
    }

    const auto virtualKey = resolveKeyToken(tokens.back());
    if (!virtualKey) {
        return std::nullopt;
    }
    chord.virtualKey = *virtualKey;
    return chord;
}

std::u16string keyChordToString(const KeyChord& chord) {
    if (chord.virtualKey == 0) {
        return u"";
    }
    std::u16string result;
    if (chord.ctrl) {
        result += u"Ctrl+";
    }
    if (chord.shift) {
        result += u"Shift+";
    }
    if (chord.alt) {
        result += u"Alt+";
    }
    result += keyTokenFor(chord.virtualKey);
    return result;
}

}  // namespace neomifes::app
