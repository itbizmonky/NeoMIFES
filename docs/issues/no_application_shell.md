# Issue: アプリケーションシェルが未実装 (P0 — 製品として成立しない)

- **起票日:** 2026-08-04 (中間レビュー、Phase 8f / 7y 完了時点)
- **解消日:** 2026-08-13 (WI-07 完了、🎉 M2「アプリケーションとして成立」達成)
- **状態:** ✅ **解消済み** — WI-01〜WI-07 (Phase 8.5 全体) で完了条件を全て満たした
- **対象:** `src/app/main.cpp`、`src/ui/`
- **優先度:** ~~最高 (P0)~~ → 解消済み
- **対応 Phase:** [Phase 8.5b〜8.5g](../design/master_roadmap.md) (roadmap v2.1 で新設)
- **親文書:** [`gap_analysis.md`](../design/gap_analysis.md) §3.2〜§3.5、§4.2、§4.3
- **関連:** [`no_document_save_capability.md`](no_document_save_capability.md) (P0-1、本 issue と同根、WI-01+WI-02 で解消済み)

## 事実

エンジン層 (Document / Rendering / Search / Encoding / Syntax / Plugin) は商用水準に達している一方、**アプリケーションとして成立するための要素がほぼ全て欠落している**。実コード検証で確認済み (2026-08-04):

| 要素 | 検証 | 結果 |
|---|---|---|
| ファイルを開くダイアログ | `IFileDialog` / `GetOpenFileName` | **0 件** — 起動時 `--open` のみ |
| ドラッグ&ドロップ | `WM_DROPFILES` | **0 件** |
| 新規ファイル (Ctrl+N) | — | **未実装** |
| 複数文書 / タブ | `TabBar` / `DocumentManager` / `Workspace` | **0 件** — `main.cpp` が `Document` を単一ローカル変数として保持 |
| メニューバー | `CreateMenu` / `LoadMenuW` | **0 件** |
| アクセラレータテーブル | `HACCEL` | **0 件** — 全キーバインドが `if` 連鎖でハードコード |
| ステータスバー | `STATUSCLASSNAME` | **0 件** — 行:桁も文字コードも画面に出ない |
| 行番号 | — | **未描画** (ガターは 4b8c で新設済みだがブックマーク専用) |
| ウィンドウタイトル | `SetWindowTextW` | **ファイル名を反映しない** |
| コンテキストメニュー | `WM_CONTEXTMENU` / `TrackPopupMenu` | **0 件** |
| 横スクロール | `WM_HSCROLL` / `leftColumn` | **0 件** — **画面幅を超える行の右端に到達できない** |
| 全選択 (Ctrl+A) | — | **未実装** |
| アイコン / リソース | `*.rc` / `*.ico` / `*.manifest` | **リポジトリに 1 つも無い** |
| IME (メインエディタ) | `main_window.cpp` の `WM_IME_*` | **0 件** — 別 issue [`no_ime_support_in_main_editor.md`](no_ime_support_in_main_editor.md) |

`src/ui/src/main_window.cpp` が処理する Windows メッセージは 15 種のみ:
`WM_PAINT` / `WM_SIZE` / `WM_DPICHANGED` / `WM_KEYDOWN` / `WM_SYSKEYDOWN` / `WM_CHAR` / `WM_MOUSEWHEEL` / `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONUP` / `WM_COMMAND` / `WM_NOTIFY` / `WM_ERASEBKGND` / `WM_CLOSE` / `WM_DESTROY`

## 影響

- 起動しても「どのファイルを開いているか」「今どこにカーソルがあるか」が**画面から一切分からない**
- exe のアイコンは MSVC 既定のまま。**配布してもユーザーは製品と認識しない**
- 長い行の右端は**閲覧も編集も不可能** (横スクロール欠落)
- 要件定義書 §6 の「複数タブ」「複数ウィンドウ」が未達

## 併発しているアーキテクチャ負債: `main.cpp` の肥大化

**`src/app/main.cpp` は 2,053 行。** CLAUDE.md 絶対ルール 4 は「1 関数 ≤ 50 行、1 クラス ≤ 300 行」を定めるが、`main.cpp` はクラスではないため形式上ルールを免れたまま、アプリケーション全体の状態機械・全キーバインド・全モード遷移・全ウィジェット配線を単一ファイルに蓄積している。

**タブ (複数文書) をこのファイルに足すことは不可能。** roadmap §8.5.5 (Phase 8.5c) で `app::EditorSession` / `app::Workspace` を新設し、**タブ UI (8.5d) より先に** `main.cpp` を解体する順序を規定した。

## 対応方針

roadmap v2.1 §8.5 に実装詳細を規定済み。サブフェーズ順序 (依存関係あり):

```
8.5a 保存基盤 → 8.5b ファイルUI → 8.5c main.cpp解体 → 8.5d タブ → 8.5f クローム
                                     ↑ 8.5e IME もここに依存
8.5g 横スクロール (独立、早期着手が望ましい — 後になるほど RenderPipeline への波及先が増える)
```

## 完了条件

- [x] `Ctrl+O` / `Ctrl+N` / ドラッグ&ドロップでファイルを開ける (WI-02、2026-08-04。`IFileOpenDialog`/`onDropFiles`/`confirmDiscardIfDirty()`)
- [x] 10 個のファイルをタブで開き `Ctrl+Tab` で切り替えられる (WI-05、2026-08-08。各タブが独立した `EditorSession`/Undo履歴を保持)
- [x] メニューバー / ステータスバー / 行番号 / ウィンドウタイトル / コンテキストメニューが機能する (WI-07、2026-08-13)
- [x] 全キーバインドが `HACCEL` に集約されている (8.6b キーバインド設定の前提)。**ただし字義通りの「全て」ではなく意図的な narrow scope** — Find/Grep/CommandPalette/Outline/GotoLineの各トグルキーはオーバーレイウィジェットのフォーカス競合のため既存の `handle*Key()` 連鎖に残した。詳細は `build_plan.md` WI-07「実装後の確定事項」参照
- [x] 長い行の右端まで横スクロールで到達できる (WI-03、2026-08-05)
- [x] `neomifes.rc` / `.ico` が存在し、exe に埋め込まれている。**`.manifest` は新設しなかった** — `.rc` が `RT_MANIFEST` を定義しない設計にすることで `main.cpp` 既存のリンカプラグマ製マニフェストとの衝突を回避したため(DPI awareness は `SetProcessDpiAwarenessContext()` で別途対応済み、`requestedExecutionLevel=asInvoker` はマニフェスト無しの既定動作と同義)。詳細は `build_plan.md` WI-07「実装後の確定事項」参照
- [x] **`src/app/main.cpp` が 500 行以下** (WI-04完了時点で361行、2026-08-13時点で398行)
- [x] 既存の全テストが green を維持している (WI-01〜WI-07、各WI完了時に Debug/Release/ubsan 全 green を確認)

## 再検証コマンド

```bash
grep -rn "IFileDialog\|WM_DROPFILES" --include=*.cpp src/       # 0 件なら未解消
grep -rn "class TabBar\|class Workspace" --include=*.h src/     # 0 件なら未解消
grep -rn "CreateMenu\|HACCEL\|STATUSCLASSNAME" --include=*.cpp src/
find . -name "*.rc" -o -name "*.ico" | grep -v build
wc -l src/app/main.cpp                                           # 1000 行超なら未解消
```
