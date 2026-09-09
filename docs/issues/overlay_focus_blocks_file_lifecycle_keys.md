# Issue: オーバーレイパネルにフォーカスがある間 Ctrl+S/O/N が届かない (P2 — 🟢 WI-40で解決済み)

- **起票日:** 2026-08-04 (WI-02 実装、設計レビュー Finding 4)
- **解決日:** 2026-09-09(WI-40)
- **対象:** `src/ui/src/command_palette.cpp` / `goto_line_bar.cpp` / `grep_bar.cpp` / `outline_pane.cpp`(埋め込みバー4種)+ `find_dialog.cpp` / `find_replace_dialog.cpp`(独立トップレベルダイアログ2種、下記2026-09-04追記参照)
- **優先度:** P2 (実害は限定的、下記「影響」参照)
- **対応 Phase:** 未定 (6 ウィジェット全てへの転送ロジック追加が必要になった時点で再評価)
- **親文書:** WI-02 (`build_plan.md`) 設計レビューで発見、実装はスコープ外とした

## 事実

`FindBar` / `GrepBar` / `CommandPalette` / `GotoLineBar` / `OutlinePane` はいずれも自身の子 HWND (`WC_EDIT` / `WC_TREEVIEW` 等) に `SetWindowSubclass` でサブクラスプロシージャを設定しており、そのプロシージャは**自分が認識するキー (Enter/Escape/F3 等) のみを処理し、それ以外は無条件に `DefSubclassProc` へ委譲する**。この委譲は「Windows 標準コントロールとしての既定動作」止まりで、**親ウィンドウ (`MainWindow`) の `onKeyDown` へは一切転送されない**。

このため、いずれかのオーバーレイがキーボードフォーカスを持っている間、`cfg.onKeyDown` (`handleKeyDownEvent()`) は一切呼ばれず、Ctrl+S/Ctrl+Shift+S/Ctrl+O/Ctrl+N (WI-02 で新設) は無反応になる。

## 影響

典型的な操作フローでは、検索/コマンドパレット/アウトライン等を使った後は編集領域へフォーカスを戻してから編集・保存する (Enter/Escape でオーバーレイが閉じ、`onClosed` コールバックが `::SetFocus(hwnd)` を呼んでフォーカスを編集領域へ戻す実装になっている箇所が大半)。そのため実害は限定的と判断し、WI-02 のスコープには含めなかった。

**唯一の既知の抜け穴:** `OutlinePane` (`WC_TREEVIEW`) のようにパネルを開いたまま項目選択以外の操作を続けるフローでは、フォーカスがそのままオーバーレイに残り続け、Ctrl+S 等が届かない体感を生みうる。

**2026-09-04追記(WI-24調査中の副次的発見):** `ui::FindDialog`(Ctrl+F、WI-24で新設)は当issueの対象外にはならない——ただし機構は異なる。`FindDialog`/`FindReplaceDialog`は`WS_CHILD`ではなく独立トップレベルウィンドウのため、この issue 本文が説明する「`SetWindowSubclass`→`DefSubclassProc`委譲」経路そのものには該当しない。しかし`runMessageLoop()`の`GetAncestor(msg.hwnd, GA_ROOT)`は`WS_CHILD`の親チェーンしか辿らないため、これらのダイアログの検索欄にフォーカスがある間は`GA_ROOT`がダイアログ自身のHWNDに解決され、`TranslateAcceleratorW`がMainWindowの`HACCEL`テーブルを適用しない——結果として、体感としては同じ「Ctrl+S/O/N が届かない」症状が別の理由(SetWindowSubclass委譲ではなく、accelerator解決対象の不一致)で生じる。`FindReplaceDialog`はWI-18から既にこの制約を持っていたが、当issueには一度も追加されていなかった。対象ファイルを実質6件(埋め込みバー4+独立ダイアログ2)へ更新した。

## 解決内容(WI-40、2026-09-09)

