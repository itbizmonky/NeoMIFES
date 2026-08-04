# NeoMIFES 製造全体計画 (Build Plan)

**発行:** 2026-08-04 / **版:** v1.0
**位置づけ:** 本書は **実行順の作業指示書**である。`master_roadmap.md` が「何を作るか (What)」、`gap_analysis.md` が「何が欠けているか (Gap)」を規定するのに対し、本書は **「次に何を、どの順で、どうやって作るか (Execution)」** を規定する。
**対象読者:** **このプロジェクトの文脈を一切持たないセッション。** 本書だけで着手できることを設計目標とする。

---

# 0. コールドスタート手順 ← 迷ったらまずここ

**あなたがこのプロジェクトについて何も知らない場合、以下を順に実行せよ。所要 5〜10 分。**

### Step 1. 3 つだけ読む (それ以上読まない)

| 順 | ファイル | 読む範囲 | 得られるもの |
|---|---|---|---|
| 1 | 本書 §1〜§3 | 全部 (約 100 行) | プロジェクトの現在地と不変ルール |
| 2 | 本書 §5 の **次の未完了 WI** | 1 項目だけ | 今回やる作業の全て |
| 3 | [`CLAUDE.md`](../../CLAUDE.md) | 全部 | 絶対ルールとコーディング規約 |

**`master_roadmap.md` (2,900 行) を最初から読んではいけない。** 必要な章は各 WI が指定する。

### Step 2. 現在地を機械的に確認する

```bash
cd D:/IDE/Claude/NeoMIFES
git log --oneline -5
git status --short
git log origin/main..HEAD --oneline    # 未 push のコミット
```

本書 §3 の進捗チェックリストと `git log` が食い違っていたら、**`git log` を信じ、本書を修正してから作業を始める**。

### Step 3. ビルドが通ることを確認する (必須)

```powershell
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll" -ErrorAction Stop
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
Set-Location "D:\IDE\Claude\NeoMIFES"
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

**green でなければ、まずそれを直す。** 新しい作業を積んではいけない。

### Step 4. §5 の次の WI に着手する

以上。**これ以外の準備は不要。**

---

# 1. このプロジェクトは何か (30 秒で)

**NeoMIFES = Windows 向け純粋ネイティブテキストエディタ。** C++23 + Win32 + Direct2D/DirectWrite。秀丸/サクラ/MIFES を超える「最速・最軽量・AI 親和」を掲げる。

- リポジトリ: `D:\IDE\Claude\NeoMIFES` (GitHub `itbizmonky/NeoMIFES`、main ブランチ)
- 規模: 約 35,000 行 / 966 テスト / ADR 21 本
- **禁止:** Electron / Qt / WPF / WinUI3 主体 / Avalonia / WebView / Chromium / .NET MAUI

**現在の状態を一行で:**

> **エンジン層 (Document / Rendering / Search / Encoding / Syntax / Plugin) は商用水準に完成している。しかし「アプリケーションシェル」が未実装で、編集内容をファイルに保存できない。**

この状態に至った経緯は [`gap_analysis.md`](gap_analysis.md) を参照 (読まなくても作業はできる)。

---

# 2. 不変のルール (毎セッション必ず守る)

### 2.1 やること

1. **推測で実装しない。** 分からないことは実コードを `grep` するか、使い捨て probe プログラムで実測してから書く (CLAUDE.md 絶対ルール 3)
2. **push 前に必ずローカル検証する。** Debug / Release / ubsan の 3 プリセットで `ctest` が全 green、変更ファイルへの clang-tidy が新規警告 0
3. **1 コミット = 1 責務。** WI 1 件 = 1 コミットを基本とする
4. **push はユーザーの明示指示を待つ。** エージェントは自発的に push しない
5. **完了時にドキュメントを同期する** (§4.5 の手順)
6. **実アプリで実際に操作して確認する。** 「プロセスが 3 秒後も生存していた」は機能確認ではない

### 2.2 やらないこと

- `new` / `delete` の直接使用 (RAII と `std::unique_ptr`)
- `dynamic_cast` (`/GR-` ビルドのため)
- グローバル可変状態の追加
- 巨大クラス/巨大関数 (1 関数 ≤ 50 行、1 クラス ≤ 300 行)
- **本書の WI 順序を勝手に飛ばすこと** (依存関係がある。飛ばす場合はユーザーに確認)
- **Phase 9 (AI) / Phase 10 / Phase 11 への先行着手** (WI-13 完了まで凍結)

### 2.3 判断に迷ったときの原則 (優先順位順)

1. **「保存できるか」を最優先する。** ユーザーの編集内容が失われる可能性のある変更は、他の何よりも先に直す
2. **エンジン層は触らない。** Document / Rendering / Search / Syntax は完成している。壊さない
3. **設定システムが無いことを理由に妥協しそうになったら、WI-09 (設定システム) を先に済ませられないか検討する。** 過去に 13 回この妥協が繰り返され、定数の二重定義という負債になった
4. **迷ったら小さく作る。** 「まずヘッドレスで正しく動かし、次に UI へ配線する」は本プロジェクトで 10 回以上成功している型
5. **それでも決められないことだけ、ユーザーに聞く。** 選択肢と推奨案を添えて

---

# 3. 進捗チェックリスト ← 作業完了ごとに更新すること

**完了した WI に `[x]` を付け、コミットハッシュを記入する。これが唯一の進捗管理台帳である。**

## 完了済み (2026-08-04 時点)

- [x] Phase 0〜8f — エンジン層 + プラグイン基盤 (`23c2cc2` まで)
- [x] 中間レビュー — ギャップ分析 + roadmap v2.1 (`a0ac815`)

## Phase 8.5 — アプリケーションシェル (P0)

- [ ] **WI-01** 文書保存基盤 (`document::saveFile()` / `isDirty()`) → コミット: `________`
- [ ] **WI-02** ファイルライフサイクル UI (Ctrl+S / Ctrl+O / Ctrl+N / D&D / 未保存警告) → `________`
  - 🎉 **ここで M1 達成: NeoMIFES で NeoMIFES を編集できるようになる (ドッグフーディング開始)**
- [ ] **WI-03** 横スクロール (`leftColumn` / `WM_HSCROLL`) → `________`
- [ ] **WI-04** `main.cpp` 解体 + `EditorSession` / `Workspace` 新設 → `________`
- [ ] **WI-05** タブ UI (`ui::TabBar`) → `________`
- [ ] **WI-06** IME 完全対応 (`WM_IME_*` + インライン未確定文字列) → `________`
- [ ] **WI-07** ウィンドウクローム (メニュー / `HACCEL` / ステータスバー / 行番号 / `.rc`) → `________`
  - 🎉 **M2 達成: アプリケーションとして成立**

## Phase 8.6 — 製品化基盤 (P1)

- [ ] **WI-08** 設定システム (`core::Settings`) + ハードコード定数の移行 → `________`
- [ ] **WI-09** テーマ (ダーク / ライト / ハイコントラスト) → `________`
- [ ] **WI-10** キーバインド設定 + プリセット (秀丸 / サクラ / VSCode) → `________`
- [ ] **WI-11** 自動保存 / バックアップ / クラッシュ復旧 / 最近開いたファイル → `________`
- [ ] **WI-12** 基本編集の穴埋め (Ctrl+A / 自動インデント / 行複製・移動・削除) → `________`
  - 🎉 **M3 達成: 設定・テーマが揃う**

## Phase 12' — MVP 出荷判定

- [ ] **WI-13** MVP 出荷判定 (§6 のチェックリスト全項目) → `________`
  - 🎉 **M4 達成: 秀丸/サクラの代替として出荷可能**

## Phase 10 以降 (WI-13 完了まで着手禁止)

- [ ] **WI-14** Phase 10 — ログ解析 / CSV / JSON-XML Tree (最大の差別化点)
- [ ] **WI-15** Phase 11 — Git 統合 / LSP / マクロ
- [ ] **WI-16** Phase 9 — AI プラグイン
- [ ] **WI-17** Phase 12 — 総合品質保証・正式出荷
  - 🎉 **M5 達成: 正式出荷**

---

# 4. セッション標準手順

**全ての WI はこの手順で進める。** WI 個別の指示は §5 にあるが、手順は共通。

### 4.1 着手 (10 分)

1. §0 のコールドスタート手順を実行 (ビルド green を確認)
2. §5 の該当 WI を読む
3. WI が指定する roadmap の章だけを読む
4. **WI の「既に決まっている設計」に無い判断が必要になったら、実コードを読んで決める。** 記憶で決めない

### 4.2 実装

- WI の「影響ファイル」に沿って進める
- 新規の外部依存を足す場合は **ADR を起票** (`docs/decisions/ADR-0NN-*.md`、次番号は `docs/decisions/README.md` で確認)
- roadmap のスケッチから逸脱したら、**その理由を WI の完了記録に書く** (逸脱自体は悪くない。黙って逸脱するのが悪い)

### 4.3 検証 (必須・省略不可)

```powershell
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll" -ErrorAction Stop
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
Set-Location "D:\IDE\Claude\NeoMIFES"
cmake --build --preset debug   ; ctest --preset debug   --output-on-failure
cmake --build --preset release ; ctest --preset release --output-on-failure
cmake --build --preset ubsan   ; ctest --preset ubsan   --output-on-failure
```

clang-tidy (**変更したファイルだけ**。全ファイル一括はタイムアウトする):

```powershell
$tidy = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
& $tidy -p build\debug --quiet --extra-arg=-Wno-unused-command-line-argument <変更したファイル>
```

**加えて、その WI の DoD に書かれた実アプリ確認を行う。**

### 4.4 コミット

```
<type>(<scope>): WI-NN <一行要約>

