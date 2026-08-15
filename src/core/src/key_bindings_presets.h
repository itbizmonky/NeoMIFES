#pragma once

// Internal to src/core/ only (no include/ path) - embedded default binding
// tables consumed by KeyBindings::forPreset() (key_bindings.cpp). Split into
// its own translation unit purely so the 4 large literal tables don't bloat
// key_bindings.cpp's own load/save logic (CLAUDE.md rule 4, keep functions/
// files focused).
//
// Every non-empty entry in every table below was verified this session
// against an external source (WI-10's explicit mandate, build_plan.md:
// "秀丸/サクラのキーバインドは記憶で書かない" - do not write Hidemaru/Sakura
// keybindings from memory) - see each function's own comment for its
// source. A command absent from a table is INTENTIONALLY left unbound for
// that preset, never guessed - "誤ったプリセットは無いより悪い" (a wrong
// preset is worse than none).

#include <map>
#include <string>
#include <vector>

namespace neomifes::core::detail {

// The "neomifes" preset - a literal transcription of this codebase's own
// pre-WI-10 defaults: command_dispatch.h's kAcceleratorTable (16 commands)
// plus normal_mode_wiring.cpp's handle*Key() functions' hardcoded chord
// checks (the other 18) - read directly from source during WI-10's design,
// not from memory.
[[nodiscard]] std::map<std::u16string, std::vector<std::u16string>> neomifesStandardBindings();

// Sourced from Sakura Editor's official help site
// (https://sakura-editor.github.io/help/HLP000107.html, fetched during
// WI-10's design), which publishes a complete default keybinding table.
// Commands with no NeoMIFES equivalent in Sakura's list (e.g. a command
// palette - Sakura has no such concept) are simply absent here.
[[nodiscard]] std::map<std::u16string, std::vector<std::u16string>> sakuraBindings();

// Hidemaru Editor publishes its key-assignment DIALOG's operating
// instructions (help.maruo.co.jp) but not a flat default-table page.
// Community references (nymemo.com and others, cross-checked via multiple
// searches during WI-10's design) confirmed only the entries present here;
// several important commands (Save As, Grep - which has NO fixed Hidemaru
// default at all, users assign their own - Find Next/Previous, all 3
// bookmark commands, tab switching, the command palette concept, and
// overwrite-mode toggle) could not be confirmed from a source reliable
// enough to trust, and are deliberately left unbound rather than guessed.
[[nodiscard]] std::map<std::u16string, std::vector<std::u16string>> hidemaruBindings();

// Sourced from code.visualstudio.com's official default-keybindings
// reference plus targeted verification searches (fetched during WI-10's
// design) for the handful of commands not covered on that single page
// (editor-tab switching, Ctrl+1..9 index switching, Go to Definition as the
// closest analog to "tag jump"). VS Code has no built-in bookmark feature
// or insert/overtype toggle by default (both are extension territory), so
// those stay unbound here, same as Hidemaru's unconfirmed entries.
[[nodiscard]] std::map<std::u16string, std::vector<std::u16string>> vscodeBindings();

}  // namespace neomifes::core::detail
