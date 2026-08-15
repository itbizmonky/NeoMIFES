# Issue: メニューバーのキーバインド表示 (`\tCtrl+X`) が実行時のリマップに追従しない (P2 — 既知の限界)

- **起票日:** 2026-08-15 (WI-10 実装、設計フェーズで既知のスコープ外として確定)
- **対象:** `src/app/menu_bar.cpp` (`buildMenuBar()`)
- **優先度:** P2 (実害はメニュー上の表示のみ、実際のキー入力自体は正しく機能する)
- **対応 Phase:** 未定 (メニュー再構築機構が必要になった時点で再評価)
- **親文書:** WI-10 (`build_plan.md` §5) 設計フェーズでスコープ外と確定

## 事実

`menu_bar.h`/`menu_bar.cpp` の `MenuItemSpec::label` はウィンドウ作成時 (`CreateWindowExW` の `hMenu`) に固定文字列として `AppendMenuW` へ焼き込まれる。`\tCtrl+S` のようなキーバインド表示部分も含め、以後この文字列を書き換える手段 (`SetMenu`/`ModifyMenuW`/`DrawMenuBar` によるラベル更新) はコードベース全体に1つも存在しない (WI-07 実装時に確認済み)。

WI-10 で `keybindings.json` のリロードやプリセット切替 (`keybindings.reload`/`keybindings.preset.*` パレットコマンド) によって実行時にキーバインドを変更できるようになったが、メニューバー上の表示はこの変更を反映しない。

## 影響

- **実際のキー入力自体は正しく機能する。** `TranslateAcceleratorW` (HACCEL対象16コマンド) または `normal_mode_wiring.cpp` の手動チェーン (`chordMatches()`、残り18コマンド) いずれも `core::KeyBindings` を直接参照するため、キーバインドを変更した瞬間から (HACCEL側は `accelTable` 再構築後、手動チェーン側は即座に) 正しいチョードで動作する。
- 影響はメニュー上の `\tCtrl+X` のような表示のみで、再起動するまで変更前のプリセット (通常は起動時のneomifes標準) のラベルが残り続ける。ユーザーがメニューを見てキー割り当てを誤認する可能性がある。
- コマンドパレット側は影響を受けない — `keybindingLabel` は `keybindingLabelFor()` により `keybindings.reload`/`keybindings.preset.*` 実行のたびに `commandPalette.setCommands(buildCommandRegistry(...))` 経由で動的に再構築される (WI-10 で新設)。

## 対応方針 (未着手)

メニューバーを実行時に再構築可能にするには、`DestroyMenu`+`buildMenuBar()`再呼び出し+`SetMenu`+`DrawMenuBar` のいずれかの組み合わせが必要になる。あるいは `ModifyMenuW` で該当項目のみをピンポイントに書き換える設計も考えられるが、いずれも WI-07 が確立した「メニューは起動時に一度構築するだけ」という既存の単純な設計からの逸脱であり、WI-10 単独の footprint を大きく超える。

## 完了条件

- [ ] `keybindings.reload`/`keybindings.preset.*` 実行後、メニューバー上のキーバインド表示 (`\tCtrl+X` 等) が新しい設定を反映する
- [ ] メニュー再構築が既存のメニュー項目クリック→`dispatchCommand()` の経路を壊さない

## 再検証コマンド

```bash
grep -rn "SetMenu\|ModifyMenuW\|DrawMenuBar" src/ui/src/menu_bar.cpp src/ui/src/main_window.cpp
```