<何を実装したか、roadmap スケッチからの逸脱があればその理由>

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

`type` は `feat` / `fix` / `refactor` / `docs` / `chore`。**push しない。**

### 4.5 ドキュメント同期 (必須)

| ファイル | 何を書くか |
|---|---|
| **本書 §3** | 該当 WI に `[x]` とコミットハッシュ |
| **本書 §5 の該当 WI** | 末尾に「実装後の確定事項」(逸脱・発見・残課題) |
| `docs/design/detailed_design.md` | 実装した機能のリファレンス節を追加 |
| `docs/design/master_roadmap.md` | §2 フェーズ表の状態更新 + 該当章に「実装後の確定事項」 |
| `docs/handoff/RESUME_HERE.md` | 冒頭の「次にやること」を次の WI へ更新 |
| `docs/history/TIMELINE.md` | 末尾にセッション記録を 1 節追記 |
| `docs/issues/` + `docs/issues/README.md` | 新たに先送りした項目があれば起票 + 索引に 1 行 |
| `docs/decisions/README.md` | ADR を起票した場合 |
| メモリ (`project_neomifes_state.md` / `MEMORY.md`) | 現在地の更新 |

**「◯◯が無いため縮退した」と判断したら必ず issue 化する。同じ理由が 3 回を超えたら、その基盤を次 WI に格上げする。**

---

# 5. 作業単位 (Work Item) 詳細

> **各 WI は 1 セッション相当の分量に切ってある。** 大きすぎると感じたら分割してよい (`WI-04a` / `WI-04b` のように)。分割したら §3 のチェックリストも更新する。

---

## WI-01 — 文書保存基盤 🔴 最優先

**目的:** `document::Document` の内容をファイルへ書き出せるようにする。**本プロジェクト最大の欠落を埋める。**

**前提:** なし (これが最初)

**参照:** `master_roadmap.md` §8.5.3 / `docs/issues/no_document_save_capability.md`

### 既に決まっている設計

**最大の課題: 自分が mmap しているファイルへは直接書き戻せない。** Phase 6d 以降、`OriginalBuffer` は `CreateFileW(GENERIC_READ)` + `MapViewOfFile` でファイルを読み取り専用マップしている。

**採用する手順:**

```
1. 同じディレクトリに一時ファイルを作る (例: <name>.neomifes-tmp)
2. BufferSnapshot の pieces() を先頭から走査し、pieceView(piece) で得た
   u16string_view を encoding::encode() で目的エンコードへ変換しつつ
   一時ファイルへ順次書き出す
   ★ 全文を一つの u16string へ実体化しないこと (10GB 対応の生命線)
   ★ 改行コード変換 (LF ⇔ CRLF ⇔ CR) と BOM 付与もこの段で行う
3. flush + ハンドルクローズ
4. OriginalBuffer のマップを解放し、元ファイルのハンドルを完全に手放す
5. ReplaceFileW(元, 一時, バックアップ) でアトミック置換
   (ACL とタイムスタンプが保たれる。MoveFileEx より望ましい)
6. 保存後のファイルを再 mmap し、Piece Table を単一 Original ピースへ再構築
```

