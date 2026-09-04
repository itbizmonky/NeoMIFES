# Issue: `rectangularAnchor` がキーボード操作のみでは決してリセットされず、無関係な矩形選択を再開すると古い基点が再利用される (P2 — 解決済み)

- **起票日:** 2026-09-04 (WI-26の実機ドッグフーディング中に発見)
- **解決日:** 2026-09-04 (WI-27)
- **対象:** `src/app/editor_input.cpp`(`dispatchMouseDown()`, `rectangularAnchor`のリセット箇所)、`src/app/normal_mode_wiring.cpp`(`handleKeyDownEvent()`/`handleSysKeyDownEvent()`)
- **優先度:** P2(実害はキーボードのみで矩形選択を複数回行うワークフローに限定されるが、無言で意図しない範囲を選択・編集してしまうため気づきにくい)

## 事実

`EditorSession::rectangularAnchor()`(`std::optional<TextPos>`)は、Shift+Alt+ドラッグ/Shift+Alt+矢印で矩形選択を開始・継続するための状態で、「マウスで始めた矩形選択をキーボードで継続できる」ようにするため意図的にセッションを跨いで永続化される設計になっている(`normal_mode_wiring.cpp:2013-2021`のコメント参照)。

このリセットは以下の**マウス操作のみ**で発生する(`editor_input.cpp:429-456`, `dispatchMouseDown()`):
- 通常のクリック(Alt無し) — 常にリセット
- 素のAlt+クリック(Shiftを伴わない) — リセット(「これから始まるAlt+ドラッグを矩形選択と誤認しないため」と明記されたコメント付き)

一方で、**キーボードのみの操作では一切リセットされない**。具体的には以下のいずれもリセットしない(コード上に対応するリセット処理が存在しない、`grep -rn rectangularAnchor src/app`で確認済み):
- 矢印キー単体、`Home`/`End`/`Ctrl+Home`等の通常カーソル移動
- `Backspace`/`Delete`/文字入力などの通常編集操作
- `Ctrl+Z`/`Ctrl+Y`(Undo/Redo)
- ドキュメントを閉じずに同一セッション内で行う他の操作全般

`document_open.cpp:59`でドキュメントを開いたときはリセットされるが、これは「別ファイルを開いた」ケースのみをカバーする。

## 再現手順(WI-26ドッグフーディングで実際に踏んだ手順)

1. 適当な複数行テキストを開き、カーソルを行0列0に置く
2. `Shift+Alt+↓`を3回押し、列0・行0〜3の矩形選択(幅0)を作る → `rectangularAnchor`が行0列0にセットされる
3. 何か編集する(例: 1文字入力)、`Ctrl+Z`で元に戻す
4. `Ctrl+Home`→`→`→`→`でカーソルを行0列2へ移動(矢印キーのみ、マウスクリックなし)
5. 再度`Shift+Alt+↓`を3回、続けて`Shift+Alt+→`を2回押し、「列2〜4・行0〜3」の矩形選択を作ったつもりになる

**期待される結果:** 手順4で移動した現在位置(行0列2)が新しい基点となり、列2〜4の矩形選択(4行×2文字=8文字)ができる。

**実際の結果:** 手順2で設定された古い基点(行0列0)が`rectangularAnchor`に残ったままのため、`handleSysKeyDownEvent()`の`if (!rectangularAnchor) { rectangularAnchor = ...; }`(`normal_mode_wiring.cpp:2062-2063`)が古い値を温存し、実際には「列0〜4・行0〜3」(4行×3文字=12文字)という、手順4のカーソル移動を実質無視した意図しない範囲が選択される。ステータスバーの選択文字数(`n selected`)で実際に12文字になることを確認済み(意図した8文字ではなく)。

この状態で列削除・列上書き等を実行すると、ユーザーが直前に移動したはずの列位置ではなく、ずっと前の(場合によっては別の編集セッションの)矩形選択基点を左端として編集が行われてしまう。エラーもクラッシュも起きないため、実際に選択範囲を目視確認しない限り気づきにくい。

## 影響

WI-26は`column.append`の実装のみを新規コードとして追加し、矩形選択の作成・編集自体はPhase 4b8a/4b8gの既存実装(未修正のまま)に依存する設計とした。本issueはその既存実装(WI-26より前、Phase 4b8gで導入)に存在する潜在バグであり、WI-26のスコープ外と判断し、実装修正は行わず起票のみに留めた。矩形選択の対話的作成(Shift+Alt+ドラッグ/矢印)自体が本プロジェクトで実機確認されたのはWI-26のドッグフーディングが初めてであり(`TIMELINE.md`の複数セッションにわたる積み残し参照)、このバグもその初回確認で初めて発見された。

