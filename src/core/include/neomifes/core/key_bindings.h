#pragma once

// KeyBindings - persisted, user-configurable command-to-key-chord mapping
// (WI-10, build_plan.md §5 / master_roadmap.md §8.6.2). Headless (no Win32/
// ui::CommandId/JSON-library type in this header) and path-independent, same
// "inject the path, don't resolve it internally" split as core::Settings/
// core::SearchHistory - callers pass platform::resolveAppDataDir()'s result.
//
// Deliberately stores commands and chords as PLAIN STRINGS, never
// ui::CommandId or a Win32 VK_* value: neomifes::core must not depend on
// neomifes::ui (CLAUDE.md §3, lower layers don't know upper layers) or on
// Win32 keyboard types. The command-string keys match ui::commandIdToString()
// ("file.save", "find.show", ...) and are resolved back to a CommandId only
// at the app layer (see src/app/include/neomifes/app/keybinding_dispatch.h);
// the chord strings ("Ctrl+Shift+P") are parsed into a VK/modifier struct
// only at the app layer too (see src/app/include/neomifes/app/key_chord.h) -
// the same "low-layer stores a string, app-layer bridges to the upper-layer
// enum" pattern WI-09's theme_settings.h established for
// render::ThemeKind <-> core::Settings::themeName.
//
// A KeyBindings object represents ONE currently-active binding set, not all
// 4 presets simultaneously - switching presets (see forPreset()) replaces
// the whole set, mirroring how WI-09's view.theme.* commands replace the
// active theme rather than holding all themes at once.

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace neomifes::core {

class KeyBindings {
public:
    // Loads from `path`. A missing file, unparsable JSON, a non-object
    // "bindings" value, or an unexpected "version" field all fall back to
    // forPreset(u"neomifes") rather than a fatal error or partial/undefined
    // state - same "never fail app startup over a bad config file" contract
    // Settings::loadFrom()/SearchHistory::loadFrom() established. A single
    // malformed entry inside "bindings" (non-array value, or an array
    // containing a non-string element) leaves just that command unbound
    // rather than discarding the whole file - SearchHistory::loadFrom()'s
    // same "tolerate a stray malformed entry" convention for its own array.
    [[nodiscard]] static KeyBindings loadFrom(const std::filesystem::path& path);

    // Writes to `path` as JSON. Best-effort: any failure (parent directory
    // missing, disk full, permission denied) is silently ignored, same
    // rationale as Settings::saveTo()/SearchHistory::saveTo().
    void saveTo(const std::filesystem::path& path) const;

    // Builds the embedded default table for `presetName` ("neomifes" /
    // "hidemaru" / "sakura" / "vscode", see key_bindings_presets.cpp).
    // Any unrecognized name falls back to "neomifes" - same safe-default
    // contract app::parseThemeKind() established for ThemeKind. A command
    // absent from a preset's table (most of "hidemaru" - several defaults
    // could not be verified against a public reference, see
    // key_bindings_presets.cpp's own comment) is simply unbound
    // (chordsFor() returns {}), never silently backfilled from another
    // preset - build_plan.md's WI-10 mandate: "誤ったプリセットは無いより
    // 悪い" (a wrong preset is worse than none).
    [[nodiscard]] static KeyBindings forPreset(std::u16string_view presetName);

    // `chordId` is the dotted command identifier ui::commandIdToString()
    // returns ("file.save", "tab.switch1", ...). Empty if the command is
    // unbound under the current bindings (either the preset never defined
    // it, or a hand-edited keybindings.json explicitly cleared it). May
    // contain more than one chord (e.g. "tab.next" -> ["Ctrl+Tab",
    // "Ctrl+PageDown"], matching the pre-WI-10 kAcceleratorTable's existing
    // two-row-per-command precedent for that command).
    [[nodiscard]] std::vector<std::u16string> chordsFor(std::u16string_view chordId) const;

    // Replaces every binding for `chordId` with exactly `chords` (an empty
    // vector unbinds it). Used by keybindings.json's own load path and by
    // any future settings-editing UI; not currently exposed via a command-
    // palette entry (WI-10 only exposes whole-file reload/preset-switch).
    void setChords(std::u16string_view chordId, std::vector<std::u16string> chords);

    // Which preset this object was last loaded/switched from - persisted
    // purely as a display label (e.g. a future settings dialog showing
    // "current: サクラ"). NOT authoritative over the actual bindings map - a
    // hand-edited keybindings.json can freely diverge from its stated
    // preset, exactly as Settings::themeName is a free-form string that
    // isn't cross-checked against RenderPipeline's live theme.
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes) -
    // deliberately public: a plain display label with no invariant to
    // protect, unlike m_bindings below (which needs encapsulation because
    // chordsFor()/setChords() maintain its shape). A getter/setter pair
    // would add indirection without protecting anything.
    std::u16string presetName = u"neomifes";

    friend bool operator==(const KeyBindings&, const KeyBindings&) = default;

private:
    std::map<std::u16string, std::vector<std::u16string>> m_bindings;
};

}  // namespace neomifes::core