**使える既存 API (実在確認済み):**

| API | 用途 |
|---|---|
| `BufferSnapshot::pieces()` → `const std::vector<Piece>&` | ピース列の取得 |
| `BufferSnapshot::pieceView(const Piece&)` → `std::u16string_view` | ピース内容 (O(1) or 初回のみデコード) |
| `encoding::encode(u16string_view, Encoding)` → `variant<vector<byte>, EncodeError>` | UTF-16 → 目的エンコード |
| `Document::snapshot()` → `shared_ptr<const BufferSnapshot>` | スナップショット取得 |

**新設する API:**

```cpp
// document.h
enum class SaveError { CannotCreateTemp, WriteFailed, EncodeFailed, ReplaceFailed };

// 保存する。encoding/lineEnding/bom は「この内容で書き出す」指定。
[[nodiscard]] std::expected<void, SaveError>
saveFile(const std::filesystem::path& path, encoding::Encoding enc,
         encoding::LineEnding le, bool writeBom);

[[nodiscard]] bool isDirty() const noexcept;   // 最後の保存以降に編集されたか
void markSaved() noexcept;                      // saveFile() 成功時に呼ぶ
```

`isDirty()` は既存の `m_version` を使い、`m_savedVersion` メンバとの比較で実装するのが最小。

### 着手前に必ず probe で確かめること (推測禁止)

| # | 確かめること | なぜ |
|---|---|---|
| **U#22** | 手順 6 で Piece Table を再構築した後、既存 `UndoStack` が保持する `TextRange` と `BufferSnapshot` の `shared_ptr` 参照が有効か | オフセットは不変なので理屈上は有効だが、`shared_ptr` の寿命関係は実測で確認する |
| **U#23** | `ReplaceFileW` が他プロセスのロックで失敗したとき、一時ファイルを残すか消すか | データ保全 vs ゴミファイル。**元ファイルが壊れないことだけは絶対条件** |
| U#26 | `MapViewOfFile` 済みのファイルに対して `ReplaceFileW` を呼ぶと何が起きるか (手順 4 のマップ解放が本当に必要か) | 必要ないなら手順が簡潔になる |

probe は使い捨て。スクラッチパッドに書き、**コミットしない** (本プロジェクトの確立した慣習)。

### 影響ファイル

- `src/document/include/neomifes/document/document.h` / `src/document/src/document.cpp` — `saveFile()` / `isDirty()` / `markSaved()`
- `src/document/include/neomifes/document/original_buffer.h` / `.cpp` — マップ解放 API の追加
- `src/document/CMakeLists.txt` — 依存追加が必要なら (`neomifes::encoding` は Phase 6d で既にリンク済み)
- `tests/unit/document_document_test.cpp` — `isDirty()` の状態遷移
- `tests/integration/` — 新規 `document_save_roundtrip_test.cpp`

### DoD

