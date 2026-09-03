#pragma once

// Settings - persisted user preferences (WI-08, master_roadmap.md §8.6.1).
// Headless (no Win32/JSON-library type in this header - see settings.cpp for
// the nlohmann::json usage) and path-independent, same "inject the path,
// don't resolve it internally" split as core::SearchHistory
// (search_history.h) - callers pass platform::resolveAppDataDir()'s result.
//
// A plain value type (all fields public, defaulted) rather than an
// accessor-per-field class: unlike SearchHistory (which has real behavior -
// MRU ordering, browse-position derivation), Settings is just a persisted
// data bag. The defaulted operator== exists so round-trip tests can compare
// a loaded Settings against the one that was saved in a single EXPECT_EQ
// instead of one assertion per field.

#include <cstdint>
#include <filesystem>
#include <string>

namespace neomifes::core {

class Settings {
public:
    // Loads from `path`. A missing file, unparsable JSON, an unexpected
    // "version" field, or an out-of-range numeric value (tabWidth == 0 or
    // > 32, fontSizeDips <= 0.0F) all fall back to that field's default
    // rather than a fatal error or a value that would break downstream
    // consumers (DirectWrite's SetIncrementalTabStop() rejects 0,
    // indent_guide_math.h already treats tabWidth==0 as degenerate) - same
    // "never fail app startup over a bad settings file" contract
    // SearchHistory::loadFrom() established. Missing individual fields
    // (e.g. an older settings.json written before a field existed) leave
    // just that field at its default rather than discarding the whole
    // file - forward compatibility for future added fields.
    [[nodiscard]] static Settings loadFrom(const std::filesystem::path& path);

    // Writes to `path` as JSON. Best-effort: any failure (parent directory
    // missing, disk full, permission denied) is silently ignored, same
    // rationale as SearchHistory::saveTo(). Called from
    // normal_mode_wiring.cpp's view.theme.* command palette entries (WI-09)
    // - the first in-app mutation+persist path for `settings` (settings.
    // reload only ever reads). Still exists independently of that caller so
    // the load/save/reload round trip stays testable.
    void saveTo(const std::filesystem::path& path) const;

    std::u16string fontFamily            = u"Consolas";
    float          fontSizeDips          = 14.0F;
    std::uint32_t  tabWidth              = 4;
    bool           insertSpacesForTab    = false;  // persisted only in WI-08 - not wired to handleChar()'s
                                                    // Tab-key insertion path (out of scope, see build_plan.md)
    bool           showLineNumbers       = true;
    bool           showMinimap           = true;
    // WI-21b:折り返し(word wrap) - see render::RenderPipeline::setWordWrap()
    // for the DirectWrite/layout-cache side of this toggle. Not yet
    // reachable from any menu/command-palette entry as of WI-21b - see that
    // WI's own build_plan.md section for why (WI-21e wires the caller).
    bool           wordWrap              = false;
    // WI-11: autosave interval in seconds (app::autoSaveAllDirtySessions(),
    // wired via MainWindow::startAutoSaveTimer()). 0 is a distinct,
    // explicit sentinel meaning "autosave disabled" (not merely "not yet
    // configured") - a user who explicitly sets this to 0 in settings.json
    // gets no periodic-timer autosave; the default below (60) is what a
    // fresh install with no settings.json at all gets instead, via
    // loadFrom()'s missing-field fallback.
    std::uint32_t  autoSaveIntervalSeconds = 60;
    // WI-11: whether saveFile() is asked to keep a durable path+".bak" of
    // the pre-save content (document::saveFile()'s keepBackup parameter).
    bool           createBackupOnSave    = true;
    std::u16string themeName             = u"dark"; // consumed by WI-09 (theme), live

    friend bool operator==(const Settings&, const Settings&) = default;
};

}  // namespace neomifes::core
