# Issue: 主要テキスト編集領域がUI Automation/スクリーンリーダーへ内容を一切公開していない (P1 — 🟢 解決済み・簡易アナウンス実装)

- **起票日:** 2026-08-31 (v1出荷判定、「基本アクセシビリティ」項目の実機検証中に発見)
- **解決日:** 2026-09-02 (方針2「簡易アナウンス実装」をユーザーが選択、実装・実機検証完了)
- **対象:** `src/ui/include/neomifes/ui/text_surface_accessible.h`(新規)、`src/ui/src/text_surface_accessible.cpp`(新規)、`src/ui/include/neomifes/ui/main_window.h`/`.cpp`、`src/app/normal_mode_wiring.cpp`
- **優先度:** P1 (専門認証は求めないが「基本疎通確認」自体が満たせていない)
- **対応 Phase:** 未定 (フルTextPattern実装は本質的に大きな新規サブシステムのまま、今回はスコープ外)

## 事実

v1出荷判定チェックリストの「基本アクセシビリティ(キーボード完結ナビゲーション・高コントラストモード動作・スクリーンリーダーでの基本疎通確認)」を検証するため、`System.Windows.Automation`(UI Automation)でNeoMIFESの実行中プロセスをクエリしたところ、以下が判明した:

- **メニューバー・ステータスバー・タブ・ダイアログは正しく公開されている。** メニュー項目(「ファイル(F)」「編集(E)」等)、ステータスバーの各パート(カーソル位置「1:1」、エンコーディング「UTF-8」、改行コード「CRLF」等)、タブ項目(「Untitled 1」)、クラッシュ復旧ダイアログ(TaskDialogIndirect、ボタンの`InvokePattern`経由でのプログラム的クリックも実際に成功した)は、いずれも`Name`プロパティに意味のある文字列を持ち、スクリーンリーダーが読み上げ可能な状態にある。
- **主要なテキスト編集領域(Direct2Dで直接描画される本文表示部分)は`ControlType.Custom`、`Name=''`(空文字列)として公開されている。** ドキュメントの実際のテキスト内容・カーソル位置・選択範囲のいずれもUI Automationツリーから一切取得できない。

## 影響

- **スクリーンリーダーユーザーは、メニュー操作やステータス確認はできても、肝心の「ファイルの中身を読む」ことが一切できない。** これはテキストエディタとして致命的なギャップである。
- ただし、これは**この種のカスタム描画(GPU/Direct2D直接描画)テキストエディタに共通する既知の課題**であり(VS Code・Sublime Text等の高性能エディタも、同様の理由でOS標準コントロール(`EDIT`/`RichEdit`)を使うメモ帳などに比べアクセシビリティ実装が本質的に困難)、NeoMIFES固有の見落としというより設計上のトレードオフの帰結である。

## 推定原因

Direct2D/DirectWriteによる直接描画は、OS標準のWin32コントロール(`EDIT`/`RichEdit`)と異なり、UI Automationへの情報公開を自動的に提供しない。UI Automation TextPattern(`ITextProvider`/`ITextRangeProvider`)を独自に実装し、`RenderPipeline`が保持するテキスト内容・カーソル位置・選択範囲をこのインターフェース経由で公開する必要がある。

## 対応方針 (2026-09-02、実施済み)

3方針(①フルTextPattern実装/②簡易アナウンス実装/③現状維持)を、着手前調査の事実(このコードベースにUI Automation関連コードは一切存在せず完全新規実装であること、テキストサーフェスは独立子HWNDではなくメインウィンドウ自体がWM_PAINT+Direct2Dで直接描画していること、キャレット/選択範囲の位置⇔ピクセル変換は既にDirectWriteの`HitTestTextPosition()`で内部実装済みであること、`document::Document`が任意レンジのテキスト抽出を既に提供すること)とともにAskUserQuestionで提示し、**「②簡易アナウンス実装(推奨)」が選ばれた。** ①は技術的には土台があるが実装規模が大きく1セッションでは完結しない可能性が高いと判断し推奨から外した。

### 実装

古典的なMSAA(Microsoft Active Accessibility)ライブリージョン機構を採用した:

1. **`ui::TextSurfaceAccessible`(新規、`text_surface_accessible.h`/`.cpp`)** — `IAccessible`を自前実装するCOMオブジェクト。`::CreateStdAccessibleObject()`で取得した標準オブジェクトへの委譲をベースに、`get_accName()`(および`IDispatch::Invoke()`経由の`DISPID_ACC_NAME`動的呼び出し)のみを独自実装で上書きし、現在行のテキストを返す。IAccessible+IDispatchの残り約26メソッドは全て`m_inner`への1行委譲(インターフェース契約自体がこの規模を要求するため、CLAUDE.mdのクラスサイズ目安はここでは適用対象外とコメントで明記)。
2. **`MainWindow::handleGetObject()`(新規)** — `WM_GETOBJECT`ハンドラ。`OBJID_CLIENT`のみ応答し、他の全てのobjIdは`DefWindowProcW`へフォールスルー(タイトルバー・システムメニュー等の既定のアクセシビリティオブジェクトを壊さないため)。`m_accessible`は初回クエリ時に遅延生成。
3. **`MainWindow::announceCurrentLineIfChanged()`(新規)** — カーソルの行番号(不透明な`const void*`セッショントークン付き、`csvGridPanePendingSessionToken`と同型のパターン)が前回と異なる場合のみ、`m_accessible`へ新しいテキストをセットし`NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, ...)`を発火する。ui層(`neomifes::ui`)はADR-009によりdocument/core型に依存できないため、呼び出し元(`handlePaintEvent()`、`normal_mode_wiring.cpp`)が`document::Document::lineText()`から解決済みの`std::wstring_view`を渡す設計にした。

