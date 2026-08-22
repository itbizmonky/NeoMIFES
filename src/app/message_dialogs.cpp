#include "neomifes/app/message_dialogs.h"

#include <commctrl.h>

#include <array>
#include <string>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

namespace {

// Not a real Win32 command ID (those are reserved for IDOK/IDCANCEL/etc.
// ranges) - just needs to be distinct from IDCANCEL (2, the value
// TDCBF_CANCEL_BUTTON produces) and from each other.
constexpr int kSaveButtonId     = 100;
constexpr int kDontSaveButtonId = 101;
constexpr int kRestoreButtonId  = 102;
constexpr int kDiscardButtonId  = 103;

// Shared boilerplate every dialog in this file needs (cbSize/hwndParent/
// pszWindowTitle) - "NeoMIFES" is a literal here rather than
// kWindowClassName (neomifes::ui) since a dialog title is user-facing
// product naming, not the internal window-class identifier.
[[nodiscard]] TASKDIALOGCONFIG makeBaseConfig(HWND owner) noexcept {
    TASKDIALOGCONFIG config{};
    config.cbSize        = sizeof(config);
    config.hwndParent    = owner;
    config.pszWindowTitle = L"NeoMIFES";
    return config;
}

}  // namespace

UnsavedChangesChoice showUnsavedChangesDialog(HWND owner, std::wstring_view documentName) {
    const std::wstring content =
        std::wstring(documentName) + L" に対する変更を保存しますか?";

    const std::array<TASKDIALOG_BUTTON, 2> buttons{{
        {.nButtonID = kSaveButtonId, .pszButtonText = L"保存する(&S)"},
        {.nButtonID = kDontSaveButtonId, .pszButtonText = L"保存しない(&N)"},
    }};

    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // TASKDIALOGCONFIG::pszMainIcon is a union member (shared with hMainIcon)
    // - this is CommCtrl.h's own C ABI, not a design choice made here (same
    // rationale as outline_pane.cpp's TVINSERTSTRUCTW::item union access).
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_WARNING_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction   = L"保存されていない変更があります";
    config.pszContent           = content.c_str();
    config.dwCommonButtons      = TDCBF_CANCEL_BUTTON;
    config.pButtons             = buttons.data();
    config.cButtons             = static_cast<UINT>(buttons.size());
    config.nDefaultButton       = kSaveButtonId;

    int        pressedButtonId = 0;
    const HRESULT hr = ::TaskDialogIndirect(&config, &pressedButtonId, nullptr, nullptr);
    if (FAILED(hr)) {
        // Fail safe - a dialog malfunction must never be interpreted as
        // permission to discard unsaved work or silently overwrite a file.
        return UnsavedChangesChoice::Cancel;
    }
    if (pressedButtonId == kSaveButtonId) {
        return UnsavedChangesChoice::Save;
    }
    if (pressedButtonId == kDontSaveButtonId) {
        return UnsavedChangesChoice::DontSave;
    }
    return UnsavedChangesChoice::Cancel;  // IDCANCEL, Escape, Alt+F4, or the title bar's close button
}