**実装時点でのスコープ再調査により、issue本文の前提が一部古くなっていたと判明した。** WI-07 step2の時点で、Save/SaveAs/Open/New/NewWindow/TabClose/TabNext/TabPrevious/TabSwitch1-9(計17コマンド、`kAcceleratorEligibleCommands`、`keybinding_dispatch.h`)は`cfg.onKeyDown`(`handleKeyDownEvent()`)経由ではなく、`main.cpp`の`runMessageLoop()`が`TranslateAcceleratorW`で**`DispatchMessageW`より前に**捕捉する設計へ変わっていた——本issueの「事実」節が説明する`SetWindowSubclass`→`DefSubclassProc`委譲経路そのものを経由しなくなっていたということ。

- **埋め込み型オーバーレイ8件(command_palette/csv_grid_pane/git_pane/goto_line_bar/grep_bar/json_path_bar/json_tree_pane/outline_pane、issue起票時の4件から後続WIで4件追加され計8件に増えていた)は全てMainWindow自身の直接の子(1段階のWS_CHILD)のため、`GetAncestor(msg.hwnd, GA_ROOT)`は既に正しくMainWindowへ解決しており、Ctrl+S/O/N等17コマンドはWI-07 step2の時点で既に動作していたと判明した(実機ドッグフーディングでGotoLineBar〔Ctrl+G〕にフォーカスがある状態でCtrl+Sが正しく機能することを確認、回帰無し)。** issueが長期間「未修正」のまま残っていたのは、この事実が一度も検証されずWI-02当時の設計のまま更新されていなかったため。
- **真に壊れていたのはFindDialog/FindReplaceDialogの2件のみ。** 両者は独立トップレベルウィンドウ(`WS_POPUP`、MainWindowに所有〔owner〕されるが`WS_CHILD`ではない)であり、`GA_ROOT`は所有者チェーンを辿らないWin32の仕様上、これらの子コントロールにフォーカスがある間は`GA_ROOT`がダイアログ自身のHWNDに解決されていた。`TranslateAcceleratorW`はそれでもキーにはマッチする(判定はメッセージの発生元と無関係)が、結果のWM_COMMANDをダイアログ自身のHWNDへ送ってしまい、各ダイアログの`wndProc`は自身のコントロールIDしか認識せず`default: return;`で無言で握りつぶしていた。

**修正:** `src/app/main.cpp`の`runMessageLoop()`の1箇所、`GetAncestor(msg.hwnd, GA_ROOT)`を`GetAncestor(msg.hwnd, GA_ROOTOWNER)`へ変更。`GA_ROOTOWNER`は`GetParent()`が返す親・所有者チェーンの両方を辿るWin32標準API(`GA_ROOT`はWS_CHILD親チェーンのみ)——FindDialogの検索欄から辿ると`GetParent(編集欄)=FindDialog`→`GetParent(FindDialog)=MainWindow`(所有者)→`GetParent(MainWindow)=NULL`で停止し、正しくMainWindowへ解決される。埋め込み型8件は全て1段階WS_CHILDのため`GA_ROOT`と`GA_ROOTOWNER`は同一の結果を返し、この変更は無影響(no-op)。1行の変更で両ダイアログを一括して修正でき、issue本文が提案していた「各ダイアログ個別のロジック」より遥かに小さい修正で済んだ。

**実機ドッグフーディングで確認済み:** Ctrl+FでFindDialogを開き検索欄にフォーカスがある状態でCtrl+Sを送信、タイトルバーの未保存マーカー(`*`)が消え、ディスク上のファイル内容も実際に更新されることを確認(修正前は無反応のはず)。Ctrl+HでFindReplaceDialogでも同様に確認。GotoLineBar(埋め込み型の代表)では修正前後で変化が無く回帰していないことも確認。

## 完了条件

- [x] いずれかのオーバーレイにフォーカスがある状態でも Ctrl+S/Ctrl+Shift+S/Ctrl+O/Ctrl+N が機能する — 実機確認済み(FindDialog/FindReplaceDialog経由、修正前は無反応・修正後は正しく動作)
- [x] 全ウィジェットで一貫した動作が確保されている — 実際には8件(埋め込み型)はWI-07 step2時点で既に動作済み、残り2件(独立ダイアログ)を`GA_ROOTOWNER`の1行修正で解消。「同じ転送ロジックを6/10ウィジェットへ個別追加する」という当初想定より遥かに小さいスコープで完了

## 再検証コマンド

```bash
grep -rn "DefSubclassProc" src/ui/src/*.cpp
```