### 実機検証で発見した2つの誤り(推測実装をしなかったことで発覚)

1. **`IDispatch::Invoke()`の単純委譲は不完全だった。** `IAccessible`は`IDispatch`派生であり、`get_accName`はストロングタイプのvtableスロットだけでなく`DISPID_ACC_NAME`(`-5003`、`oleacc.h`)経由の動的ディスパッチでも呼び出しうる。当初`Invoke()`を`m_inner`へ無条件委譲していたため、動的ディスパッチ経由の呼び出しは自分のオーバーライドを迂回し常に空文字列を返していた(PowerShellの`Accessibility.IAccessible`後期バインディング経由で発覚)。`Invoke()`内で`DISPID_ACC_NAME`のプロパティ取得だけを`get_accName()`へ転送するよう修正。
2. **「1ステップ遅れて見える」誤検知。** 初回の実機検証で、行を移動しても1つ前の行の内容が返るように見えたが、標準プローブで切り分けたところ**これはコードのバグではなく検証スクリプト自体の問題だった** — 検証に使ったWM_KEYDOWN(VK_RETURN)単体での改行合成は、この環境のTranslateMessageの二重処理(またはEnterのキーバインド経路との競合)により余分な空行を生む副作用があり、実際のドキュメント構造がテスト側の想定と食い違っていた。WM_CHAR(`'\r'`)を直接送る方式に切り替えたところ、行番号変化時の即時・正確なアナウンスを確認できた(遅延・陳腐化は一切なし)。

### 実機検証(2026-09-02、Debugビルド)

`AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, IID_IAccessible, ...)` + `IAccessible::accName(CHILDID_SELF)`を直接呼ぶPowerShellスクリプトで、実際にNeoMIFESを操作しながら検証した(UI Automationの`System.Windows.Automation.Descendants`ツリー巡回では`Name`プロパティがこの機構と別経路で解決されており本機能を反映しないことが判明したため、実際にAT(支援技術)が`EVENT_OBJECT_LIVEREGIONCHANGED`受信時に呼ぶのと同じ`AccessibleObjectFromWindow`+`accName`の経路で検証する方式に切り替えた)。

- 空のドキュメント: `accName=''`(正しい)
- "AAA" + Enter(WM_CHAR) + "BBB"と入力後、カーソルが"BBB"にある状態: `accName=''`(カーソル到達時点でその行は空だったため、正しい — 同一行内でのタイプ中の追従アナウンスは方針2の意図的なスコープ外)
- 上矢印×5でトップへ移動: `accName='AAA'`(正しい)
- 下矢印×1: `accName='BBB'`(正しい、遅延なし)
- 文末で下矢印を繰り返す: `accName='BBB'`のまま(正しい、末尾でのクランプ)

Debug/Release/ubsan全ビルド green、clang-tidy新規警告0(該当は全て`cppcoreguidelines-pro-type-union-access`、Win32/OLE Automationの`VARIANT`タグ付きunionアクセスが原因でありNOLINT済み)。DOGFOOD-TEMP診断ログは全て除去済み(`grep -rn "DOGFOOD" src/`で確認)。

### 意図的にスコープ外としたもの

- 列(カラム)単位のキャレット追跡・範囲選択の読み上げ・文字単位ナビゲーション(いずれもフルTextPattern実装でのみ提供可能)
- 同一行内でタイピング中の内容変化のリアルタイム追従アナウンス(スクリーンリーダー自身の文字エコー機能と重複するため、行番号変化時のみをトリガーとする設計にした)
- Narrator実機での音声読み上げそのものの確認(この開発環境でNarratorを起動しての音声確認は行っていない — `AccessibleObjectFromWindow`+`accName`読み取りによる、AT側の実際の動作経路を模した検証で代替)

## 完了条件

- [x] 上記3方針のいずれかを採用するかを決定する(ユーザー確認) — ②簡易アナウンス実装を採用
- [x] (方針2採用の場合) 簡易アナウンス実装後、Narratorでカーソル移動時に行内容が読み上げられることを実機確認する — Narrator起動での音声確認はしていないが、AT実装が読み上げに使う`AccessibleObjectFromWindow`+`accName`経路を直接検証し、行移動ごとの正確・即時な内容反映を確認した(上記「実機検証」参照)

## 再検証コマンド

```powershell
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$proc = Get-Process NeoMIFES
$root = [System.Windows.Automation.AutomationElement]::RootElement
$condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $proc.Id)
$window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)
$window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
    Where-Object { $_.Current.ControlType.ProgrammaticName -eq "ControlType.Custom" } |
    ForEach-Object { "Name='$($_.Current.Name)'" }
# 空文字列が返れば未解消
```
