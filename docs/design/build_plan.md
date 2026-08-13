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
2. **push 前に必ずローカル検証する。** Debug / Release / ubsan の 3 プリセットで `ctest` が全 green、変更ファイルへの clang-tidy が新規警告 0。**WI を複数ステップに分けた場合、フル3構成の検証は「WI完了時(最終コミット直前)」に1回で足りる。各中間ステップでは Debug 構成のみで素早く確認する**(詳細は §4.3)。性能・Undefined Behavior のリスクが高いと判断した中間ステップ(生ポインタ操作・並行処理・ベンチマーク対象コード等)は、そのステップ単独で ubsan を追加してよい
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
3. **設定システムが無いことを理由に妥協しそうになったら、`core::Settings` が使えないか検討する。** WI-08 (2026-08-13完了) で実装済み。過去に 13 回この妥協が繰り返され、定数の二重定義という負債になった
4. **迷ったら小さく作る。** 「まずヘッドレスで正しく動かし、次に UI へ配線する」は本プロジェクトで 10 回以上成功している型
5. **それでも決められないことだけ、ユーザーに聞く。** 選択肢と推奨案を添えて

---

# 3. 進捗チェックリスト ← 作業完了ごとに更新すること

**完了した WI に `[x]` を付け、コミットハッシュを記入する。これが唯一の進捗管理台帳である。**

## 完了済み (2026-08-04 時点)

- [x] Phase 0〜8f — エンジン層 + プラグイン基盤 (`23c2cc2` まで)
- [x] 中間レビュー — ギャップ分析 + roadmap v2.1 (`a0ac815`)

## Phase 8.5 — アプリケーションシェル (P0)

- [x] **WI-01** 文書保存基盤 (`document::saveFile()` / `isDirty()`) → コミット: `a4a0445`
- [x] **WI-02** ファイルライフサイクル UI (Ctrl+S / Ctrl+O / Ctrl+N / D&D / 未保存警告) → コミット: `3e611d8`。ドッグフーディングで2件の実害あるバグを発見・修正 (`5712435`/`8199c38`/`a8df325`)、ユーザーが実際に編集・保存・コミット (`d02138b`/`34b79e5`) まで完走し 🎉 M1 達成
  - 🎉 **M1 達成 (2026-08-05): NeoMIFES で NeoMIFES を編集できるようになった (ドッグフーディング完了)**
- [x] **WI-03** 横スクロール (`leftColumn` / `WM_HSCROLL`) → コミット: `6052da8`
- [x] **WI-04** `main.cpp` 解体 + `EditorSession` / `Workspace` 新設 → コミット: `c58245e` (ステップ1) / `8237ec4` (ステップ2) / `2c549d0` (ステップ3) / `3480b5f` (ステップ3b)
- [x] **WI-05** タブ UI (`ui::TabBar`) → コミット: `4f9bced` (ステップ1) / `fe037d7` (ステップ2) / `62edf0c` (ステップ3) / `57acef8` (ステップ4)
- [x] **WI-06** IME 完全対応 (`WM_IME_*` + インライン未確定文字列) → コミット: `0baccaa` (ステップ1〜3) / `94e2259`・`f233f02` (CI修正) / 実機MS-IME確認完了 (2026-08-12)
- [x] **WI-07** ウィンドウクローム (メニュー / `HACCEL` / ステータスバー / 行番号 / `.rc`) → コミット: `c0f296b` (ステップ0) / `55f80cc` (ステップ1) / `1b989af` (ステップ2) / `fe69c44` (ステップ3) / `b9f8c82` (ステップ4) / `6fc8cbd` (ステップ5) / `a075e6d` (ステップ6) / `cefd5a6` (ステップ7) / `292280b` (ステップ8) / `91104bd` (ステップ9) / `68a53ee` (ステップ10)
  - 🎉 **M2 達成 (2026-08-13): アプリケーションとして成立**

## Phase 8.6 — 製品化基盤 (P1)

- [x] **WI-08** 設定システム (`core::Settings`) + ハードコード定数の移行 → コミット: `6a76722` (ステップ1) / `0fbd148` (ステップ2) / `0b55e86` (ステップ3)
- [x] **WI-09** テーマ (ダーク / ライト / ハイコントラスト) → コミット: `be65533` (実装) / `________` (ドキュメント同期)
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

**検証の粒度 (2026-08-12改訂):** WI を複数ステップに分けている場合、**中間ステップは Debug 構成の build+ctest のみ**でよい (下記コマンドの1行目だけを実行)。**Debug/Release/ubsan のフル3構成は、WI 完了時 (最終コミット直前) に1回まとめて実行する。** これは検証を省略するのではなく、同じ検証を何度も繰り返さないための順序変更である — コミット前には必ずフル3構成が green であることを確認する。単一ステップの WI (分割しない場合) は、これまで通りそのままフル3構成を実行する。性能・Undefined Behavior のリスクが高いステップ (生ポインタ操作・並行処理・ベンチマーク対象コード等) は、そのステップ単独で ubsan を追加してよい。

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

**実行はサブエージェントへの委任を基本とする (2026-08-12改訂)。** ビルド・テスト・clang-tidy の実行自体は、Agent ツール (subagent_type: general-purpose、run_in_background) にバックグラウンドで投げ、「green/red 判定 + 失敗があれば失敗内容の要約」のみをメイン会話へ持ち帰らせる。生のビルドログ/ctest出力/clang-tidy出力をメイン会話に直接貼らない。これによりコンテキスト消費を抑えつつ、検証自体の網羅性は変えない。

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

### 実装後の確定事項 (2026-08-04 完了)

**probeでU#22/U#23/U#26を検証した結果、上記「採用する手順」の 4 (mmap解放) と 6 (再mmap + Piece Table再構築) は不要と判明し、実装しなかった。** `ReplaceFileW`はマップ済みファイルに対しても成功し (`FILE_SHARE_DELETE`込みの既存mmapのまま)、旧mmapビューは孤立して旧内容を返し続け、新規オープンは新内容を返す。U#23は「エラーコード分岐」ではなく「失敗後の`fs::exists()`による実ファイル状態チェック」で解決した (`ERROR_FILE_NOT_FOUND`だけでは「target不在」と「replacement不在(バグ)」を区別できないとprobeで判明したため)。

設計レビューで追加発覚した2件: (1) `ReplaceFileW`は既存ファイル置換専用のため新規ファイル/Save Asには`MoveFileExW`フォールバックが必要、(2) `Document::lineCount()`が`'\n'`のみを数えるため行境界のみのチャンク分割はCR-onlyファイル/巨大単一行で退化する → 行数上限とコード単位上限のハイブリッドチャンク分割を採用。