- [ ] 「開く → 編集 → 保存 → 再度開く → 内容一致」のラウンドトリップテストが green
- [ ] UTF-8 / UTF-8 BOM / UTF-16LE / Shift-JIS それぞれで保存でき、`detectEncoding()` が保存後のファイルを正しく判定する
- [ ] 改行コード LF / CRLF / CR を指定して保存でき、`detectLineEnding()` が一致する
- [ ] **100MB 以上のファイルを保存してもピークメモリがファイルサイズに比例しない** (全文実体化していないことの証明。`document_load_bench.cpp` の計測パターンを流用)
- [ ] 保存が失敗しても**元ファイルが壊れない** (U#23 の結論に従う)
- [ ] `isDirty()` が編集で true、保存で false になる
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-02 — ファイルライフサイクル UI 🎉 M1

**目的:** ユーザーが実際にファイルを開き、保存できるようにする。**この WI 完了時点でドッグフーディングが始まる。**

**前提:** WI-01

**参照:** `master_roadmap.md` §8.5.4

### 既に決まっている設計

- **キーバインド:** `Ctrl+S` (保存) / `Ctrl+Shift+S` (名前を付けて保存) / `Ctrl+O` (開く) / `Ctrl+N` (新規)
- **ダイアログ:** `IFileOpenDialog` / `IFileSaveDialog` (COM)。**既存 [ADR-008](../decisions/ADR-008-com-raii-comptr.md) の `Microsoft::WRL::ComPtr` 流儀をそのまま踏襲する** (`src/render/` に前例が多数)
- **ドラッグ&ドロップ:** `DragAcceptFiles(hwnd, TRUE)` + `WM_DROPFILES`。`MainWindow` に `onDropFiles` フックを新設 (既存の `onKeyDown` / `onMouseDown` 等と同じパターン)
- **未保存警告:** `TaskDialogIndirect` (Windows 10/11 標準の外観。`MessageBoxW` より望ましい)。「保存する / 保存しない / キャンセル」の 3 択
- **`WM_CLOSE`:** ⚠️ **`MainWindowConfig` に `onClose` フックは存在しない** (実在するのは `onWindowCreated` / `onFirstPaint` / `onDeferredInit` / `onResize` / `onKeyDown` / `onSysKeyDown` / `onChar` / `onMouseWheel` / `onMouseDown` / `onMouseDrag` / `onCommand` / `onAppMessage` / `onNotify` の 13 種)。`main_window.cpp:177` の `case WM_CLOSE:` は無条件に `DestroyWindow()` を呼ぶだけ。**`onClose` フック (戻り値 `bool` = 閉じてよいか) を新設する必要がある**

**名前を付けて保存ダイアログにエンコード/改行選択を出すかは、この WI では出さない。** 既定 (元ファイルと同じ、新規なら UTF-8 / CRLF) で保存し、選択 UI は WI-07 (ステータスバーから変更) へ回す。理由: ダイアログのカスタマイズは `IFileDialogCustomize` が必要で本 WI が肥大化するため。

### 影響ファイル

- `src/app/main.cpp` — Ctrl+S/O/N ハンドラ、`WM_DROPFILES`、未保存警告
- `src/ui/include/neomifes/ui/main_window.h` / `src/ui/src/main_window.cpp` — `onDropFiles` フック、`DragAcceptFiles`
- 新規 `src/app/include/neomifes/app/file_dialogs.h` / `src/app/src/file_dialogs.cpp` — `IFileDialog` ラッパ (COM を app 層に閉じ込める)
- `src/app/CMakeLists.txt`
- `tests/unit/` — ダイアログ本体はテスト不能。**パス正規化やダーティ判定など純粋ロジックだけを切り出してテストする**

### DoD

- [ ] `Ctrl+O` でファイルを開き、`Ctrl+S` で保存し、再度開くと編集内容が保持されている
- [ ] `Ctrl+Shift+S` で別名保存できる
- [ ] `Ctrl+N` で空の新規文書になる
- [ ] エクスプローラからファイルをドラッグ&ドロップして開ける
- [ ] 未保存のまま `Ctrl+N` / `Ctrl+O` / ウィンドウを閉じる、のいずれでも警告が出て「キャンセル」で操作が中止される
- [ ] 🎉 **ドッグフーディング: NeoMIFES で NeoMIFES のソースを開いて編集し、保存し、そのままコミットできた** ← **本 WI の完了条件の中で最も重要**
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

### 完了後にやること

**`CLAUDE.md` §11 のチェックリストに「本セッションの変更を NeoMIFES 自身で編集して確認したか」を追加する。** これ以降、ドッグフーディングは全 WI の標準手順になる。

---

## WI-03 — 横スクロール

**目的:** 画面幅を超える行の右端に到達できるようにする。**現状は閲覧も編集も不可能。**

**前提:** なし (独立。ただし **WI-05 (タブ) より前に必ず終わらせる** — 後になるほど `RenderPipeline` の X 座標計算への波及先が増える)

**参照:** `master_roadmap.md` §8.5.9

### 既に決まっている設計

- `RenderPipeline` に `m_leftColumn` (先頭表示桁) を持たせ、描画時に X 座標へ `-leftColumnDips` のオフセットを掛ける
- **波及先 (全て `render_pipeline.cpp`):** キャレット描画 / 選択範囲 / マッチハイライト / Indent guides / フォールドマーカー / `hitTest()` / `hitTestFoldMarker()`。**ガターとミニマップは横スクロールしない** (画面に固定)
- `WM_HSCROLL` + `SetScrollInfo` で標準の水平スクロールバーを出す
- `Home` / `End` / 文字入力時に**キャレットが画面外なら自動で追従スクロールする**必要がある (`Viewport` の縦方向と同じ考え方)

**折返し表示 (word wrap) は本 WI のスコープ外。** 横スクロールと排他的な別モードであり、`RenderPipeline` の「論理行 ⇔ 画面行」マッピング全体に波及する。要否は U#25 として Phase 12 前に判断する。

### 影響ファイル

- `src/render/include/neomifes/render/render_pipeline.h` / `src/render/src/render_pipeline.cpp`
- `src/render/include/neomifes/render/viewport_math.h` — 横方向の可視桁範囲を求める純粋関数 (既存の `computeVisibleLineCount()` と同じ「デバイス非依存・ヘッダオンリー・単体テスト可能」パターン)
- `src/ui/src/main_window.cpp` — `WM_HSCROLL`
- `src/app/main.cpp` — スクロール状態の同期
- `tests/unit/render_viewport_math_test.cpp` — 桁範囲計算
- `tests/integration/render_text_smoke_test.cpp` — 横スクロール後の `hitTest()` ラウンドトリップ

### DoD

- [ ] 1000 文字の行を含むファイルで、右端まで横スクロールして内容を読める
- [ ] 横スクロール中にクリックしたとき、`hitTest()` が正しい文字位置を返す
- [ ] `End` キーでキャレットが行末へ移動し、画面が自動追従する
- [ ] ガターとミニマップは横スクロールしても位置が変わらない
- [ ] `--measure-frame` の実測値が既存ベースライン (avgFrameNs ≈ 16.5ms) から悪化していない
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-04 — `main.cpp` 解体 + 複数文書モデル

**目的:** 2,053 行の `main.cpp` を解体し、複数文書を扱える構造にする。**WI-05 (タブ) の前提。**

**前提:** WI-02、WI-03

**参照:** `master_roadmap.md` §8.5.5

### 既に決まっている設計

**本 WI は新機能を 1 つも足さない純粋なリファクタリングである。完了条件は「既存の全テストが無変更で green を保つこと」。**

現状 `main.cpp` の `wWinMain` は以下を全てローカル変数として保持している:
`Document` / `SelectionModel` / `CommandDispatcher` / `Viewport` / `FoldingModel` / `BookmarkManager` / `FindReplaceState` / `GrepState` / `RenderPipeline` / 各ウィジェット / 全キーバインド / 全モード遷移。

**新設する型:**

```cpp
// src/app/include/neomifes/app/editor_session.h
// 「1 つの開いている文書」に紐づく全状態。タブ 1 枚 = EditorSession 1 個。
class EditorSession {
    document::Document        m_document;
    core::SelectionModel      m_selection;
    core::CommandDispatcher   m_dispatcher;    // Undo 履歴を含む
    core::Viewport            m_viewport;
    core::FoldingModel        m_folding;
    core::BookmarkManager     m_bookmarks;
    std::optional<syntax::Language> m_language;
    std::filesystem::path     m_path;
    bool                      m_isUntitled = true;
    // + アクセサ
};

// src/app/include/neomifes/app/workspace.h
// EditorSession の集合 + アクティブタブ。1 ウィンドウ = 1 Workspace。
class Workspace {
    std::vector<std::unique_ptr<EditorSession>> m_sessions;
    std::size_t m_activeIndex = 0;
public:
    EditorSession& active();
    std::size_t openFile(const std::filesystem::path&);   // 既に開いていればそのタブを返す
    bool closeSession(std::size_t index);                  // 未保存なら false (呼び出し側が確認)
    void activate(std::size_t index);
    [[nodiscard]] bool hasUnsavedChanges() const;
};
```

**`main.cpp` に残すもの:** `wWinMain` / ウィンドウ生成 / メッセージループ / `Workspace` と `RenderPipeline` の所有のみ。キーバインド処理は `src/app/editor_input.cpp` と新設のコマンドテーブルへ移す。

**移設の進め方 (安全な順序):** 一度に全部動かさない。
1. まず `EditorSession` を作り、`main.cpp` のローカル変数群をそこへ**移すだけ** (呼び出し側は `session.document()` のように置換)。テスト green を確認してコミット
2. 次に `Workspace` を被せる (要素数 1 のまま)。テスト green を確認してコミット
3. 最後にキーバインド群を `editor_input.cpp` へ移す

**単一 `SyntaxWorker` を共有するか、タブごとに持つか (U#24)** は WI-05 で判断する。本 WI では現状どおり `RenderPipeline` が 1 個持つまま。

### 影響ファイル

- 新規 `src/app/include/neomifes/app/editor_session.h` (+ 必要なら `.cpp`)
- 新規 `src/app/include/neomifes/app/workspace.h` / `src/app/src/workspace.cpp`
- `src/app/main.cpp` — **大幅縮小**
- `src/app/src/editor_input.cpp` — キーバインド移設先
- `src/app/CMakeLists.txt`
- 新規 `tests/unit/app_workspace_test.cpp` — `openFile` の重複検出 / `closeSession` / `hasUnsavedChanges`

### DoD

- [ ] **`src/app/main.cpp` が 500 行以下** (現状 2,053 行)
- [ ] **既存の全テストが無変更で green** (新機能を足していないことの証明)
- [ ] 実アプリの挙動が WI-03 完了時点と完全に同一 (ドッグフーディングで確認)
- [ ] `Workspace` の単体テストが追加されている
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-05 — タブ UI

**目的:** 複数ファイルを同時に開けるようにする (要件定義書 §6 必須)。

**前提:** WI-04

**参照:** `master_roadmap.md` §8.5.6

### 既に決まっている設計

- 新規 `ui::TabBar`。**実装方式 (`WC_TABCONTROL` か自前 D2D 描画か) は着手時に決める。** 既存ウィジェット (`FindBar` / `GrepBar` / `OutlinePane` / `CommandPalette`) は全て標準コントロール路線であり、それに合わせるのが自然
- キーバインド: `Ctrl+Tab` / `Ctrl+Shift+Tab` / `Ctrl+W` / `Ctrl+PgUp` / `Ctrl+PgDn` / `Ctrl+1`〜`Ctrl+9`
- タブに未保存マーカー (●) を表示
- タブバーの高さぶん、`RenderPipeline` の `reservedTopHeightDips()` を増やす (Breadcrumb / Sticky scroll と同じ機構が既にある)

**判断が必要な点 (U#24):** `render::SyntaxWorker` をタブごとに持つか 1 個を共有するか。
- 共有する場合、タブ切替時に `requestParse(..., resetIncrementalState=true)` で保持木を捨てる必要がある (この経路は Phase 8d で確立済み)
- タブごとに持つ場合、メモリと `std::thread` 数が増える
- **まず共有で作り、体感が悪ければ分ける。** ベンチ根拠なしに先行して複雑化しない (CLAUDE.md ルール 10)

### 影響ファイル

- 新規 `src/ui/include/neomifes/ui/tab_bar.h` / `src/ui/src/tab_bar.cpp`
- `src/app/main.cpp` — タブ切替の配線
- `src/render/src/render_pipeline.cpp` — `reservedTopHeightDips()` にタブバー高さを加算
- `src/ui/CMakeLists.txt`
- `tests/unit/` — タブ順序・切替インデックス計算などの純粋ロジック

### DoD

- [ ] 10 個のファイルをタブで開き、`Ctrl+Tab` で切り替えられる
- [ ] **各タブが独立した Undo 履歴・カーソル位置・スクロール位置・検索状態を保持する**
- [ ] 未保存タブに ● が表示され、保存すると消える
- [ ] `Ctrl+W` で閉じるとき、未保存なら警告が出る
- [ ] タブ切替時にシンタックスハイライトが正しい言語で再描画される
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-06 — IME 完全対応

**目的:** メインエディタで日本語をまともに入力できるようにする。**日本語市場向け製品として単独で出荷を阻む欠陥の解消。**

**前提:** WI-04

**参照:** `master_roadmap.md` §8.5.7 / §16.1 / `docs/issues/no_ime_support_in_main_editor.md`

### 現状 (実測済み)

`src/ui/src/main_window.cpp` が処理する `WM_*` は 15 種で、**`WM_IME_*` は 1 つも含まれない。** `ImmGetContext` / `CANDIDATEFORM` の使用も 0 件。Find bar 等が IME を扱えているのは標準 `WC_EDIT` 子コントロールが Win32 から無償で得ているだけで、D2D 描画のメインテキスト領域とは無関係。

確定文字列は `WM_CHAR` で届くため「入力自体はできる」が、**変換中の未確定文字列が画面に出ず、候補ウィンドウがキャレットに追従しない。**

### 既に決まっている設計

| メッセージ | 処理 |
|---|---|
| `WM_IME_STARTCOMPOSITION` | 未確定文字列の描画を開始。**`return 0` で既定の IME ウィンドウを抑止する** |
| `WM_IME_COMPOSITION` | `ImmGetCompositionStringW(GCS_COMPSTR)` で未確定文字列、`GCS_RESULTSTR` で確定文字列、`GCS_COMPATTR` で変換対象節の属性を取得 |
| `WM_IME_ENDCOMPOSITION` | 未確定表示をクリア |

- `ImmSetCandidateWindow(CFS_CANDIDATEPOS)` で候補ウィンドウをキャレット位置へ追従させる
- `RenderPipeline` に未確定文字列のインライン描画を追加 (下線 + 変換対象節のハイライト)
- **`imm32.lib` のリンク追加が必要** (`src/ui/CMakeLists.txt`)
- **複数カーソル時の挙動を決める。** 最小案: IME 変換中はプライマリカーソルのみで変換し、確定時に全カーソルへ挿入する / あるいは変換中は複数カーソルを畳む。どちらでもよいが**明示的に決めてコメントに書く**

### DoD

- [ ] メインエディタで未確定文字列が**下線付きでキャレット位置にインライン表示される**
- [ ] 変換対象節がハイライトされる
- [ ] 候補ウィンドウがキャレット位置に追従する
- [ ] 変換確定後、確定文字列が Undo 1 ステップとして `Document` へ挿入される
- [ ] 複数カーソル時の挙動が定義され、コメントに明記されている
- [ ] 🔴 **実機で MS-IME による手動確認を完了している。自動テストによる代替を認めない。** 「にほんご」と入力し、未確定文字列・候補ウィンドウの表示を目視確認し、スクリーンショットを `TIMELINE.md` に記録する
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

**中国語 / 韓国語 IME の確認は Phase 12 (WI-17) へ。** 本 WI は日本語のみ。

---

## WI-07 — ウィンドウクローム 🎉 M2

**目的:** 「起動しても何のファイルを開いているか分からない」状態を解消し、配布物としての体裁を整える。

**前提:** WI-04

**参照:** `master_roadmap.md` §8.5.8

### 既に決まっている設計

| 要素 | 実装 |
|---|---|
| メニューバー | `CreateMenu` / `AppendMenuW`。ファイル / 編集 / 検索 / 表示 / ツール / ヘルプ |
| アクセラレータテーブル | `CreateAcceleratorTable` + `TranslateAcceleratorW`。**現在 `editor_input.cpp` と `main.cpp` に散在する `if (ctrlDown && vkCode == 'X')` の連鎖を `HACCEL` + コマンド ID へ集約する。これが WI-10 (キーバインド設定) の前提** |
| ステータスバー | `STATUSCLASSNAME`。行:桁 / 選択文字数 / 文字コード / 改行コード / INS-OVR / 言語 |
| 行番号 | Phase 4b8c で新設したブックマーク専用ガター (`kGutterWidthDips=24`) を拡張して行番号を描画。**幅は桁数に応じて動的に** |
| ウィンドウタイトル | `<ファイル名> [*] - NeoMIFES` (`*` は未保存) |
| コンテキストメニュー | `WM_CONTEXTMENU` + `TrackPopupMenu` |
| リソース | 新規 `resources/neomifes.rc` / `neomifes.ico` / `neomifes.manifest` (DPI awareness / Common Controls v6 / `requestedExecutionLevel=asInvoker`) |

**ステータスバーの文字コード・改行コード欄はクリックで変更できるようにする** (WI-02 でダイアログに出さなかった選択 UI をここで提供する)。

**アイコンについて:** `.ico` は自前で用意する必要がある。デザインが決められない場合は、暫定として単色背景に "N" の字を置いた最小限のものを作り、**issue に「アイコンの正式デザイン」として起票して先送りしてよい** (体裁上、既定アイコンのままよりは遥かに良い)。

### 影響ファイル

- 新規 `resources/neomifes.rc` / `neomifes.ico` / `neomifes.manifest`
- `src/app/CMakeLists.txt` — `.rc` をターゲットソースへ追加
- `src/ui/src/main_window.cpp` — メニュー / `WM_CONTEXTMENU` / `TranslateAcceleratorW`
- 新規 `src/ui/include/neomifes/ui/status_bar.h` / `src/ui/src/status_bar.cpp`
- `src/render/src/render_pipeline.cpp` — 行番号描画、ガター幅の動的化
- `src/app/main.cpp` — `HACCEL` へのキーバインド集約、タイトル更新

### DoD

- [ ] メニューバーから 開く / 保存 / 元に戻す / 検索 / 各種トグルが実行できる
- [ ] ステータスバーに 行:桁 / 文字コード / 改行コード / 選択文字数 が表示され、カーソル移動で更新される
- [ ] 文字コード欄・改行コード欄をクリックして変更でき、保存に反映される
- [ ] 行番号が表示される
- [ ] ウィンドウタイトルにファイル名と未保存マーク (`*`) が出る
- [ ] 右クリックでコンテキストメニューが出る
- [ ] **exe に独自アイコンが埋め込まれている** (エクスプローラで確認)
- [ ] **全キーバインドが `HACCEL` に集約されている** (`editor_input.cpp` に `if (ctrlDown && vkCode == ...)` の連鎖が残っていない)
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0
- [ ] 🎉 **M2 達成: アプリケーションとして成立**

---

## WI-08 — 設定システム

**目的:** ハードコードされた定数群を設定ファイル経由へ移す。**13 箇所で機能縮退の理由になってきた負債の返済。**

**前提:** WI-07

**参照:** `master_roadmap.md` §8.6.1 / `docs/issues/no_settings_system.md`

### 既に決まっている設計

- `core::Settings` — 保存先 `%APPDATA%\NeoMIFES\settings.json` (`platform::resolveAppDataDir()` が Phase 5c5 で実装済み)
- **形式は JSON (JSON5 ではない)。** roadmap U#7 は JSON5 を第一候補としていたが、[ADR-013](../decisions/ADR-013-json-library.md) で導入済みの nlohmann/json は JSON5 を解釈できず、かつ `core::SearchHistory` が既に素の JSON を採用した前例がある。**同じ判断を踏襲する**
- 初期スコープ: フォントファミリ / フォントサイズ / タブ幅 / タブをスペースで挿入 / 行番号表示 / ミニマップ表示 / 自動保存間隔 / テーマ名
- 設定ファイルが壊れている / 存在しない場合は**既定値で安全に起動する** (起動失敗させない)

### 🔴 本 WI で最も重要なこと

**「設定システムを作る」だけでは不十分。既存のハードコード定数を実際に移行することが完了条件である。**

移行必須の既知の箇所:

| 定数 | 現在の場所 | 問題 |
|---|---|---|
| `kTabWidth = 4` | `src/app/main.cpp:872` (関数内 `constexpr int`) | ハードコード。`master_roadmap.md` §7 は「`render_pipeline.cpp` 側にも複製し 2 箇所の手動同期が必要」と記録しているが、**着手時に `grep -rn "TabWidth\|tabWidth" --include=*.cpp --include=*.h src/` で現在の実体を必ず再確認すること** (レンダラ側のタブ展開が別名の定数か DirectWrite 既定に委ねられている可能性がある) |
| フォント関連 | `render_pipeline.cpp` | 変更不能 |
| `kMinimapWidthDips` / `kMinimapScaleDivisor` 他 | `render_pipeline.cpp` | 表示トグル不能 |
| 色定数 (`constexpr D2D1_COLOR_F k*Color`) | `render_pipeline.cpp` | WI-09 (テーマ) で移行 |

移行前に `grep -rn "constexpr.*k[A-Z]" src/ | grep -v test` で全体を洗い出すこと。

### DoD

- [ ] `%APPDATA%\NeoMIFES\settings.json` から読み書きできる
- [ ] **`kTabWidth` の二重定義が解消されている**
- [ ] 設定ファイルが無い / 壊れている場合に既定値で起動する
- [ ] フォント・タブ幅・行番号表示の変更が**再起動なしで反映される**
- [ ] 「読み込み → 変更 → 保存 → 再読み込み」のラウンドトリップ単体テストがある
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-09 — テーマ

**目的:** ダーク / ライト / ハイコントラストを切り替えられるようにする (要件定義書 §14 必須)。

**前提:** WI-08

**参照:** `master_roadmap.md` §8.6.3

### 既に決まっている設計

`render_pipeline.cpp` にハードコードされている色定数群を `render::Theme` 構造体経由へ移す。

**色は `D2D1::ColorF` ではなく `constexpr D2D1_COLOR_F` の集成体初期化で書かれている** (実在確認済み)。例:

```cpp
constexpr D2D1_COLOR_F kTextColor      = {220.0F / 255.0F, 220.0F / 255.0F, 220.0F / 255.0F, 1.0F};
constexpr D2D1_COLOR_F kSelectionColor = {  0.0F / 255.0F, 120.0F / 255.0F, 215.0F / 255.0F, 0.4F};
constexpr D2D1_COLOR_F kKeywordColor   = { 86.0F / 255.0F, 156.0F / 255.0F, 214.0F / 255.0F, 1.0F};
```

**着手時に `grep -n "constexpr D2D1_COLOR_F" src/render/src/render_pipeline.cpp` で全数を確認すること** (`D2D1::ColorF` で grep しても 0 件なので注意)。

移行対象 (既知): テキスト / 選択範囲 / マッチ / 現在マッチ / ブックマーク / フォールドマーカー / Keyword / Type / String / Number / Comment / Preprocessor / ミニマップ 4 種 / Breadcrumb 背景 / Indent guide / 背景 / キャレット

- テーマ切替時は全ブラシを作り直す (既存の `recreateDevice()` のリセット経路がそのまま使える)
- ハイコントラストは Windows のシステム設定 (`SystemParametersInfo(SPI_GETHIGHCONTRAST)`) を尊重して自動選択してもよい

### DoD

- [ ] ダーク / ライト / ハイコントラストを切り替えられ、設定に永続化される
- [ ] `render_pipeline.cpp` に `D2D1::ColorF` のハードコードが残っていない
- [ ] テーマ切替でデバイスロストが起きても正しく再構築される
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-10 — キーバインド設定 + プリセット

**目的:** キーバインドをユーザーが変更でき、秀丸 / サクラ / VSCode 風のプリセットを選べるようにする。

**前提:** WI-07 (`HACCEL` 集約)、WI-08 (設定システム)

**参照:** `master_roadmap.md` §8.6.2 / §13.1

### 既に決まっている設計

- WI-07 で `HACCEL` へ集約したキーバインドを、設定ファイル (`keybindings.json`) から構築できるようにする
- プリセット: NeoMIFES 標準 / 秀丸 / サクラ / VSCode の 4 種を同梱
- コマンド ID は WI-07 で定義済みのものをそのまま使う (コマンドパレットの `CommandDescriptor::id` と揃えるのが自然)

**秀丸/サクラのキーバインドは記憶で書かない。** 公開されているキーバインド一覧を確認するか、確認できない項目は同梱せず「未対応」として空にする。誤ったプリセットは無いより悪い。

### DoD

- [ ] キーバインドを設定ファイルで変更でき、再起動後も保持される
- [ ] 4 プリセットを切り替えられる
- [ ] 競合するキーバインドを設定したとき、警告するか後勝ちにするかが定義されている
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-11 — 自動保存 / バックアップ / クラッシュ復旧 / 最近開いたファイル

**目的:** 要件定義書 §6・§15 の必須項目を満たす。

**前提:** WI-08

**参照:** `master_roadmap.md` §8.6.4

### 既に決まっている設計

- **自動保存:** N 秒ごと (設定可能、既定 60 秒) およびフォーカス喪失時に `%APPDATA%\NeoMIFES\autosave\<hash>.tmp` へ書き出す。**元ファイルは上書きしない**
- **バックアップ:** 保存時に元ファイルを `<name>.bak` として残す (設定でオフ可能)。WI-01 の `ReplaceFileW` は第 3 引数でバックアップファイル名を取れるため、そのまま使える
- **クラッシュ復旧:** 起動時に autosave ディレクトリを走査し、対応する正規ファイルより新しい autosave があれば復旧を提案する
- **最近開いたファイル:** `%APPDATA%\NeoMIFES\recent.json` (MRU 上限 20)。メニューの「ファイル」に表示。Jump List (`ICustomDestinationList`) 対応は任意 (roadmap §21.7)

### DoD

- [ ] 編集後 N 秒放置すると autosave ファイルが生成される
- [ ] 保存時に `.bak` が生成される (設定オフで生成されない)
- [ ] プロセスを強制終了 → 再起動 で復旧が提案され、承諾すると内容が戻る
- [ ] 最近開いたファイルがメニューに出て、クリックで開ける
- [ ] **autosave が元ファイルを破壊しないことをテストで保証している**
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

---

## WI-12 — 基本編集の穴埋め 🎉 M3

**目的:** 未実装のまま残っている基本操作を埋める。

**前提:** WI-07

**参照:** `master_roadmap.md` §8.6.5

### 実装するもの

| 機能 | キー | 備考 |
|---|---|---|
| 全選択 | `Ctrl+A` | 未実装 |
| 自動インデント | (改行時) | 前行のインデントを継承。**60 機能マトリクスが Phase 4b8 に割り当てていたが実装されないまま「完了」宣言されていた項目** |
| 行複製 | `Ctrl+D` | |
| 行移動 | `Alt+↑` / `Alt+↓` | |
| 行削除 | `Ctrl+Shift+K` | |

いずれも既存の `core::MultiCursorEditCommand` / `core::ReplaceAllCommand` の上に構築でき、新しいコマンド基盤は不要。

### DoD

- [ ] 上記 5 機能が複数カーソル状態でも正しく動く
- [ ] いずれも Undo 1 ステップで戻る
- [ ] 自動インデントはタブ/スペース設定 (WI-08) を尊重する
- [ ] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0
- [ ] 🎉 **M3 達成**

---

## WI-13 — MVP 出荷判定 🎉 M4

**目的:** 「秀丸/サクラの代替として実用に耐える」状態を確認し、初回出荷する。

**前提:** WI-01 〜 WI-12 全て

**参照:** `master_roadmap.md` §12.4

### 実施内容

§6 の MVP 出荷判定チェックリストを 1 項目ずつ実機で確認する。**未達項目があれば出荷せず、その項目を新規 WI として §3 へ追加してから戻る。**

加えて:
- Authenticode 署名 + Portable Zip の配布物を作る
- 最低限のユーザーマニュアル (キーバインドリファレンス) を `docs/user/` に用意
- 8 時間ソークテスト (クラッシュ 0)

### DoD

§6 の全項目にチェックが入ること。

---

## WI-14 〜 WI-17 — Phase 10 / 11 / 9 / 12

**WI-13 完了まで着手禁止。**

着手時は `master_roadmap.md` の該当章を読み、**本書 §5 と同じ形式で WI を切り直してから**始めること (章をそのまま実装しようとすると 1 セッションに収まらない)。

| WI | 内容 | roadmap 章 | 目安 |
|---|---|---|---|
| WI-14 | Phase 10 — ログ解析 / CSV / JSON-XML Tree | §10 | 3 領域 × 各 3〜5 サブ WI |
| WI-15 | Phase 11 — Git 統合 / LSP / マクロ | §11 | 3 領域 × 各 3〜6 サブ WI |
| WI-16 | Phase 9 — AI プラグイン | §9 | 4〜6 サブ WI |
| WI-17 | Phase 12 — 総合品質保証・正式出荷 | §12 | §12.3 の 22 項目 |

**順序の根拠:**
- **Phase 10 が先** — roadmap §1.5 が「本ソフト最大の差別化点」と位置づけ、外部サービスに依存せず陳腐化しない
- **Phase 9 (AI) が最後** — CLAUDE.md が「エディタ本体は AI 無しでも 100% 動作しなければならない」と定めており、本体完成後に載せるのが筋。加えて外部 API 依存で陳腐化が速い

---

# 6. MVP 出荷判定チェックリスト (WI-13)

- [ ] ファイルを 開く / 編集 / 保存 / 別名保存 が全て動作する
- [ ] 日本語 IME でインライン変換が正しく表示される (**実機手動確認必須**)
- [ ] 未保存で終了しようとすると警告が出る
- [ ] 10 個のファイルをタブで開いて相互に切り替えられる
- [ ] 設定でフォント・タブ幅・テーマを変更でき、再起動後も保持される
- [ ] 長い行の右端まで横スクロールで到達できる
- [ ] 起動時間 ≤ 300ms (Release 実測)
- [ ] 60fps スクロール維持 (`--measure-frame`)
- [ ] 10GB ファイルを開ける
- [ ] クラッシュ 0 (8 時間ソーク)
- [ ] ASan / UBSan クラッシュ 0、clang-tidy 新規指摘 0
- [ ] Authenticode 署名 + Portable Zip 配布
- [ ] **開発者が日常的に NeoMIFES で NeoMIFES を開発している**
- [ ] ユーザーマニュアル (キーバインドリファレンス) を同梱

**Phase 12' で意図的にスコープ外とするもの (WI-17 へ):** NVDA/JAWS 対応、WCAG 2.2 AA、中韓 IME、RTL、fuzz 24 時間、MSIX、自動更新、SBOM、テレメトリ。

---

# 7. よくある状況への対処

| 状況 | どうするか |
|---|---|
| **本書と `git log` が食い違う** | `git log` を信じる。本書 §3 を修正してから作業を始める |
| **WI が大きすぎる** | `WI-05a` / `WI-05b` に分割してよい。§3 のチェックリストも更新する |
| **WI の「既に決まっている設計」が実コードと食い違う** | **実コードを信じる。** 本書を修正し、なぜ食い違ったかを 1 行残す |
| **設計判断が必要だが決められない** | 実コードを読んで決める → それでも無理なら選択肢と推奨案を添えてユーザーに聞く |
| **DoD を満たせない** | **「未達」と正直に記録して次へ進む。** 達成したふりをしない。本プロジェクトは Phase 7q/7t/7u で実際にそうしてきた |
| **「◯◯が無いから簡略版にする」と考えた** | その ◯◯ を `docs/issues/` に起票する。3 回目なら ◯◯ の実装を次 WI に格上げする |
| **ビルドが壊れた / CI が落ちた** | Windows/MSVC/clang-tidy 特有の落とし穴はメモリの `reference_windows_cpp_ci_gotchas.md` に 13 種集約済み。まずそれを見る |
| **エンジン層を直したくなった** | ほぼ間違いなく不要。触る前にユーザーへ理由を説明して確認する |
| **性能を改善したくなった** | ベンチマークで劣化を実測してから。憶測で最適化しない (CLAUDE.md ルール 10)。過去に 4 フェーズ費やして未達に終わった前例がある |

---

# 8. 本書の更新運用

- **WI を完了したら §3 のチェックリストと該当 WI の「実装後の確定事項」を必ず更新する。** これを怠ると次のセッションが迷う
- **新しい WI を追加したら §3 にも行を足す**
- 本書と `master_roadmap.md` が矛盾したら、**実行順は本書、機能仕様は roadmap** が正
- WI-13 (MVP 出荷) 到達時に本書を v2.0 へ改訂し、WI-14 以降を詳細化する

---

*本書は 2026-08-04 の中間レビューを受けて発行。コンテキストを持たないセッションが単独で製造を継続できることを設計目標とする。*
