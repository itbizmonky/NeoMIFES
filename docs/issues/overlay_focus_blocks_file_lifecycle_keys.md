# Issue: オーバーレイパネルにフォーカスがある間 Ctrl+S/O/N が届かない (P2 — 既知の限界)

- **起票日:** 2026-08-04 (WI-02 実装、設計レビュー Finding 4)
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

## 対応方針 (未着手)

埋め込みバー4種のサブクラスプロシージャに「自分が認識しないキーは親 HWND へ転送する」ロジックを追加する必要がある。具体的には、各サブクラスプロシージャの `default:` 分岐で `ctrlDown && (vkCode == 'S' || vkCode == 'O' || vkCode == 'N')` を検出したら `SendMessageW`/`PostMessageW` で親の `WM_KEYDOWN` 相当を転送するか、`MainWindowConfig` に「オーバーレイからのキー転送専用フック」を新設する設計が考えられる。独立ダイアログ2種(`FindDialog`/`FindReplaceDialog`)は機構が異なるため(2026-09-04追記参照)、同じ転送ロジックではなく別の対策(例: 各ダイアログの`wndProc`自身が`TranslateAcceleratorW`相当のチェックを行う、または該当キーだけ`MainWindow`へ明示転送する)が必要になる見込み。いずれも複数ウィジェット × 個別ロジックという footprint の大きな変更になるため、WI-02 単独では対応しなかった。

## 完了条件

- [ ] いずれかのオーバーレイにフォーカスがある状態でも Ctrl+S/Ctrl+Shift+S/Ctrl+O/Ctrl+N が機能する
- [ ] 6 ウィジェット全てで同じ転送ロジック(または同等の対策)が一貫して適用されている

## 再検証コマンド

```bash
grep -rn "DefSubclassProc" src/ui/src/*.cpp
```