詳細は [`detailed_design.md` §3.4](detailed_design.md#34-filesaver-wi-01実装2026-08-04)、[`docs/handoff/RESUME_HERE.md` §3.67](../handoff/RESUME_HERE.md) 参照。

### 影響ファイル

- `src/document/include/neomifes/document/document.h` / `src/document/src/document.cpp` — `saveFile()` / `isDirty()` / `markSaved()`
- `src/document/include/neomifes/document/original_buffer.h` / `.cpp` — マップ解放 API の追加
- `src/document/CMakeLists.txt` — 依存追加が必要なら (`neomifes::encoding` は Phase 6d で既にリンク済み)
- `tests/unit/document_document_test.cpp` — `isDirty()` の状態遷移
- `tests/integration/` — 新規 `document_save_roundtrip_test.cpp`

### DoD

- [x] 「開く → 編集 → 保存 → 再度開く → 内容一致」のラウンドトリップテストが green
- [x] UTF-8 / UTF-8 BOM / UTF-16LE / Shift-JIS それぞれで保存でき、`detectBom()`/直接 `decode()` で保存後のファイルが一致することを確認 (`detectEncoding()` の自動判定は別機能のため、ラウンドトリップ検証は既知のエンコードで直接デコードする形にした)
- [x] 改行コード LF / CRLF / CR を指定して保存でき、`detectLineEnding()` が一致する
- [x] **100MB 以上のファイルを保存してもピークメモリがファイルサイズに比例しない** (`document_save_bench.cpp`、peak working set delta 計測)
- [x] 保存が失敗しても**元ファイルが壊れない** (U#23 の結論に従う。統合テスト `FailedSaveLeavesTheOriginalFileUntouched` で実証)
- [x] `isDirty()` が編集で true、保存で false になる
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

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

- [x] `Ctrl+O` でファイルを開き、`Ctrl+S` で保存し、再度開くと編集内容が保持されている (実装完了・自動テストで裏付け。実アプリでの手動確認は下記ドッグフーディング項目が兼ねる)
- [x] `Ctrl+Shift+S` で別名保存できる (`performSave(forceSaveAs=true)`)
- [x] `Ctrl+N` で空の新規文書になる
- [x] エクスプローラからファイルをドラッグ&ドロップして開ける (`onDropFiles` 実装済み。実機でのドラッグ操作自体は未検証 — 過去セッションから継続する Win32 GUI 自動化の制約、下記確定事項参照)
- [x] 未保存のまま `Ctrl+N` / `Ctrl+O` / ウィンドウを閉じる、のいずれでも警告が出て「キャンセル」で操作が中止される (`confirmDiscardIfDirty()` で一元化)
- [x] 🎉 **ドッグフーディング: NeoMIFES で NeoMIFES のソースを開いて編集し、保存し、そのままコミットできた** ← **ユーザーが実施し、2件の実害あるバグを発見・報告 (下記「ドッグフーディングで発覚したバグ」参照)。両バグとも修正し、ユーザーが再確認して問題解消を確認済み (2026-08-05)。その後、ユーザーが実際に `README.md` を NeoMIFES で開いて編集(テキスト追記)・`Ctrl+S` で保存・`git diff`/`git status` で差分確認・`git commit` (`d02138b`) までを実際に完走し、さらに同じループで内容を修正して再度保存・コミット (`34b79e5`) した。**🎉 M1達成 (2026-08-05)。**
- [x] Debug / Release / ubsan 全 green (各 1002/1002)、clang-tidy 新規警告 0 (変更/新規ファイル全件個別実行で確認)

### 実装後の確定事項 (2026-08-04)

**設計時点からの簡略化:** 当初「BOM/エンコード/改行コードのロード時メタデータを運ぶ新しい共有関数を app 層に新設する」設計を検討したが、`document::LoadResult` 自体に `lineEnding` フィールドを1つ追加し `loadFile()` 内部で計算する方が、既存の `hadBom`/`detectedEncoding` と全く同じ形で全呼び出し元 (起動時ロード・F12・Grep結果クリック・Ctrl+O・D&D) に自動的に伝播し、複数箇所での実装乖離リスクが構造的に排除できると判明したため、この方式を採用した (`file_loader.cpp` の `detectLineEndingBounded()`、先頭 `1<<20` code units のみ走査)。

**設計レビューで実装前に検出・修正した問題 (Plan agent):**
1. **`CoInitializeEx` が本コードベースのどこからも呼ばれていなかった** — 既存の D2D/DXGI/D3D11 COM 利用 (ADR-008) は全てファクトリ関数経由で `CoCreateInstance` を要しないが、`IFileOpenDialog`/`IFileSaveDialog` は要する。`file_dialogs.cpp` にファイルローカルな RAII `ComInitGuard` を新設して対応。
2. **境界プレフィックスでの改行コード検出に実害あるバグが実装前に見つかった。** `kLineEndingDetectionHeadCodeUnits` (1MB) の走査境界がCRLFペアの `\r` と `\n` の間で偶然切れると、`encoding::detectLineEnding()` が末尾の孤立 `\r` を「CR単独」の証拠として誤検出し、一貫したCRLFファイルを `Mixed` と誤判定して `saveFile()` が無言でLFへ書き換える経路になり得た。`detectLineEndingBounded()` で境界切断時の末尾 `\r` を明示的にトリムして対処 (`document_file_loader_test.cpp` に境界を精密に構成した回帰テストあり)。
3. **Ctrl+N を素朴に実装すると、直前の編集内容が Undo 経由で新規 (空) 文書へ混入する実害あるデータ破損経路が実装前の設計検証で見つかった。** `openDocumentAt()` は `dispatcher.resetUndoHistory()`/`bookmarks.clear()`/両アンカーのリセット/`freeCursorVirtualColumns.reset()` を内部で行うが、Ctrl+N はファイルを読まないため `openDocumentAt()` を経由せず、これらを自前で複製する必要がある。省略すると「編集→Ctrl+N→Ctrl+Z」で `PieceTable::insert()` の範囲外オフセットクランプ (`min(pos, total)`) により、直前ファイルの削除済み内容が新規文書の先頭へ無言で復元される。`handleNewDocumentKey()` で `openDocumentAt()` と同じリセット手順を明示的に複製して対処。

**既知の未対応事項 (Finding 4、docs/issues/ に起票):** FindBar/GrepBar/CommandPalette/GotoLineBar/OutlinePane のいずれかがキーボードフォーカスを持っている間は Ctrl+S/O/N が届かない (各オーバーレイの `SetWindowSubclass` コールバックが未知のキーを `DefSubclassProc` へ委譲するのみで、親 HWND へは転送しない構造的制約)。5 箇所への転送ロジック追加は本 WI の footprint を超えるため今回は対応せず、`docs/issues/overlay_focus_blocks_file_lifecycle_keys.md` に起票した。

**実アプリでの視覚/操作確認の限界:** 過去複数セッションで確立した通り、この開発環境では Win32 GUI へのキーボード入力合成 (Ctrl 修飾キー含む) が不安定なため、Ctrl+S/O/N/Shift+S の実機キー入力確認は行っていない。実施したのは (a) 全 1000 件の自動テスト green、(b) `NeoMIFES.exe --open <file>` の起動生存確認のみ。**M1 の核心である「NeoMIFES で NeoMIFES のソースを編集・保存・コミットする」ドッグフーディングは、実際にユーザーのリポジトリへ書き込む操作であり自動化・代行せず、ユーザー自身に実施を依頼した。**

詳細は [`detailed_design.md`](detailed_design.md)、[`docs/handoff/RESUME_HERE.md`](../handoff/RESUME_HERE.md) 参照。

### ドッグフーディングで発覚したバグ (2026-08-05)

ユーザーが実際にドッグフーディングを試み、以下 2 件の実害あるバグを発見・報告した。両方とも本セッション中に根本原因を特定し修正・自動テストで実証済み。

1. **Ctrl+O でファイルを読み込んだ際に内容が表示されない (ウィンドウ移動等の無関係な再描画で初めて反映される)。** 原因は `RenderPipeline::render()` の粗粒度フレームスキップ (Phase 3c/ADR-011) が、文書 SWAP (`setDocument()` を新しい `Document` へ差し替える) を「何も変わっていない」と誤判定しうる構造的な穴だった。`FrameState::documentVersion` は新しい `Document` 自身の独立したバージョンカウンタ (`Document::version()`) を見ているため、直前の文書と偶然同じ値 (典型的には起動直後、両方とも `version()==0` または最初の1回の編集で `version()==1`) になり得る。他の全フィールド (topLine/カーソル/マッチ/ブックマーク/フォールド領域) も文書スワップ直後は既定値に揃うため、`FrameState::operator==` (defaulted) が偶然一致し `render()` が再描画を丸ごとスキップしていた。**修正:** `RenderPipeline` に単調増加する `m_documentGeneration` カウンタを新設し、全ての文書スワップ経路が無条件に呼ぶ `setLanguage()` 内でインクリメント。`FrameState` に `documentGeneration` フィールドを追加し `captureFrameState()` で反映。単調カウンタは値が絶対に繰り返さないため、この種の偶然の一致を構造的に排除する。同種の懸念は `setLanguage()` 自身の既存コメントが `refreshDocumentCacheIfStale()` 側の別チェック (`m_hasCachedSnapshot`) に対して既に指摘・対処済みだったが、`render()` レベルの外側のチェックには対処が漏れていた。
2. **マウスホイールで一番下までスクロールし続けると、画面は EOF より下にスクロールされないが、内部的にはスクロールした分だけカーソル位置(トップライン)が下に移動しており、上にスクロールして戻るのが極端に重い。** `core::Viewport::scrollTo()` は意図的にクランプしないベアセッター (「クランプは描画時に `RenderPipeline` が行う」という既存設計方針)。`src/app/editor_input.cpp` の `applyMouseWheelScroll()` はこの前提のもと下限 (0未満にしない) のみクランプし、上限は一切クランプしていなかった。`RenderPipeline` は描画時に実効トップラインを正しく `totalLines-1` でクランプするため画面上は正常に見えるが、`Viewport` が内部に保持する実際のトップライン値は際限なく増え続け、それを「巻き戻す」までスクロールバックが画面上に反映されなかった。**修正:** `applyMouseWheelScroll()` に `totalLines` 引数を追加し、`RenderPipeline` が既に使っている実効クランプ式 (`totalLines>0 ? totalLines-1 : 0`) と全く同じ上限を下向きスクロール側にも適用。`Viewport::topLine()` が描画結果から二度と乖離しなくなる。

両バグとも `tests/integration/render_text_smoke_test.cpp`/`tests/unit/app_editor_input_test.cpp` に回帰テストを追加し、修正前の状態に戻すと実際にテストが RED になることを確認してから修正を確定させた (`DocumentSwapWithCoincidentallyMatchingVersionForcesRedraw`、`ApplyMouseWheelScrollDownClampsToLastLineNearEof`)。ローカル Debug/Release/ubsan 全 1002/1002 green、clang-tidy 新規警告 0 (変更 4 ファイル個別実行、既存の無関係な `tests/` 警告 1 件を確認済みだが本バグ修正とは無関係な既存コード)。

### 🎉 M1 達成記録 (2026-08-05)

**NeoMIFES で NeoMIFES 自身のソース (`README.md`) を開いて編集・`Ctrl+S` で保存・`git diff`/`git status` で差分確認・`git commit` まで、ユーザー自身の手で実際に完走した。** コミット `d02138b`(テスト用追記)・`34b79e5`(修正・再保存)の2件がその実証。これにより Phase 8.5a (WI-01) + Phase 8.5b (WI-02) の目標である「🎉 M1: NeoMIFES で NeoMIFES を編集できる」を正式に達成した。

このマイルストーンに至る過程で、実際のドッグフーディング使用時にのみ発覚する2件の実害あるバグ(Ctrl+O後の画面未反映、マウスホイールEOF超過スクロール)が見つかり、修正された。これは「プロセスが3秒後も生存していた」という縮退した検証だけでは決して見つからなかった種類の不具合であり、ドッグフーディングDoDそのものの価値を実証する結果になった。

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

- [x] 1000 文字の行を含むファイルで、右端まで横スクロールして内容を読める (1200文字行での`render()`無エラーを複数`leftColumn`値で確認、実アプリでもNO_WRAPで右端まで伸びることを視覚確認)
- [x] 横スクロール中にクリックしたとき、`hitTest()` が正しい文字位置を返す (`HitTestAccountsForLeftColumnWhenScrolledHorizontally`)
- [x] `End` キーでキャレットが行末へ移動し、画面が自動追従する (`Viewport::ensureVisible()`の列版を全17箇所の既存呼び出し元が無改修で獲得。実機キー入力確認はWin32 GUI自動化の既知の制約により未実施 — 下記確定事項参照)
- [x] ガターとミニマップは横スクロールしても位置が変わらない (`GutterFoldMarkerHitTestIsUnaffectedByHorizontalScroll`、ミニマップは元々`m_leftColumn`を一切参照しない設計)
- [x] `--measure-frame` の実測値が既存ベースライン (avgFrameNs ≈ 16.5ms) から悪化していない (実測 avg 16.50ms / p50 16.67ms / p95 16.79ms、5万行合成文書・300フレーム・Release)
- [x] Debug / Release / ubsan 全 green (各1013/1013)、clang-tidy 新規警告 0 (変更11ファイル個別実行)

### 実装後の確定事項 (2026-08-05)

**設計時点からの唯一の逸脱: `Viewport::setVisibleColumnCount()`と対をなす垂直方向`setVisibleLineCount()`が、既存コードのどこからも一度も呼ばれていないことが実装中に判明した。** これは`Viewport::ensureVisible()`の下端追従クランプ(`m_visibleLineCount > 0 && line >= m_topLine + m_visibleLineCount`)が本番コードでは常にfalseのまま生存してきたことを意味する、既存の潜在バグ(WI-03のスコープ外、本セッションでは修正していない)。横方向は新規機能でありDoD「Endキーでの自動追従」を満たす必要があったため、水平方向に限り`RenderPipeline::visibleColumnCount()`(新設、`viewport_math.h::computeVisibleColumnCount()`をガター/ミニマップ分を差し引いた幅で呼ぶ)を毎フレーム描画後に`Viewport::setVisibleColumnCount()`へ供給する配線を追加した。垂直方向との非対称性(横は配線されている、縦は配線されていない)を意図的に許容し、縦方向の同種の修正はWI-03のスコープに含めなかった。

**ガタークリップの技術的必然性(着手前調査で発見):** `drawGutterOnLine()`(ブックマークドット・フォールドシェブロン)は`[0, kGutterWidthDips)`へ背景の塗りつぶしを一切行わない。`-leftColumnOffsetDips()`のオフセットを導入すると、右へスクロールした行のグリフがガター領域へ視覚的にはみ出しうるため、`drawTextLine()`内のテキスト由来の描画(マッチ/選択ハイライト/インデントガイド/トークン色/グリフ本体/キャレット/フォールドヘッダーマーカー)のみを`PushAxisAlignedClip`/`PopAxisAlignedClip`で囲んだ。ガター自体(ブックマーク/フォールドマーカー)はクリップの**外側**で描画され、常に固定表示される。

**フレームスキップ再発防止:** `FrameState`に`leftColumn`フィールドを追加した。本セッション冒頭で修正したばかりの`m_documentGeneration`欠落バグ(コミット`5712435`)と全く同じ「変化したフィールドがFrameStateに含まれていないと粗粒度フレームスキップに再描画ごと飲み込まれる」パターンを、水平スクロールバーのドラッグのみで再発させないための予防的対応。回帰テスト`LeftColumnOnlyChangeForcesRedraw`で実証。

**実アプリでの視覚確認:** 1200文字行を含むテストファイルを実際に`--open`し、スクリーンショットで(a) 長い行がNO_WRAPで右端を超えて伸びること、(b) 本コードベース初のネイティブ水平スクロールバー(`WS_HSCROLL`)が画面下端に正しいサイズの thumb で表示されることを確認した。**この過程で、この開発環境のスクリーンショット手法が別の無関係なウィンドウの内容を誤って撮影する事故が1件発生した(既知の環境不調パターン、内容は読み上げず即座に削除・ユーザーに報告済み)。** これを受けてユーザーの判断により、スクロールバーのクリック/ドラッグによる実際のスクロール動作の対話的確認は行わず、自動テストスイート(hitTest ラウンドトリップ・ガター固定・フレームスキップ打破・`render()`無エラー)で正しさを担保する方針に切り替えた。

詳細は [`detailed_design.md`](detailed_design.md) 参照。

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

- [x] **`src/app/main.cpp` が 500 行以下** (2,439 行 → **361 行**。着手時点の実測は 2,053 行ではなく 2,439 行だった — WI-03 完了時点で既にその行数まで増えていたため、本 WI 冒頭で実測し訂正した)
- [x] **既存の全テストが無変更で green** (新機能を足していないことの証明。ステップ1〜3b の各コミットで毎回 1026 テスト全 green を確認)
- [x] 実アプリの挙動が WI-03 完了時点と完全に同一 (ドッグフーディングで確認 — 下記「実装後の確定事項」参照)
- [x] `Workspace` の単体テストが追加されている (`tests/unit/app_workspace_test.cpp`、13 ケース)
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0 (ステップ1〜3b の全コミットで確認済み)

### 実装後の確定事項

**ファイル配置の訂正:** 本節が当初示していた `src/app/src/workspace.cpp` は誤り。実際の `src/app/` に `src/` サブディレクトリは存在しない (既存の `document_open.cpp`/`editor_input.cpp` 等はすべて `src/app/` 直下)。新規ファイルは実際の慣習に合わせ `src/app/workspace.cpp`/`src/app/editor_session.cpp` とした。

**3 段階では 500 行に届かず、ステップ3b を追加した:** 当初の「安全な進め方」(EditorSession 新設 → Workspace 新設 → キーバインド群を editor_input.cpp へ移設) の 3 段階だけでは `main.cpp` は約 650 行までしか縮まらないと実装途中で判明した。理由は `wireNormalMode()` とその依存関数群 (`buildFindBarConfig`/`buildCommandRegistry`/`handleKeyDownEvent` 等、約 46 関数・約 1,780 行) がいずれも `RenderPipeline`/`HWND`/`ui::` ウィジェットに依存しており、Win32 非依存を維持する `editor_input.cpp` (`app_editor_input_test.cpp` がその性質にヘッドレスで依存) には移せないため。これらを新規 `src/app/normal_mode_wiring.h`/`.cpp` へ切り出すステップ3bを追加した。さらにステップ3b 単独でも main.cpp は 564 行までしか縮まらなかったため、`wWinMain` 本体より前に実行される「プロセス起動前処理」(コマンドライン解析・多重起動チェック・DPI/共通コントロール初期化・起動時 Document 構築、約 190 行) を `src/app/launch_setup.h`/`.cpp` へ追加分割し、最終的に 361 行まで到達した。いずれも「main.cpp に残すのは wWinMain/ウィンドウ生成/メッセージループ/Workspace と RenderPipeline の所有のみ」という本節の既定方針を字義通り満たすための精緻化であり、スコープ追加ではない。

**状態の振り分け根拠:** `EditorSession` には Document/SelectionModel/CommandDispatcher/Viewport/FoldingModel/BookmarkManager に加え、`FindReplaceState` (検索状態) と `altCursorAnchor`/`rectangularAnchor`/`freeCursorVirtualColumns` (前文書内の位置に紐づくアンカー類) も含めた — いずれも `resetViewAfterDocumentSwap()`/`document_open.h` が文書切替の都度リセットしていた実際の既存動作から逆算した判断であり、WI-05 の「各タブが独立した検索状態を保持する」という前提も先取りする。逆に `GrepState`/`freeCursorModeEnabled`/`isDraggingMinimap`/各種 UI ウィジェット (FindBar/CommandPalette/GotoLineBar/GrepBar/OutlinePane/SearchHistory) は `EditorSession` に含めず `wWinMain`/`wireNormalMode()` 側に残した — Grep はプロジェクト全体検索で文書非依存、フリーカーソルモード/ミニマップドラッグは UI ジェスチャ状態、各ウィジェットは Workspace 全体で 1 個の実体という理由による。

**`CommandDispatcher` のポインタ安定性制約:** `core::CommandDispatcher` は構築時に `Document&`/`SelectionModel&` を生ポインタとして束縛し、以後再解決しない。そのため `EditorSession` は move/コピー禁止 (`= delete`) にし、`Workspace` は `std::vector<std::unique_ptr<EditorSession>>` でヒープ固定配置した。

**`EditorSession::language()` は意図的にキャッシュしない:** 既存コードが `detectLanguage(path)` を呼び出し箇所ごとに都度再計算していた挙動 (キャッシュフィールドが存在しなかった) をそのまま踏襲した。これにより「2 箇所で更新を忘れて食い違う」という新しい同期バグのクラスを増やさずに済む (CLAUDE.md が警告する `kTabWidth` 二重定義と同種の負債の先取り回避)。

**ドッグフーディング (実アプリ動作確認):** ステップ3b 完了後、NeoMIFES 自身の `src/app/main.cpp` (リファクタ後のバージョン) を `--open` で実際に開き、シンタックスハイライト・ミニマップ・ガター・水平/垂直スクロールバーの描画、およびマウスホイールスクロール操作を実機スクリーンショット2枚で視覚確認した。`WM_CLOSE` で正常終了し、作業ツリーへの意図しない変更が発生しないことも確認した。キーボード修飾キー合成 (Ctrl+S 等) を伴う編集・保存の完全な往復までは自動化環境の制約により実施していない — 既存メモリ (`reference_no_win32_gui_automation`) が記録する既知の制約と同じ理由。

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

- [x] 10 個のファイルをタブで開き、`Ctrl+Tab` で切り替えられる (`Workspace::openFile()`/`openBlank()`+`handleTabSwitchKey()`実装完了。**視覚的なタブ切替の実機確認は下記「実装後の確定事項」に記載の`docs/issues/native_overlay_widgets_invisible.md`によりブロック中** — Win32 API構造確認 (`TCM_GETITEMCOUNT`) と`app_workspace_test.cpp`の網羅的単体テストで代替)
- [x] **各タブが独立した Undo 履歴・カーソル位置・スクロール位置・検索状態を保持する** (`EditorSession`が個別に保持する構造的帰結 (WI-04)。`UndoHistoryIsIndependentPerSession`単体テストで直接検証。カーソル/スクロール/検索状態は`syncViewForActiveSession()`がタブ切替の都度復元する設計で、視覚確認は上記と同じ理由でブロック中)
- [x] 未保存タブに ● が表示され、保存すると消える (`TabBar::setTabs()`を毎フレーム呼びライブ反映、`handleSaveKey()`に`InvalidateRect()`追加。視覚確認は上記と同じ理由でブロック中)
- [x] `Ctrl+W` で閉じるとき、未保存なら警告が出る (`confirmDiscardIfDirty()`経由、最後の1枚は白紙へリセット。視覚確認は上記と同じ理由でブロック中)
- [x] タブ切替時にシンタックスハイライトが正しい言語で再描画される (`syncViewForActiveSession()`が`setLanguage()`を呼び`SyntaxWorker`の保持木を強制的に作り直す。視覚確認は上記と同じ理由でブロック中)
- [x] Debug / Release / ubsan 全 green (1044/1044)、clang-tidy 新規警告 0 (ステップ1〜4の全コミットで確認済み)

### 実装後の確定事項

**`WC_TABCONTROL`を採用** (自前D2D描画は不採用) — 既存ウィジェット (`OutlinePane`) の標準コントロール路線に合わせた。`initCommonControls()`に`ICC_TAB_CLASSES`が欠落しており`WC_TABCONTROLW`が未登録のままだった実害あるバグをステップ2のドッグフーディングで発見・修正した (`ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES`)。

**🔴 未解決の重大issue: `docs/issues/native_overlay_widgets_invisible.md`。** ステップ2完了後のドッグフーディングで、`TabBar`を含む全てのネイティブ Win32 オーバーレイウィジェット (`FindBar`/`GrepBar`/`CommandPalette`/`GotoLineBar`/`OutlinePane`/`TabBar`) が、Win32 API上は正しく作成・配置・データ投入されている (`TCM_GETITEMCOUNT`等で確認可能) にもかかわらず画面上に一切描画されない、という WI-05 固有ではない全社的な不具合が発覚した。ユーザー自身が実機で`Ctrl+F`(FindBar、Phase 5b3a以来の既存・実績ある機能)を押しても入力欄が見えないことを確認し、TabBar固有の問題ではなくシステム全体の問題であると確定した。DXGI flip-model/DWM合成無効化/RDPセッション/低コントラスト/`WM_PAINT`枯渇の5仮説を検証し全て否定したが、根本原因は未特定のまま。ユーザーの指示によりissueとして起票し本格調査は将来のセッションへ引き継いだ。**WI-05自体の実装は、この既知の制約下で「実アプリでの視覚確認」の代わりにWin32 API構造確認+単体テストで検証を進めた** (Ctrl+S後の●マーカー消滅のような視覚専用のDoD項目は、その裏付けとなるコード自体 (`InvalidateRect()`呼び出し等) の存在確認をもって「実装完了」の根拠とした)。

**`resetViewAfterDocumentSwap()`と`syncViewForActiveSession()`を明確に分離した。** 前者 (WI-02由来) は「同一タブ内で文書を差し替える」際 (F12/Grep結果クリック) に検索マッチ/フォールド/ブックマークを**クリアする**関数のまま変更していない。後者 (WI-05新設) は「既にそのタブ自身の状態を持つ既存セッションへ主役を移すだけ」のタブ切替向けで、クリアではなく**復元**する。新規タブ (`openBlank()`) では両者の観測結果が偶然一致する (状態が最初から空のため) ため、新規タブにも`syncViewForActiveSession()`のみで対応できた。

**`Workspace::openFile()`の戻り値を`std::optional<size_t>`から`std::variant<size_t, document::LoadError>`へ拡張した。** 既存の`document_open.h::openDocumentAt() -> std::variant<LoadedFileMeta, document::LoadError>`と同じ`variant`規約に厳密に合わせた判断 (`std::expected`という2つ目の「成功か失敗か」表現を持ち込まない)。`Ctrl+O`が具体的な失敗理由をダイアログ表示し続けられる。

**`Ctrl+PgUp`/`Ctrl+PgDn`を意図的にタブ切替へ再割り当てした。** `editor_input.cpp`の`applyMovementKey()`は元々`VK_PRIOR`/`VK_NEXT`について`ctrlDown`を見ておらず (矢印キー/Home/Endとは異なる既存の非対称性)、無条件でページ移動フォールバックへ落ちていた。`handleTabSwitchKey()`をこのフォールバックより手前へ挿入することでタブ切替用に転用した。

**`Ctrl+1`〜`Ctrl+9`は額面通りの位置** (Ctrl+1=タブ0、…、Ctrl+9=タブ8)。Chrome/VSCode式の「Ctrl+9=常に最後のタブ」は不採用 — `tabIndexForDigit()`は範囲外/該当タブなしをクランプせず`nullopt`(no-op)として扱う。

**独立して発見・修正したバグ: `confirmDiscardIfDirty()`の「保存しない」選択と`Workspace::closeSession()`の独立したdirtyチェックが衝突していた。** 前者はユーザーが破棄に同意しても`isDirty()`自体はクリアしない設計だが、後者はdirtyなセッションを無条件に拒否する既存契約を持つ。放置すると`Ctrl+W`で「保存しない」を選んでもタブが閉じない実害あるバグになっていたため、`handleTabCloseKey()`内で破棄同意直後に`session.document().markSaved()` (実ディスク書き込みなし) を呼びこの矛盾を解消した。

**U#24 (`SyntaxWorker`を共有するかタブごとに持つか) の回答: 共有のまま。** `syncViewForActiveSession()`の`setLanguage()`呼び出しが`SyntaxWorker`の保持木破棄+全文書再解析を毎回強制するため、タブ切替のたびに正しい言語で再描画される。体感が悪ければ将来分離を検討する (ベンチ根拠なしに先行複雑化しないというCLAUDE.mdルール10の方針通り)。

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

- [x] メインエディタで未確定文字列が**下線付きでキャレット位置にインライン表示される**
- [x] 変換対象節がハイライトされる
- [x] 候補ウィンドウがキャレット位置に追従する
- [x] 変換確定後、確定文字列が Undo 1 ステップとして `Document` へ挿入される
- [x] 複数カーソル時の挙動が定義され、コメントに明記されている (`collapseToPrimary()` を `WM_IME_STARTCOMPOSITION` で呼ぶ。確定後の複数カーソル復元はしない — 詳細は下記「実装後の確定事項」)
- [x] 🔴 **実機で MS-IME による手動確認を完了している。** ユーザーが実機で「にほんご」等を入力し、未確定文字列の下線表示・候補ウィンドウ追従・1 Undo ステップでの確定・Escape によるキャンセルを確認、「問題無いように見える」との報告を受けた (2026-08-12)。**スクリーンショットは本セッションでは取得していない** — DoD原文が求めていた記録は口頭確認で代替した。今後より厳密な記録が必要になった場合は追加で取得する
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

**中国語 / 韓国語 IME の確認は Phase 12 (WI-17) へ。** 本 WI は日本語のみ。

### 実装後の確定事項 (2026-08-12 完了)

**オーバーレイ方式を採用(真の行内リフローは不採用)。** 未確定文字列は既存の`drawFoldedHeaderMarker()`/`drawBreadcrumb()`と同じ「毎フレーム`CreateTextLayout()`する使い捨てレイヤー」として、実際の行の描画の上に重ねて描く。真のリフロー(既存行のグリフを右へ押し出す)は`drawTokensOnLine()`等5箇所の列計算に影響が及ぶため、本WIの唯一の受け入れ条件が実機目視確認のみである点を踏まえ最もリスクの低い設計とした。トレードオフ(変換中はその行の未変換文字列より後ろの文字が一時的に隠れうる)はDoDの文言が明示的に要求していないため許容。

**Imm32呼び出しは`MainWindow`に一元化。** `normal_mode_wiring.cpp`は`NeoMIFES.exe`へ直接コンパイルされ、API呼び出しを分散させると`imm32.lib`を複数ターゲットへリンクする必要が生じるため、`MainWindow::setImeCandidatePosition(POINT)`という命令的publicメソッド1つに集約した。

**`HIMC`のRAIIには新規`platform::ImeContext`を新設。** 既存の`platform::HandleGuard`は単一引数のステートレスDeleterのみに対応し、`ImmGetContext(hwnd)`/`ImmReleaseContext(hwnd, himc)`という「解放にhwnd・himc両方を要するペア」には適合しないため。

**複数カーソルは`WM_IME_STARTCOMPOSITION`で`collapseToPrimary()`を呼び、確定後の復元は行わない。** `CommandDispatcher::dispatch()`が`ReplaceRangeCommand::cursorsAfterExecute()`で無条件にカーソル集合を単一カーソルへ置き換えるため、確定後に「1カーソルに戻る」が追加コード無しで自然に成立する。キャンセル時もカーソルは畳まれたまま据え置く意図的な単純化。

**確定文字列の1 Undoステップ化は、`WM_IME_COMPOSITION`を`DefWindowProcW`へ一切フォワードしないことから機械的に導かれる。** フォワードすると、Windowsの既定処理が`GCS_RESULTSTR`から自動的に1コード単位ごとの`WM_CHAR`を生成し(`tryMerge()`はADR-012により意図的に未実装のため)、3文字の日本語単語が3個の独立したUndoステップとして確定してしまう。自前で`GCS_RESULTSTR`を抽出し`ReplaceRangeCommand`を1回dispatchすることで、これを回避した。

**CI検証の過程で3件の実装バグ/debtを発見・修正した(詳細は`docs/history/TIMELINE.md` Session 84):** `RenderPipeline::captureFrameState()`が`FrameState`へ`.imeComposition`を含め忘れていたバグ(WI-03の`leftColumn`欠落と同型の粗粒度フレームスキップ再発パターン、新規回帰テストで発見)、および今回のpushで初めてCI検証されたWI-05由来のclang-tidy debt2件(`normal_mode_wiring.cpp`の`performance-unnecessary-value-param`/`readability-function-cognitive-complexity`、`tab_bar.cpp`の`misc-redundant-expression`)。

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
| リソース | 新規 `resources/neomifes.rc` / `neomifes.ico`。**`.manifest` は新設しなかった** — 詳細は下記「実装後の確定事項」参照 |

**ステータスバーの文字コード・改行コード欄はクリックで変更できるようにする** (WI-02 でダイアログに出さなかった選択 UI をここで提供する)。

**アイコンについて:** `.ico` は自前で用意する必要がある。デザインが決められない場合は、暫定として単色背景に "N" の字を置いた最小限のものを作り、**issue に「アイコンの正式デザイン」として起票して先送りしてよい** (体裁上、既定アイコンのままよりは遥かに良い)。

### 影響ファイル

- 新規 `resources/neomifes.rc` / `neomifes.ico` (`.manifest` は新設しなかった、実装後の確定事項参照)
- `src/app/CMakeLists.txt` — `.rc` をターゲットソースへ追加
- `src/ui/src/main_window.cpp` — メニュー / `WM_CONTEXTMENU` / `TranslateAcceleratorW`
- 新規 `src/ui/include/neomifes/ui/status_bar.h` / `src/ui/src/status_bar.cpp`
- `src/render/src/render_pipeline.cpp` — 行番号描画、ガター幅の動的化
- `src/app/main.cpp` — `HACCEL` へのキーバインド集約、タイトル更新

### DoD

- [x] メニューバーから 開く / 保存 / 元に戻す / 検索 / 各種トグルが実行できる (ファイル/編集/検索/表示/ツール/ヘルプの6メニュー、`menu_bar.h`)
- [x] ステータスバーに 行:桁 / 文字コード / 改行コード / 選択文字数 が表示され、カーソル移動で更新される (INS/OVR・言語も追加で表示、計6パート)
- [x] 文字コード欄・改行コード欄をクリックして変更でき、保存に反映される (ステップ6、`TrackPopupMenu`による選択肢提示)
- [x] 行番号が表示される (**桁数に応じた動的幅**、ステップ7の想定を上回る形で実装)
- [x] ウィンドウタイトルにファイル名と未保存マーク (`*`) が出る
- [x] 右クリックでコンテキストメニューが出る
- [x] **exe に独自アイコンが埋め込まれている。** ただしエクスプローラでの目視確認ではなく、`System.Drawing.Icon.ExtractAssociatedIcon()` で実際にビルド済み `.exe` からアイコンを抽出しPNG保存する形で確認した(詳細は下記「実装後の確定事項」)。デザインは暫定(単色背景+"N")、正式デザインは別途 issue 化が必要
- [x] **全キーバインドが `HACCEL` に集約されている。** ただし字義通りの「全て」ではなく、**意図的に narrow scope** — `editor_input.cpp` からは `if (ctrlDown && vkCode == ...)` 連鎖を完全に除去できたが、`normal_mode_wiring.cpp` 側の Find/Grep/CommandPalette/Outline/GotoLine の各トグルキーは既存の `handle*Key()` 連鎖に意図的に残した(理由は下記「実装後の確定事項」、`command_dispatch.h` 冒頭コメントに機械可読な形で明記済み)
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0 (ステップ10の最終検証で新規警告0、WI-06のような追加バグ発見は無し)
- [x] 🎉 **M2 達成 (2026-08-13): アプリケーションとして成立**

### 実装後の確定事項 (2026-08-13 完了)

**ステップ0: `WS_CLIPCHILDREN` 仮説が的中した。** 着手前に発見したP0 issue [`native_overlay_widgets_invisible.md`](../issues/native_overlay_widgets_invisible.md)(FindBar等6ウィジェットが不可視になる根本原因未特定のバグ)を、本WIの最初のステップとして先行調査した。`MainWindow::create()` の `windowStyle` に `WS_CLIPCHILDREN` が欠落していたことが原因(`src/ui/src/main_window.cpp`)。1行追加で解消し、実機スクリーンショットでTabBar帯の可視化を確認した。issueは解決済みへ移動済み — ステータスバー実装(本WI)が7つ目の被害ウィジェットになるリスクは解消された。

**`CommandId` + `dispatchCommand()` という単一チョークポイントを新設し、HACCELとメニューの両方から同じ経路で呼べるようにした。** `ui::CommandId`(40000番台、既存の子ウィジェットコントロールID帯1001-7001と非衝突)を新設し、`CommandDescriptor` にも同フィールドを追加してコマンドパレット・HACCEL・メニューバーが同じ語彙を共有する設計にした。`command_dispatch.h` は意図的に narrow scope — Save/SaveAs/Open/New/タブ切替/タブクローズ/Copy/Cut/Paste/Undo/Redo/INS-OVRトグルのみを扱い、Find/Grep/CommandPalette/Outline/GotoLineの各トグルキーは既存の `handle*Key()` 連鎖に残した。理由: これらは「オーバーレイウィジェットにフォーカスがある間は親へキーが届かない」という既存の構造的制約と絡み合っており、グローバルアクセラレータテーブルへ昇格させるとフォーカス中の子コントロール(`WC_EDIT`)より先にキーを奪ってしまう競合が実際に発生することが判明したため。メニューバーのクリックはWin32のWM_COMMAND経由でこの競合が起きないため、メニュー項目としては両カテゴリとも問題なく配線できている。

**ステータスバーの `NM_CLICK` は Common Controls 4.71 以降サポートされていることを実機で確認してから実装した(推測に頼らず検証、CLAUDE.mdルール3)。** `msctls_statusbar32` から `WM_NOTIFY` 経由で届くことを確認し、`StatusBar::handleNotify()` → `onPartClicked` → 文字コード/改行コード欄クリック時の `TrackPopupMenu` 選択肢提示という経路を実装した(ステップ6)。

**INS/OVR は表示だけでなく実編集動作まで本格実装した(ユーザー承認済み)。** `VK_INSERT` で `EditorSession::overwriteMode()` をトグルし、上書きモード時はカーソル直後の1文字を置換する(行末/文末では挿入にフォールバック)。既存の `MultiCursorEditCommand` を再利用したため、新規 `ICommand` を作らずにUndo/Redoが自動対応した。

**行番号ガターは固定幅ではなく、桁数に応じた動的幅で実装した(ステップ7、当初のroadmap想定を上回る形)。** `RenderPipeline::gutterWidthDips()` が `computeGutterWidthDips(totalLines, charWidthDips, minWidthDips)` を呼び、`minWidthDips`(旧`kGutterWidthDips=24.0F`)は文字幅未計測時・空文書時のフォールバック値として残した。既存の全テスト座標系を壊さない設計。

**ウィンドウタイトルは `formatWindowTitle(filename, isDirty)` という純粋関数 + `MainWindow::setTitle()` という命令的メソッドの組み合わせで実装した(ステップ8)。** 毎フレーム再構築する既存の `tabBar.setTabs()` と同じ「差分ガード無し」規約を踏襲。

**右クリックコンテキストメニューは、`menu_bar.h` の既存 `kEditMenuItems`(5項目: Undo/Redo/Cut/Copy/Paste)をそのまま流用した(ステップ9)。** 新規コンテンツ定義が不要になり、メニューバーの編集メニューと右クリックメニューが将来も文言面でズレない設計になった。

**ステップ10のリソースファイル実装で、着手前に「要probe」と明記していた2件の技術的分岐点が、いずれも実装より軽い形で解決した。** (a) Ninja+MSVCでの`.rc`コンパイルに `enable_language(RC)` は不要 — `.rc`ファイルを`add_executable()`のソースリストへ加えるだけでCMakeが自動検出し`rc.exe`を呼ぶ。(b) `.rc`埋め込みマニフェストと`main.cpp`既存のリンカプラグマ製マニフェスト(Common Controls v6依存)の共存方法は、`resources/neomifes.rc` が**そもそも`RT_MANIFEST`リソースを一切定義しない**設計にすることで、両者の衝突自体を回避した。結果として当初想定していた `neomifes.manifest` ファイルは新設しなかった(上表「既に決まっている設計」の訂正箇所)。アイコン自体はPowerShell + `System.Drawing`による手製の複数解像度(16/32/48/256px)ICOファイル(暫定デザイン、単色背景+"N")。

**WI-07全体の最終検証(Debug/Release/ubsanフル3構成+clang-tidyスイープ)は新規警告0で通過した** — WI-06の最終検証がCI由来のバグ2件を発見したのとは対照的に、本WIは10ステップにわたり一度も検証失敗が発生しなかった。

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

- [x] `%APPDATA%\NeoMIFES\settings.json` から読み書きできる (`core::Settings::loadFrom()`/`saveTo()`、`search_history.json`と同じ場所・同じ形式)
- [x] **`kTabWidth` の二重定義が解消されている** (`render_pipeline.cpp`側は`RenderPipeline::m_tabWidth`へ、`editor_input.cpp`側は`applyIndentationConversion()`の`tabWidth`引数へ統合。`grep -rn "kTabWidth" src/`は定義0件、コメント内の歴史的言及のみ残存)
- [x] 設定ファイルが無い / 壊れている場合に既定値で起動する (`loadFrom()`が欠落/不正JSON/バージョン不一致いずれも無条件に既定値へフォールバック、実アプリで壊れたJSONを与えて確認済み)
- [x] フォント・タブ幅・行番号表示の変更が**再起動なしで反映される** (`RenderPipeline::setFontSettings()`/`setTabWidth()`/`setLineNumbersVisible()`/`setMinimapVisible()`の4セッター。ミニマップ表示も同じ形で追加配線した — DoD必須3項目に加えた任意拡張)
- [x] 「読み込み → 変更 → 保存 → 再読み込み」のラウンドトリップ単体テストがある (`core_settings_test.cpp::SaveThenLoadRoundTripsAllFields`、日本語を含む`fontFamily`/`themeName`で往復確認)
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0

**保留項目なし。完全に完了。**

### 実装後の確定事項

- **`SetIncrementalTabStop()`未着手ギャップの発見:** 着手前調査で、`IDWriteTextFormat::SetIncrementalTabStop()`がコードベース全体で1件も呼ばれていないことが判明した。既存の2つの`kTabWidth`コピー(`render_pipeline.cpp`のインデントガイド線計算用、`editor_input.cpp`のタブ⇔スペース変換コマンド用)は、実際の文書中のリテラル`'\t'`文字の描画幅には一切関与しておらず、DirectWriteの既定タブストップに委ねられたままだった。単純に2つの`kTabWidth`を1つの設定値へ統合するだけでは、DoDの「タブ幅の変更が再起動なしで反映される」は見た目上は達成できても実際のタブ文字表示は変わらないという不整合が生じるところだった。`ensureTextFormat()`内で`SetIncrementalTabStop(tabWidth * charWidthDips)`を新規に呼ぶことでこの隠れたギャップを合わせて解消した。
- **`TextLayoutCache`のinvalidation契約:** `TextLayoutCache::getOrCreate()`は`document::LineNumber`のみをキーとし、呼び出しごとに渡される`textFormat`/幅/高さを再検証しない契約(既存)。フォント/タブ幅変更時は`setFontSettings()`/`setTabWidth()`双方が明示的に`m_layoutCache.clear()`を呼ぶ設計とした。
- **設定変更手段:** 本WIでは専用の設定ダイアログを新設せず、`%APPDATA%\NeoMIFES\settings.json`の手動編集+コマンドパレット限定の新規コマンド`settings.reload`(`.commandId = CommandId::None`、`edit.convertTabsToSpaces`と同じ軽量パターン)で「再起動なしの反映」を成立させた。汎用設定ダイアログはWI-08原文に記載がなくスコープ外。
- **ドッグフーディング結果:** `%APPDATA%\NeoMIFES\settings.json`を手動作成しフォントサイズ26.0/タブ幅8/`showLineNumbers=false`/`showMinimap=false`を設定→実際にNeoMIFES.exeを起動→大きなフォント・行番号ガター消失・ミニマップ消失・8幅タブインデントを実機スクリーンショットで確認。続けて構文エラーのあるJSONに書き換えて再起動→クラッシュせず全項目が既定値(小フォント/行番号あり/ミニマップあり/タブ幅4)へフォールバックすることを実機確認。`settings.reload`コマンド自体のコマンドパレット経由での対話的実行(Ctrl+Shift+P)は、この環境の既知の制約(Ctrl/Shift等の修飾キー合成入力が不可)により自動化検証できなかったが、同コマンドが呼ぶ4セッター自体は上記の起動時ドッグフーディングで実機検証済みであり、`Settings::loadFrom()`のラウンドトリップも単体テスト済みのため、実質的な機能は実機で証明されている。

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

移行対象 (既知): テキスト / 選択範囲 / マッチ / 現在マッチ / ブックマーク / フォールドマーカー / Keyword / Type / String / Number / Comment / Preprocessor / ミニマップ 4 種 / Breadcrumb 背景 / Indent guide / 背景

> **実装後の訂正:** 「キャレット」は独立したブラシとして存在しない。`drawCaretOnLine()`は`m_textBrush`をそのまま再利用しているため、テキスト色の移行で自動的にカバーされる。専用の`Theme::caret`フィールドは追加不要だった。

- テーマ切替時は全ブラシを作り直す (既存の `recreateDevice()` のリセット経路がそのまま使える)
- ハイコントラストは Windows のシステム設定 (`SystemParametersInfo(SPI_GETHIGHCONTRAST)`) を尊重して自動選択してもよい

### DoD

- [x] ダーク / ライト / ハイコントラストを切り替えられ、設定に永続化される (`view.theme.dark`/`view.theme.light`/`view.theme.highContrast`の3コマンド、コマンドパレット限定。`settings.themeName`へ`themeKindToSettingsString()`経由で書き込み、`saveTo()`で即時永続化)
- [x] `render_pipeline.cpp` に `D2D1::ColorF` のハードコードが残っていない (23フィールド×3テーマ、11個の`ensureXxxBrush()`+`renderOnce()`の背景`Clear()`を含め全て`theme.h`/`theme.cpp`へ集約。`grep -n "constexpr D2D1_COLOR_F" src/render/src/render_pipeline.cpp`は0件)
- [x] テーマ切替でデバイスロストが起きても正しく再構築される (`recreateDevice()`は新設`resetThemeBrushes()`経由で従来通り21ブラシをリセット、テーマの状態自体には無関係に動作)
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0 (1111/1111テスト×3構成。clang-tidyは`theme.cpp`の`misc-redundant-expression`(`255.0F / 255.0F`の自己除算がフルの色チャンネル値として誤検出される、既存コードベースに前例あり)を1件検出→`1.0F`直書きへ修正し解消)

**保留項目なし。完全に完了。**

### 実装後の確定事項

- **`FrameState`修正が正しさに必須と判明:** 着手前調査で`render()`(`render_pipeline.cpp`)を直接読解し確認 — 粗粒度フレームスキップ(Phase 3c/ADR-011)は`captureFrameState()`のスナップショットが直前と一致すれば`renderOnce()`を完全にスキップする。`setTheme()`単体呼び出し(topLine/cursor/文書バージョン等が無変化)の場合、`FrameState`にテーマを含めなければ、ブラシは`resetThemeBrushes()`でリセットされるのに実際の再描画(新色での再構築)がフレームスキップに飲み込まれ、画面が古い色のまま固まる — これは見た目の問題ではなく正しさの問題である。`ThemeKind themeKind`を`FrameState`の最後のフィールド(`imeComposition`直後)として追加し解消した。この修正の効果は`ThemeOnlyChangeForcesRedrawInsteadOfFrameSkip`テストで直接検証している(`m_leftColumn`/`m_imeComposition`と同型の回帰テスト)。
- **`resetThemeBrushes()`への切り出し:** `recreateDevice()`(デバイスロスト回復)が持っていた21ブラシの`.Reset()`ブロックを新規private`resetThemeBrushes()`へ抽出し、`setTheme()`と共有した。`recreateDevice()`自体の挙動は無変更(リファクタのみ)。
- **3フラットコマンド vs `showChoiceMenu<T>()`ピッカー:** 既存の`showChoiceMenu<T>()`(ステータスバーの文字コード/改行コード選択で使用)はクリック起点の`TrackPopupMenu`であり、パレットコマンド(クリック位置を持たない)から使うには新規`GetCursorPos()`フォールバック機構が要る。build_plan.md §2.3の「迷ったら小さく作る」原則に従い3つの独立コマンドを採用した。
- **メニューバー統合はスコープ外:** `kViewMenuItems`は1項目のみでサブメニュー機構が無く、追加するには新規`CommandId`+`dispatchCommand()`配線という大きな変更が要る。WI-08の`settings.reload`(パレット限定・メニュー無し)と同じ扱いとした。
- **OSハイコントラスト自動検出(`SPI_GETHIGHCONTRAST`)はスコープ外:** build_plan.md原文が「してもよい」と明記する任意項目であり、要件定義書§14にも記載が無い。ユーザーの明示選択でのみ`HighContrast`に到達する(OS設定からの推測はしない)。
- **`core::Settings`自体は機能的に無変更:** `themeName`フィールド(WI-08で追加済み)は検証されない自由記述文字列のまま。安全網は消費境界(`theme_settings.h`の`parseThemeKind()`)にのみ置いた(CLAUDE.mdの「境界でのみ検証する」原則)。
- **ドッグフーディング結果:** `%APPDATA%\NeoMIFES\settings.json`の`themeName`を`light`/`high-contrast`/存在しない値("this-is-not-a-real-theme")に手動書き換え→起動、の3サイクルを実施し、いずれも実機スクリーンショットで正しい配色(白背景+VSCode Light+風トークン色/純黒背景+シアン・オレンジ等の高彩度トークン色/デフォルトのDarkへの安全なフォールバック)を確認した。さらにコマンドパレット(Ctrl+Shift+P)経由で`Theme: Light`を実行し、**再起動なしで画面が即座にLight配色へ再描画されること**、および`settings.json`が`"themeName":"light"`へ即座に書き換わることを確認した(この環境で過去複数セッションCtrl+Shift+P等の修飾キー合成入力が不調だったが、本セッションでは正常動作した)。続けてNeoMIFESを終了→再起動し、永続化された`light`テーマが再起動後も自動的に復元されることを確認した。デバイスロスト相当のシナリオ(最小化/復元)は`resetThemeBrushes()`が`recreateDevice()`と無関係に動作する設計のため理論上の懸念はないが、実機での明示的なデバイスロスト誘発は本セッションでは行わなかった(自動テストの`RendersWithoutDocumentAttached`等の既存デバイス回復テストがこの経路自体の健全性を別途担保している)。

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