void showSaveErrorDialog(HWND owner, document::SaveError error) {
    // SaveError's 5 enumerators are all handled below (exhaustive switch, no
    // default) - the trailing fallback return is unreachable via the public
    // API but kept as a defensive guard against a future enumerator added
    // here without a matching case.
    const wchar_t* detail = [error]() noexcept -> const wchar_t* {
        switch (error) {
            case document::SaveError::CannotCreateTempFile:
                return L"一時ファイルを作成できませんでした。書き込み権限を確認してください。";
            case document::SaveError::WriteFailed:
                return L"ファイルへの書き込みに失敗しました。ディスクの空き容量を確認してください。";
            case document::SaveError::EncodeFailed:
                return L"選択した文字コードへ変換できない文字が含まれています。";
            case document::SaveError::ReplaceFailed:
                return L"ファイルの置き換えに失敗しました。元のファイルは変更されていません。";
            case document::SaveError::OriginalFileAtRisk:
                return L"保存に失敗し、元のファイルの状態が不明です。"
                       L"同じフォルダの \"<ファイル名>.neomifes-bak\" を手動で確認してください。";
        }
        return L"不明なエラーが発生しました。";
    }();

    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // See showUnsavedChangesDialog()'s comment on pszMainIcon's union access.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_ERROR_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"保存に失敗しました";
    config.pszContent         = detail;
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showOpenErrorDialog(HWND owner, document::LoadError error) {
    const wchar_t* detail = L"不明なエラーが発生しました。";
    switch (error) {
        case document::LoadError::NotFound:
            detail = L"ファイルが見つかりませんでした。";
            break;
        case document::LoadError::PermissionDenied:
            detail = L"ファイルを開く権限がありません。";
            break;
        case document::LoadError::IoFailure:
            detail = L"ファイルの読み込み中にエラーが発生しました。";
            break;
        case document::LoadError::InvalidUtf8:
            detail = L"有効なUTF-8として読み込めませんでした。";
            break;
        case document::LoadError::TooLarge:
            detail = L"ファイルサイズが上限を超えています。";
            break;
        case document::LoadError::InvalidEncoding:
            detail = L"検出された文字コードとして読み込めませんでした。";
            break;
        case document::LoadError::Unknown:
            break;
    }
    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // See showUnsavedChangesDialog()'s comment on pszMainIcon's union access.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_ERROR_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"ファイルを開けませんでした";
    config.pszContent         = detail;
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

bool showCrashRecoveryDialog(HWND owner, std::wstring_view fileName) {
    const std::wstring content =
        std::wstring(fileName) + L" に保存されていない変更が見つかりました。復元しますか?";

    const std::array<TASKDIALOG_BUTTON, 2> buttons{{
        {.nButtonID = kRestoreButtonId, .pszButtonText = L"復元する(&R)"},
        {.nButtonID = kDiscardButtonId, .pszButtonText = L"復元しない(&D)"},
    }};

    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // See showUnsavedChangesDialog()'s comment on pszMainIcon's union access.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_WARNING_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"未保存の変更を復元しますか?";
    config.pszContent         = content.c_str();
    config.pButtons           = buttons.data();
    config.cButtons           = static_cast<UINT>(buttons.size());
    config.nDefaultButton     = kRestoreButtonId;

    int           pressedButtonId = 0;
    const HRESULT hr = ::TaskDialogIndirect(&config, &pressedButtonId, nullptr, nullptr);
    if (FAILED(hr)) {
        // Fail safe - see this function's own header comment for why the
        // safe default here is the OPPOSITE direction from
        // showUnsavedChangesDialog()'s.
        return false;
    }
    return pressedButtonId == kRestoreButtonId;
}

void showLogFormatNotDetectedDialog(HWND owner) {
    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // See showUnsavedChangesDialog()'s comment on pszMainIcon's union access.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_INFORMATION_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"ログ形式を判定できませんでした";
    config.pszContent =
        L"組込のログ形式(Syslog RFC 5424/3164、Apache/Nginx Common・Combined Log Format、"
        L"汎用 ISO-8601 + レベル行)のいずれにも十分な確信度で一致しませんでした。"
        L"コマンドパレットから特定の形式を直接選んで有効化することもできます。";
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showJsonFormatInvalidDialog(HWND owner) {
    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_ERROR_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"整形できません";
    config.pszContent         = L"現在のドキュメントは有効なJSONではないため整形できません。";
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showJsonValidDialog(HWND owner) {
    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_INFORMATION_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"有効なJSONです";
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showJsonValidationErrorDialog(HWND owner, std::u16string_view message) {
    const std::wstring content(neomifes::util::toWstringView(message));

    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_ERROR_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"JSONの構文エラー";
    config.pszContent         = content.c_str();
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showJsonPathInvalidJsonDialog(HWND owner) {
    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_ERROR_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"JSONPathを評価できません";
    config.pszContent         = L"現在のドキュメントは有効なJSONではないためJSONPathを評価できません。";
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showJsonPathSyntaxErrorDialog(HWND owner, std::u16string_view expression) {
    const std::wstring content(neomifes::util::toWstringView(expression));

    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_ERROR_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"JSONPathの構文エラー";
    config.pszContent         = content.c_str();
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showJsonPathNoMatchDialog(HWND owner) {
    TASKDIALOGCONFIG config = makeBaseConfig(owner);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    config.pszMainIcon = TD_INFORMATION_ICON;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    config.pszMainInstruction = L"一致するノードが見つかりませんでした";
    config.dwCommonButtons    = TDCBF_OK_BUTTON;
    ::TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

}  // namespace neomifes::app