キーボードのみで矩形選択を繰り返し使うワークフロー(マウス操作を挟まない)でのみ顕在化するため、影響範囲は限定的だが、MIFES/秀丸ライクな操作を志向する本プロジェクトの性質上、キーボード中心のユーザーほど踏みやすい。

## 対応 (WI-27で実施)

`handleKeyDownEvent()`(WM_KEYDOWN)の冒頭で`rectangularAnchor`/`altCursorAnchor`を無条件にリセットする一手を追加した——WM_KEYDOWNはShift+Alt組み合わせを一切運ばない(それはWM_SYSKEYDOWNとしてOSレベルで別メッセージに分類される)ため、この関数に到達するキー入力は原理的に「矩形選択/Alt+クリック伸長を継続するジェスチャではない」と機械的に確定できる。加えて`handleSysKeyDownEvent()`側にも、プレーンAlt+↑/↓(行移動)分岐とShift+Alt+I(`convertToLineEndCursors()`)分岐の2箇所に同様のリセットを追加、矩形拡張分岐には`handleMouseDragEvent()`の既存の前例(1334行目)に倣い`altCursorAnchor`のリセットを追加した。

**実装中、Plan agentによる設計レビューで2件の見落としを事前に発見・修正**(`handleFreeCursorRightArrow()`の早期returnがリセットをスキップしてしまう配置ミス、`altCursorAnchor`もrectangularAnchorと対称にリセットすべき箇所の見落とし)。

**さらに実機ドッグフーディングで、設計レビューでも見逃されていた重大な設計不備を1件発見・修正した:** `VK_SHIFT`のプレーンな押下(Altより先にShiftを押す、変調キーを離した状態から新しいShift+Alt+矢印を開始する際の自然な順序)は、それ自体が(Shift+Altの組み合わせではなく)通常のWM_KEYDOWN(VK_SHIFT)として先に発火する。当初の実装はこれを「矩形選択と無関係なキー」として無条件にリセット対象にしてしまい、**まさに継続しようとしているその一連のキー入力自身によって、直前のWM_SYSKEYDOWN(矢印)が届く前に基点を破壊してしまう**という自己矛盾したバグを生んでいた(ステータスバーの選択文字数が意図と食い違うことで発覚、`8 selected`のはずが`1 selected`になっていた)。`vkCode == VK_SHIFT`(および念のため`VK_MENU`)をリセット対象から除外することで解消した。**この教訓: 「無関係なキー入力」の定義は、単に「Shift+Alt組み合わせを運ばないメッセージ種別」というメッセージレベルの区別だけでは不十分で、これから来る組み合わせキーの前触れとなるベアな修飾キー単体の押下は除外する必要がある。**

`tests/unit/app_editor_input_test.cpp`へ`dispatchMouseDown()`(既存のマウス側リセット処理、これまで無テストだった)の結合テスト3件を安全網として追加。`normal_mode_wiring.cpp`側の修正自体(WM_KEYDOWN/WM_SYSKEYDOWNハンドラ)はアーキテクチャ上単体テスト不可能なため、実機ドッグフーディングで4シナリオ(バグ再現手順そのものの解消確認・複数回連続拡張の継続動作確認・Shift+Alt+I後の新規基点確認・Alt+クリック→矩形選択でのaltCursorAnchor整合性確認)を確認した。

副次的発見(`handleSysKeyDownEvent()`に`isDiffViewActive()`ガードが無い件)は別issue([`handle_sys_key_down_missing_diff_view_guard.md`](handle_sys_key_down_missing_diff_view_guard.md))として起票した。

## 完了条件

- [x] `rectangularAnchor`のリセット条件を設計し直し、キーボードのみの操作で矩形選択を再開する際に無関係な古い基点が再利用されないようにする
- [x] 上記「再現手順」を実機ドッグフーディングで検証し回帰が無いことを確認(`normal_mode_wiring.cpp`はアーキテクチャ上単体テスト不可能、`dispatchMouseDown()`側の安全網テストは追加済み)

## 再検証コマンド

```bash
grep -rn "rectangularAnchor" src/app
```
