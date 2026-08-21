#include "key_bindings_presets.h"

namespace neomifes::core::detail {

std::map<std::u16string, std::vector<std::u16string>> neomifesStandardBindings() {
    return {
        {u"file.save", {u"Ctrl+S"}},
        {u"file.saveAs", {u"Ctrl+Shift+S"}},
        {u"file.open", {u"Ctrl+O"}},
        {u"file.new", {u"Ctrl+N"}},
        {u"tab.close", {u"Ctrl+W"}},
        {u"tab.next", {u"Ctrl+Tab", u"Ctrl+PageDown"}},
        {u"tab.previous", {u"Ctrl+Shift+Tab", u"Ctrl+PageUp"}},
        {u"tab.switch1", {u"Ctrl+1"}},
        {u"tab.switch2", {u"Ctrl+2"}},
        {u"tab.switch3", {u"Ctrl+3"}},
        {u"tab.switch4", {u"Ctrl+4"}},
        {u"tab.switch5", {u"Ctrl+5"}},
        {u"tab.switch6", {u"Ctrl+6"}},
        {u"tab.switch7", {u"Ctrl+7"}},
        {u"tab.switch8", {u"Ctrl+8"}},
        {u"tab.switch9", {u"Ctrl+9"}},
        {u"find.show", {u"Ctrl+F"}},
        {u"find.replace", {u"Ctrl+H"}},
        {u"find.next", {u"F3"}},
        {u"find.previous", {u"Shift+F3"}},
        {u"command.paletteShow", {u"Ctrl+Shift+P"}},
        {u"search.grep.show", {u"Ctrl+Shift+F"}},
        {u"view.outline.toggle", {u"Ctrl+Shift+O"}},
        {u"view.jsonTree.toggle", {u"Ctrl+Shift+J"}},
        {u"view.csvGrid.toggle", {u"Ctrl+Shift+G"}},
        {u"goto.line.show", {u"Ctrl+G"}},
        {u"bookmark.toggle", {u"Ctrl+F2"}},
        {u"bookmark.next", {u"F2"}},
        {u"bookmark.previous", {u"Shift+F2"}},
        {u"navigate.tagJump", {u"F12"}},
        {u"edit.copy", {u"Ctrl+C"}},
        {u"edit.cut", {u"Ctrl+X"}},
        {u"edit.paste", {u"Ctrl+V"}},
        {u"edit.undo", {u"Ctrl+Z"}},
        {u"edit.redo", {u"Ctrl+Y"}},
        {u"edit.toggleOverwriteMode", {u"Insert"}},
    };
}

std::map<std::u16string, std::vector<std::u16string>> sakuraBindings() {
    return {
        {u"file.new", {u"Ctrl+N"}},
        {u"file.open", {u"Ctrl+O"}},
        {u"file.save", {u"Ctrl+S"}},
        {u"file.saveAs", {u"Ctrl+Shift+S"}},
        {u"edit.undo", {u"Ctrl+Z"}},
        {u"edit.redo", {u"Ctrl+Y"}},
        {u"edit.cut", {u"Ctrl+X"}},
        {u"edit.copy", {u"Ctrl+C"}},
        {u"edit.paste", {u"Ctrl+V"}},
        {u"find.show", {u"Ctrl+F"}},
        {u"find.next", {u"F3"}},
        {u"find.previous", {u"Shift+F3"}},
        {u"find.replace", {u"Ctrl+R"}},
        {u"search.grep.show", {u"Ctrl+G"}},
        {u"goto.line.show", {u"Ctrl+J"}},
        {u"view.outline.toggle", {u"F11"}},
        {u"navigate.tagJump", {u"F12"}},
        {u"bookmark.toggle", {u"Ctrl+F2"}},
        {u"bookmark.next", {u"F2"}},
        {u"bookmark.previous", {u"Shift+F2"}},
        {u"tab.next", {u"Ctrl+Tab"}},
        {u"tab.previous", {u"Ctrl+Shift+Tab"}},
        // Deliberately unbound (no confirmed Sakura default): tab.close,
        // tab.switch1-9, command.paletteShow (no palette concept in Sakura),
        // edit.toggleOverwriteMode, view.jsonTree.toggle (WI-15c - no JSON
        // tree feature in Sakura), view.csvGrid.toggle (WI-16c - no CSV
        // grid feature in Sakura).
    };
}

std::map<std::u16string, std::vector<std::u16string>> hidemaruBindings() {
    return {
        {u"file.new", {u"Ctrl+N"}},
        {u"file.open", {u"Ctrl+O"}},
        {u"file.save", {u"Ctrl+S"}},
        {u"edit.undo", {u"Ctrl+Z"}},
        {u"edit.redo", {u"Ctrl+Y"}},
        {u"edit.cut", {u"Ctrl+X"}},
        {u"edit.copy", {u"Ctrl+C"}},
        {u"edit.paste", {u"Ctrl+V"}},
        {u"find.show", {u"Ctrl+F"}},
        {u"find.replace", {u"Ctrl+R"}},
        {u"goto.line.show", {u"Ctrl+G"}},
        {u"view.outline.toggle", {u"F11"}},
        {u"navigate.tagJump", {u"F10"}},
        // Deliberately unbound (could not be confirmed from a reliable
        // source, see this file's header comment): file.saveAs,
        // find.next, find.previous (F3 is Hidemaru's default for word
        // completion, not find-next), search.grep.show (Hidemaru's own
        // Grep command has no fixed default key at all), command.paletteShow
        // (no palette concept), bookmark.toggle/next/previous, tab.close,
        // tab.next, tab.previous, tab.switch1-9, edit.toggleOverwriteMode,
        // view.jsonTree.toggle (WI-15c - no JSON tree feature in Hidemaru),
        // view.csvGrid.toggle (WI-16c - no CSV grid feature in Hidemaru).
    };
}

std::map<std::u16string, std::vector<std::u16string>> vscodeBindings() {
    return {
        {u"file.new", {u"Ctrl+N"}},
        {u"file.open", {u"Ctrl+O"}},
        {u"file.save", {u"Ctrl+S"}},
        {u"file.saveAs", {u"Ctrl+Shift+S"}},
        {u"edit.undo", {u"Ctrl+Z"}},
        {u"edit.redo", {u"Ctrl+Y"}},
        {u"edit.cut", {u"Ctrl+X"}},
        {u"edit.copy", {u"Ctrl+C"}},
        {u"edit.paste", {u"Ctrl+V"}},
        {u"find.show", {u"Ctrl+F"}},
        {u"find.next", {u"F3"}},
        {u"find.previous", {u"Shift+F3"}},
        {u"find.replace", {u"Ctrl+H"}},
        {u"search.grep.show", {u"Ctrl+Shift+F"}},
        {u"goto.line.show", {u"Ctrl+G"}},
        {u"command.paletteShow", {u"Ctrl+Shift+P", u"F1"}},
        {u"view.outline.toggle", {u"Ctrl+Shift+O"}},
        // "Go to Definition" (editor.action.revealDefinition) - the closest
        // VS Code analog to "tag jump".
        {u"navigate.tagJump", {u"F12"}},
        {u"tab.close", {u"Ctrl+W"}},
        {u"tab.next", {u"Ctrl+PageDown"}},
        {u"tab.previous", {u"Ctrl+PageUp"}},
        {u"tab.switch1", {u"Ctrl+1"}},
        {u"tab.switch2", {u"Ctrl+2"}},
        {u"tab.switch3", {u"Ctrl+3"}},
        {u"tab.switch4", {u"Ctrl+4"}},
        {u"tab.switch5", {u"Ctrl+5"}},
        {u"tab.switch6", {u"Ctrl+6"}},
        {u"tab.switch7", {u"Ctrl+7"}},
        {u"tab.switch8", {u"Ctrl+8"}},
        {u"tab.switch9", {u"Ctrl+9"}},
        // Deliberately unbound: VS Code has no built-in bookmark feature or
        // insert/overtype toggle by default (both are extension territory).
        // view.jsonTree.toggle (WI-15c) is also unbound - VS Code's own
        // Outline view covers JSON structure but has no dedicated default
        // shortcut of its own to point at. view.csvGrid.toggle (WI-16c) is
        // likewise unbound - VS Code's CSV support is extension territory
        // (e.g. "Rainbow CSV"), no built-in default shortcut exists.
    };
}

}  // namespace neomifes::core::detail
