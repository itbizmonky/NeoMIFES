#include "neomifes/app/status_bar_format.h"

namespace neomifes::app {

std::wstring formatStatusBarPosition(document::LineNumber line, std::uint32_t column) {
    return std::to_wstring(line + 1) + L":" + std::to_wstring(column + 1);
}

std::wstring formatStatusBarSelectionCount(std::uint64_t selectedLength) {
    if (selectedLength == 0) {
        return L"";
    }
    return std::to_wstring(selectedLength) + L" selected";
}

std::wstring formatStatusBarEncoding(encoding::Encoding encoding) {
    switch (encoding) {
        case encoding::Encoding::Utf8:       return L"UTF-8";
        case encoding::Encoding::Utf8Bom:    return L"UTF-8 BOM";
        case encoding::Encoding::Utf16Le:    return L"UTF-16 LE";
        case encoding::Encoding::Utf16LeBom: return L"UTF-16 LE BOM";
        case encoding::Encoding::Utf16Be:    return L"UTF-16 BE";
        case encoding::Encoding::Utf16BeBom: return L"UTF-16 BE BOM";
        case encoding::Encoding::Utf32Le:    return L"UTF-32 LE";
        case encoding::Encoding::Utf32LeBom: return L"UTF-32 LE BOM";
        case encoding::Encoding::Utf32Be:    return L"UTF-32 BE";
        case encoding::Encoding::Utf32BeBom: return L"UTF-32 BE BOM";
        case encoding::Encoding::ShiftJis:   return L"Shift_JIS";
        case encoding::Encoding::EucJp:      return L"EUC-JP";
        case encoding::Encoding::Iso2022Jp:  return L"ISO-2022-JP";
    }
    return L"";  // unreachable - every enumerator handled above
}

std::wstring formatStatusBarLineEnding(encoding::LineEnding lineEnding) {
    switch (lineEnding) {
        case encoding::LineEnding::Crlf:  return L"CRLF";
        case encoding::LineEnding::Lf:    return L"LF";
        case encoding::LineEnding::Cr:    return L"CR";
        case encoding::LineEnding::Mixed: return L"Mixed";
    }
    return L"";  // unreachable - every enumerator handled above
}

std::wstring formatStatusBarOverwriteMode(bool overwriteMode) {
    return overwriteMode ? L"OVR" : L"INS";
}

std::wstring formatStatusBarLanguage(std::optional<syntax::Language> language) {
    if (!language) {
        return L"";
    }
    switch (*language) {
        case syntax::Language::Cpp:        return L"C++";
        case syntax::Language::Python:     return L"Python";
        case syntax::Language::C:          return L"C";
        case syntax::Language::JavaScript: return L"JavaScript";
        case syntax::Language::Java:       return L"Java";
        case syntax::Language::Go:         return L"Go";
        case syntax::Language::Rust:       return L"Rust";
        case syntax::Language::Json:       return L"JSON";
        case syntax::Language::Html:       return L"HTML";
        case syntax::Language::Css:        return L"CSS";
        case syntax::Language::Shell:      return L"Shell";
        case syntax::Language::Yaml:       return L"YAML";
        case syntax::Language::Toml:       return L"TOML";
        case syntax::Language::Xml:        return L"XML";
        case syntax::Language::TypeScript: return L"TypeScript";
        case syntax::Language::Tsx:        return L"TSX";
        case syntax::Language::Php:        return L"PHP";
        case syntax::Language::Markdown:   return L"Markdown";
        case syntax::Language::PowerShell: return L"PowerShell";
        case syntax::Language::Ini:        return L"INI";
        case syntax::Language::Batch:      return L"Batch";
        case syntax::Language::Sql:        return L"SQL";
    }
    return L"";  // unreachable - every enumerator handled above
}

}  // namespace neomifes::app
