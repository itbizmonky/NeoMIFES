#include "neomifes/core/key_bindings.h"

#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "json_string_convert.h"
#include "key_bindings_presets.h"

namespace neomifes::core {

namespace {

using detail::fromUtf8;
using detail::toUtf8;

constexpr int kFormatVersion = 1;

}  // namespace

KeyBindings KeyBindings::forPreset(std::u16string_view presetName) {
    KeyBindings bindings;
    bindings.presetName = std::u16string(presetName);
    if (presetName == u"hidemaru") {
        bindings.m_bindings = detail::hidemaruBindings();
    } else if (presetName == u"sakura") {
        bindings.m_bindings = detail::sakuraBindings();
    } else if (presetName == u"vscode") {
        bindings.m_bindings = detail::vscodeBindings();
    } else {
        bindings.presetName = u"neomifes";
        bindings.m_bindings = detail::neomifesStandardBindings();
    }
    return bindings;
}

std::vector<std::u16string> KeyBindings::chordsFor(std::u16string_view chordId) const {
    const auto it = m_bindings.find(std::u16string(chordId));
    if (it == m_bindings.end()) {
        return {};
    }
    return it->second;
}

void KeyBindings::setChords(std::u16string_view chordId, std::vector<std::u16string> chords) {
    m_bindings[std::u16string(chordId)] = std::move(chords);
}

KeyBindings KeyBindings::loadFrom(const std::filesystem::path& path) {
    KeyBindings bindings = forPreset(u"neomifes");

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return bindings;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto parsed = nlohmann::json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return bindings;
    }
    const auto versionIt = parsed.find("version");
    if (versionIt == parsed.end() || !versionIt->is_number_integer() ||
        versionIt->get<int>() != kFormatVersion) {
        return bindings;
    }
    const auto bindingsIt = parsed.find("bindings");
    if (bindingsIt == parsed.end() || !bindingsIt->is_object()) {
        return bindings;
    }

    if (const auto presetIt = parsed.find("preset"); presetIt != parsed.end() && presetIt->is_string()) {
        if (auto text = fromUtf8(presetIt->get<std::string>())) {
            bindings.presetName = std::move(*text);
        }
    }

    bindings.m_bindings.clear();
    for (const auto& [key, value] : bindingsIt->items()) {
        auto chordIdText = fromUtf8(key);
        if (!chordIdText || !value.is_array()) {
            continue;  // tolerate a stray malformed entry rather than discarding the whole file
        }
        std::vector<std::u16string> chords;
        for (const auto& chordValue : value) {
            if (!chordValue.is_string()) {
                continue;
            }
            if (auto chordText = fromUtf8(chordValue.get<std::string>())) {
                chords.push_back(std::move(*chordText));
            }
        }
        bindings.m_bindings[std::move(*chordIdText)] = std::move(chords);
    }

    return bindings;
}

void KeyBindings::saveTo(const std::filesystem::path& path) const {
    nlohmann::json bindingsJson = nlohmann::json::object();
    for (const auto& [chordId, chords] : m_bindings) {
        std::vector<std::string> utf8Chords;
        utf8Chords.reserve(chords.size());
        for (const auto& chord : chords) {
            utf8Chords.push_back(toUtf8(chord));
        }
        bindingsJson[toUtf8(chordId)] = utf8Chords;
    }

    nlohmann::json j;
    j["version"]  = kFormatVersion;
    j["preset"]   = toUtf8(presetName);
    j["bindings"] = bindingsJson;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << j.dump();
}

}  // namespace neomifes::core
