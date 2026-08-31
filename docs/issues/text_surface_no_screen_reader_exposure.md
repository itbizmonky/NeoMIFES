# Issue: 主要テキスト編集領域がUI Automation/スクリーンリーダーへ内容を一切公開していない (P1 — 未対応)

- **起票日:** 2026-08-31 (v1出荷判定、「基本アクセシビリティ」項目の実機検証中に発見)
- **対象:** `src/render/`(Direct2D直接描画によるテキストサーフェース)、`src/ui/src/main_window.cpp`
- **優先度:** P1 (専門認証は求めないが「基本疎通確認」自体が満たせていない)
- **対応 Phase:** 未定 (UI Automation TextPattern実装は本質的に大きな新規サブシステム)

## 事実

v1出荷判定チェックリストの「基本アクセシビリティ(キーボード完結ナビゲーション・高コントラストモード動作・スクリーンリーダーでの基本疎通確認)」を検証するため、`System.Windows.Automation`(UI Automation)でNeoMIFESの実行中プロセスをクエリしたところ、以下が判明した:

- **メニューバー・ステータスバー・タブ・ダイアログは正しく公開されている。** メニュー項目(「ファイル(F)」「編集(E)」等)、ステータスバーの各パート(カーソル位置「1:1」、エンコーディング「UTF-8」、改行コード「CRLF」等)、タブ項目(「Untitled 1」)、クラッシュ復旧ダイアログ(TaskDialogIndirect、ボタンの`InvokePattern`経由でのプログラム的クリックも実際に成功した)は、いずれも`Name`プロパティに意味のある文字列を持ち、スクリーンリーダーが読み上げ可能な状態にある。
- **主要なテキスト編集領域(Direct2Dで直接描画される本文表示部分)は`ControlType.Custom`、`Name=''`(空文字列)として公開されている。** ドキュメントの実際のテキスト内容・カーソル位置・選択範囲のいずれもUI Automationツリーから一切取得できない。

## 影響

- **スクリーンリーダーユーザーは、メニュー操作やステータス確認はできても、肝心の「ファイルの中身を読む」ことが一切できない。** これはテキストエディタとして致命的なギャップである。
- ただし、これは**この種のカスタム描画(GPU/Direct2D直接描画)テキストエディタに共通する既知の課題**であり(VS Code・Sublime Text等の高性能エディタも、同様の理由でOS標準コントロール(`EDIT`/`RichEdit`)を使うメモ帳などに比べアクセシビリティ実装が本質的に困難)、NeoMIFES固有の見落としというより設計上のトレードオフの帰結である。

## 推定原因

Direct2D/DirectWriteによる直接描画は、OS標準のWin32コントロール(`EDIT`/`RichEdit`)と異なり、UI Automationへの情報公開を自動的に提供しない。UI Automation TextPattern(`ITextProvider`/`ITextRangeProvider`)を独自に実装し、`RenderPipeline`が保持するテキスト内容・カーソル位置・選択範囲をこのインターフェース経由で公開する必要がある。

## 対応方針 (未着手)

1. **UI Automation TextPattern (`ITextProvider`) を実装する。** `document::Document`/`RenderPipeline`の既存状態を、`ITextRangeProvider`が要求する形式(文字単位の矩形取得・テキスト抽出・選択範囲同期等)へブリッジする独自の`IRawElementProviderSimple`実装が必要。実装規模は大きく、専用のサブフェーズとして計画すべき。
2. **最小限、カーソル移動時に現在行の内容を`NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, ...)`等でアナウンスする簡易実装に留める。** フルのTextPatternより実装コストは低いが、Narrator等での体験は限定的(行単位の読み上げのみ、範囲選択・文字単位ナビゲーション等はカバーしない)。
3. **何もしない(現状維持)。** v1出荷判定の「専門認証は求めない」方針を踏まえ、この項目は「未達」として正直に記録し、将来の商用配布検討時(§12.3フル版のNVDA/JAWS専門認証)に本格対応する。

現時点では対応方針を確定せず、ユーザーへ選択肢を提示した上で次フェーズ候補として検討する。

## 完了条件

- [ ] 上記3方針のいずれかを採用するかを決定する(ユーザー確認)
- [ ] (方針1採用の場合) `ITextProvider`実装後、Narratorで実際に文書内容が読み上げられることを実機確認する
- [ ] (方針2採用の場合) 簡易アナウンス実装後、Narratorでカーソル移動時に行内容が読み上げられることを実機確認する

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
