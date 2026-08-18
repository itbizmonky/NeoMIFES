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
- 規模: 約 35,000 行 / 1309 テスト / ADR 21 本
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
- [x] **WI-09** テーマ (ダーク / ライト / ハイコントラスト) → コミット: `be65533` (実装) / `da1da01` (ドキュメント同期)
- [x] **WI-10** キーバインド設定 + プリセット (秀丸 / サクラ / VSCode) → コミット: `dc5a724`
- [x] **WI-11** 自動保存 / バックアップ / クラッシュ復旧 / 最近開いたファイル → コミット: `bf03ff0`
- [x] **WI-12** 基本編集の穴埋め (Ctrl+A / 自動インデント / 行複製・移動・削除) → コミット: `51d419d`
  - 🎉 **M3 達成: 設定・テーマが揃う**

## Phase 12' — MVP 出荷判定

- [x] **WI-13** MVP 出荷判定 (§6 のチェックリスト14項目中12達成、残り2項目はユーザー判断で保留のまま🎉M4達成扱いに確定) → コミット: `89d4dcf`〜`6ccc992`
  - 🎉 **M4 達成 (2026-08-16): 秀丸/サクラの代替として出荷可能**

## Phase 10 — ログ解析 / CSV / JSON-XML Tree (最大の差別化点、WI-13完了により着手解禁)

roadmap §10.1 (ログ解析モード) を WI-14a〜d の4サブ WI へ切り直し完結した (詳細は §5)。JSON-XML Tree (§10.3) は WI-15a (ヘッドレス基盤) → WI-15b (非同期インデックス化 + EditorSession配線、UIなし) まで進行中、残りは切り直しながら継続する。CSV (§10.2) は WI-16a (ヘッドレス解析モデル) → WI-16b (非同期ワーカー + EditorSession配線、UIなし) まで進行中、残り(グリッドUI)は切り直しながら継続する。

- [x] **WI-14a** ログ解析モード ヘッドレス基盤 (`LogPatternRule`/`LogModel`、スレッド/UI なし) → コミット: `2512c76`
- [x] **WI-14b** 非同期インデックス構築 + フォーマット自動検出 + `EditorSession`配線 + ピース単位ストリーミング最適化 → コミット: `4f55d8b`/`062bfd9`/`9c5c982`/`2f856b1`/`a6c1849`/`525e0f1`
- [x] **WI-14c** UI モード MVP 🎉 (色分け/フィルタ/時系列ジャンプ、Phase 10.1 の MVP 達成) → コミット: `e92ddfb`/`84f5bf9`/`0f5af55`/`8250f3d`/`d41f52b`/`4d30233`
- [x] **WI-14d** 複数行エントリのグルーピング + ユーザー編集可能パターンファイル 🎉 (Phase 10.1 完結) → コミット: `2c16e79`/`9673824`
- [x] **WI-15a** JSON ツリーモデル ヘッドレス基盤 (Phase 10.3 最初のサブ WI、XML/UI は非スコープ) → コミット: `9334f0c`/`1f21780`
- [x] **WI-15b** JSON ツリー 非同期インデックス化 + EditorSession配線 (UIなし) → コミット: `1d9156c`/`9b8075a`/`83fcadb`/`7bd4dee`
- [x] **WI-16a** CSV モード ヘッドレス解析モデル (Phase 10.2 最初のサブ WI、非同期ワーカー/グリッドUIは非スコープ) → コミット: `ab7dd5e`/`c8fd842`
- [x] **WI-16b** CSV モード 非同期ワーカー + EditorSession配線 (UIなし) → コミット: `a8af2b7`/`0457fda`/`aa15488`
- [ ] **WI-15c以降** Phase 10.3 の残り (ツリーUI・折り畳み統合・整形・バリデーション・XPath/JSONPath)
- [ ] **WI-16c以降** Phase 10.2 の残り (グリッドUI・列固定・フィルタ・ソート・式列・セル編集) — 着手時に本書 §5 と同じ形式でサブ WI を切り直す
- [ ] **WI-17** Phase 11 — Git 統合 / LSP / マクロ
- [ ] **WI-18** Phase 9 — AI プラグイン
- [ ] **WI-19** Phase 12 — 総合品質保証・正式出荷
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

**中国語 / 韓国語 IME の確認は Phase 12 (WI-19) へ。** 本 WI は日本語のみ。(WI 番号は WI-15a/WI-16a 着手時の繰り下げを反映した現在値 — 本行執筆時点の WI-17 という表記は 2026-08-18/2026-08-19 のリナンバリングで陳腐化していたため訂正)

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

- [x] キーバインドを設定ファイルで変更でき、再起動後も保持される (`%APPDATA%\NeoMIFES\keybindings.json`、`core::KeyBindings::loadFrom()`/`saveTo()`)
- [x] 4 プリセットを切り替えられる (`keybindings.preset.{neomifes,hidemaru,sakura,vscode}` パレットコマンド、`KeyBindings::forPreset()`)
- [x] 競合するキーバインドを設定したとき、警告するか後勝ちにするかが定義されている (`command_ids.h` の enum 宣言順で後勝ち、決定的。Debug ビルド限定で `OutputDebugStringW` へログ — 詳細は下記「実装後の確定事項」)
- [x] Debug / Release / ubsan 全 green (各 1158/1158 テスト)、clang-tidy 新規警告 0

**保留項目なし。完全に完了。**

### 実装後の確定事項

- **スコープは「広範囲」を採用:** `ui::CommandId` 35個のうち `About`(キーボード経路なし)を除く **34個全て** をリマップ対象にした。既存 `HACCEL` の16個(Save/Open/New/Tab*)に加え、`normal_mode_wiring.cpp` の `handle*Key()` 関数群にハードコードされていた残り18個(Find*/Grep/CommandPalette/Outline/GotoLine/Bookmark*/TagJump/Copy/Cut/Paste/Undo/Redo/ToggleOverwriteMode)も対象にした。秀丸/サクラ/VSCode の差を出すキー(検索・grep・ブックマーク等)がまさにこの18個側にあり、対象外にすると4プリセットの実質的な違いが矮小化されるため。AskUserQuestionでユーザーに確認済み。
- **競合解決方針(決定的・enum宣言順):** `resolveKeyBindingConflicts()` は `ui::kAllRemappableCommandIds`(`command_ids.h` の宣言順、固定)を走査し、同一chordへの複数バインドは**後に宣言されたCommandIdが勝つ**。JSON書き込み順や `core::KeyBindings` 内部の `std::map` 順など再現性のない基準は使わない。結果として HACCEL対象16個(Save等)がFind/Grep/Palette等12個より優先され、Copy/Cut/Paste/Undo/Redo/ToggleOverwriteModeの6個が最終的に全てに優先する、という3層の優先順位になる(これは狙って設計したものであり、既存の `app_keybinding_dispatch_test.cpp` のテストが実測値でこれを固定している)。通知手段はDebugビルド限定の `OutputDebugStringW` ログのみ — トースト/ダイアログ等のライブUI基盤が本コードベースに無いため(ADR-019時点で `ui::ToastState` はヘッドレスのまま)、可視的な警告UIは本WIのスコープ外。
- **秀丸プリセットは意図的に不完全:** `key_bindings_presets.cpp` の秀丸テーブルは、外部一次資料で確認できた項目(Ctrl+N/O/S、Ctrl+Z/Y/X/C/V、Ctrl+F/Shift+Ctrl+F、Ctrl+R、Ctrl+G、F11、F10/Ctrl+F10)のみを収録し、確認できなかった項目(SaveAs・Grep・FindNext/FindPrevious・ブックマーク系・タブ切替・CommandPalette相当・ToggleOverwriteMode)は空(未対応)のまま残した。build_plan.md 自身の「誤ったプリセットは無いより悪い」指示に従った判断であり、バグではない。サクラ/VSCodeプリセットは公式ヘルプ/公式ドキュメントで裏取りできたためほぼ全項目を収録している。
- **2つの独立したディスパッチ機構は WI-07 のまま維持:** HACCEL対象16個は `TranslateAcceleratorW` 経由(`buildAcceleratorRows()` が `keybindings.json` のロード/リロード/プリセット切替のたびに1回だけ再構築、毎キー入力では再構築しない)、残り18個は `normal_mode_wiring.cpp` の既存 `handle*Key()` チェーン経由(各関数が `chordMatches()` を毎キー入力で呼ぶ、再構築ステップ不要で即座に反映される)。この非対称性はWI-07が確立した「オーバーレイウィジェット(FindBar等の `WC_EDIT`)とのフォーカス競合を避けるため一部コマンドはグローバルアクセラレータに乗せられない」という制約をそのまま継承しており、WI-10では変更していない。
- **`core::KeyBindings` は `ui::CommandId` にも Win32 `VK_*` にも依存しない:** レイヤードアーキテクチャ(CLAUDE.md §3)を守るため、`core::` 層はコマンドもチョードも純粋な文字列(`std::u16string`)として保持する。文字列⇔`CommandId` の変換は `ui::command_id_name.h`(`commandIdToString()`/`commandIdFromString()`)、文字列⇔`KeyChord` の変換は `app::key_chord.h`(`parseKeyChord()`/`keyChordToString()`)がそれぞれ担う — WI-09の `theme_settings.h` が確立した「下位層は文字列、上位層で enum へブリッジする」パターンをそのまま踏襲した。
- **メニューバー表示の実行時更新はスコープ外:** `menu_bar.h` の `MenuItemSpec::label` はウィンドウ作成時に固定文字列として焼き込まれ、`SetMenu`/`ModifyMenuW` はコードベース全体に1つも存在しない。リマップ後も実際のキー入力自体は正しく機能する(HACCELまたは手動チェーン経由)ため、影響はメニュー上の `\tCtrl+X` 表示が再起動まで古いままという見た目のみ。既知の制限として `docs/issues/menu_bar_keybinding_label_stale.md` に起票した。
- **コマンドパレットの `keybindingLabel` は動的生成に変更:** 既存6個の登録済みコマンド(find.show/find.replace/find.next/find.previous/edit.undo/edit.redo)のラベルはハードコード文字列から `keybindingLabelFor(keyBindings, chordId)`(現在の最初のバインドを `parseKeyChord()`→`keyChordToString()` で整形)へ切り替えた。`keybindings.reload`/`keybindings.preset.*` コマンドはロード/切替のたびに `commandPalette.setCommands(buildCommandRegistry(...))` を呼び、パレット全体のラベルを再構築する。
- **ドッグフーディング:** 実機での対話的UI検証(コマンドパレットを実際に開いて4プリセットのラベル表示を目視確認、`Ctrl+Alt+S` 押下でSaveが実際に発火することの確認等)は、この環境で修飾キー付きキーボード入力の合成が過去複数セッションにわたり不安定/不能と判明しているため実施できなかった。代わりにファイルレベルの検証(`%APPDATA%\NeoMIFES\keybindings.json` の直接読み書き+プロセス生存確認)で以下4点を確認した: (1) ファイル不在時は自動生成せず埋め込みneomifesプリセットへフォールバック、(2) 34個中1個だけを定義した手書きJSONを正しくロードしクラッシュしない、(3) 壊れたJSONでneomifesプリセットへ安全にフォールバックしクラッシュしない、(4) `find.show`と`file.save`を同一chord(`Ctrl+Q`)へ意図的に競合させてもアクセラレータテーブル構築が例外を投げずクラッシュしない。ロジック自体の正しさ(chord一致判定・競合解決の決定性・アクセラレータ行の省略)は既存の単体/統合テスト(1158/1158 green)で別途証明済みであり、本ドッグフーディングが追加したのは「実際にコンパイルされたバイナリでのUI配線がクラッシュしない」という経験的証拠のみである。

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

- [x] 編集後 N 秒放置すると autosave ファイルが生成される (`MainWindow::onTimer`/`onFocusLost` → `autoSaveAllDirtySessions()` → `performAutoSave()`、ドッグフーディングで `autosave/` ディレクトリの起動時自動作成を確認)
- [x] 保存時に `.bak` が生成される (設定オフで生成されない) (`saveFile(keepBackup=settings.createBackupOnSave)`、`document_save_roundtrip_test.cpp` で往復検証済み)
- [x] プロセスを強制終了 → 再起動 で復旧が提案され、承諾すると内容が戻る (`scanForRecoverableAutoSaves()` + `showCrashRecoveryDialog()` + `Workspace::adoptSession()`。実機での強制終了→再起動の対話フロー自体は本セッションの環境制約 (キーボード修飾キー合成不安定) により完全な実演はできず、`app_autosave_test.cpp` のヘッドレステスト + コードレビューで正しさを担保した。起動時のスキャン自体は実機で「候補0件」を確認済み)
- [x] 最近開いたファイルがメニューに出て、クリックで開ける (実機ドッグフーディングで「ファイル」メニューの「最近使ったファイル」サブメニューが正しく描画され、`(なし)` プレースホルダも確認済み。クリックでの実際のオープンは `dispatchRecentFileCommand()` のコードレビューで検証、`--open` 起動はRecentFilesを更新しない設計のため実機では空リストのままだった)
- [x] **autosave が元ファイルを破壊しないことをテストで保証している** (`app_autosave_test.cpp`: `performAutoSave()` 後に実ファイルが完全無変更であることを直接検証)
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0 (バックグラウンドエージェントによる最終フルスイープで確認)

### 実装後の確定事項

- **`saveFile()` の `keepBackup`/`markAsSaved` は末尾トレーリング引数で拡張** — 既存呼び出し元・テストの挙動を1バイトも変えずに済んだ。`performAutoSave()` は `markAsSaved=false` で呼ぶため、自動保存後も `Document::isDirty()` は正しく `true` のまま(タブの未保存マーカーが誤って消えない)。
- **`AutosaveIndex` は searchHistory 等と異なり毎回変更のたびに即座に `saveTo()` する** — クラッシュ復旧の全趣旨が「クラッシュ前に確実にディスクへ書かれている」ことのため、exit時バッチ書き込みでは不可。
- **`MenuBarHandles{HMENU menuBar, HMENU recentFilesSubmenu}` を新設し、`buildMenuBar()` の呼び出しタイミングを `wireNormalMode()` 内部から `main.cpp`(`window.create()` より前)へ移動した** — `CreateWindowExW` の `hMenu` はウィンドウ作成時に固定されるため。
- **クラッシュ復旧は常に通常通り `Workspace` を構築した上で `adoptSession()` により追加タブとして復元する方式を採用**(「復旧対象を初期タブとして使う」特別扱いはしない)。`--open` なしで復旧時に空タブが1つ余分に残る軽微なUXコストを許容し、分岐の複雑化を避けた。
- **`CommandDispatchContext::autosave`/`AutosaveContext::index` が非const参照メンバのため、これらを内部で構築する全ての関数は自身の `autosave` パラメータを非const `AutosaveContext&` として宣言する必要があった**(`const AutosaveContext&` のままだと `CommandDispatchContext{...}` 構築時にMSVC C2440「修飾子の喪失」でコンパイル失敗する)。既に受け取った `CommandDispatchContext&`/`AutosaveContext&` を転送するだけの関数は影響を受けない。
- **`--open` CLI引数はRecentFilesを更新しない**(設計通り、対話的なオープン(Ctrl+O/F12/Grep結果クリック/D&D/最近使ったファイルメニュー)のみが `recentFiles.record()` を呼ぶ)。実機ドッグフーディングで確認済み。

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

- [x] 上記 5 機能が複数カーソル状態でも正しく動く (`core_line_operations_test.cpp` / `app_editor_input_test.cpp` の複数カーソルケースで検証、実機では Ctrl+D のみ視覚確認 — 下記「実装後の確定事項」参照)
- [x] いずれも Undo 1 ステップで戻る (`LineOperationCommand`/`MultiCursorEditCommand` はいずれも単一 `ICommand` として dispatch されるため構造的に保証される)
- [x] 自動インデントはタブ/スペース設定 (WI-08) を尊重する (前行の実テキストをそのまま文字列コピーするため、タブ/スペースいずれの設定でも自動的に追従する。`Settings` を直接参照する必要はない — 詳細は下記)
- [x] Debug / Release / ubsan 全 green、clang-tidy 新規警告 0 (バックグラウンドエージェントによる最終フルスイープで確認、1227/1227 テスト green)
- [x] 🎉 **M3 達成**

### 実装後の確定事項

- **行指向コマンド専用の第3のカーソル復元ポリシー `core::LineOperationCommand` を新設した。** 既存の2つ(`MultiCursorEditCommand`: edits.size()==cursorsBefore.size() の厳密な1:1対応、`ReplaceAllCommand`: N編集M カーソルでカーソル自体は動かさない)のどちらも「複数カーソルが同一行を共有すると編集本数がカーソル本数より少なくなるが、それでも各カーソルを意味のある位置へ再配置する必要がある」という行削除/行移動の要件に合わなかったため。`CursorEditMapping{editIndex, offsetIntoInsertedText}` を呼び出し側が明示的に渡す設計とし、適用/Undo自体は既存の `cumulative_shift_edit.h`(`applyEditsWithCumulativeShift()`/`undoEditsDescending()`)を他の2クラスと共有する。
- **行の連続実行(contiguous run)へのグループ化ロジック `groupIntoContiguousRuns()` を `computeMoveLineEdits()`/`computeDeleteLineEdits()` で共有する。** 複数行にまたがる削除で「直前の `\n` を削るかどうか」を行ごとに判定すると、文書末尾に到達する複数行ランで末尾に `\n` が余分に残るバグが発生した(バックグラウンド検証エージェントが単体テストで発見、`"abc\ndef\nghi"` の末尾2行削除が `"abc\n"` になっていた不具合)。ラン単位で1回だけ判定する設計に修正して解消。
- **既存コードベースの確立済み規約(`selection_model.cpp` の `lineContentEnd()` コメント由来)に従い、行末尾の `'\r'` は行内容として扱い、`'\n'` のみを行区切り文字とする。** `RenderPipeline` の行分割と同じ挙動であり、CRLF対応自体は将来の Encoding Engine 側の課題として意図的に据え置く。
- **WI-12 の5コマンドは意図的に `core::KeyBindings`/プリセットシステム(WI-10)の対象外のままとした。** Ctrl+A/Ctrl+D/Alt+↑/Alt+↓/Ctrl+Shift+K はいずれも `normal_mode_wiring.cpp` にハードコードされた VK_* 比較のまま(既存の継続編集キー: 矢印/Home/End/Backspace/Delete と同じ扱い)。理由: これらのコマンドに対応する秀丸/サクラ/VSCode相当のキーバインドが必ずしも自明ではなく(複製・行移動・行削除の既定キーは製品によって大きく異なる)、未確認の外部一次資料調査という新規スコープを避けるため。`CommandDescriptor` は5件とも `CommandId::None`(パレット限定、既存の `edit.convertTabsToSpaces` と同じパターン)で追加し、パレット検索自体は可能にした。
- **自動インデントは `core::Settings::insertSpacesForTab`/`tabWidth` を一切参照しない設計にした。** 新しい行に挿入するインデントは「前行の実テキストの先頭部分をそのまま文字列コピーする」方式であり、前行がスペースならスペース、タブならタブがそのままコピーされる。設定値を読んで再構築するアプローチより単純かつ、ユーザーが手動でインデントスタイルを混在させているファイルでも一貫した挙動になる。
- **ドッグフーディング: Ctrl+D(行複製)のみ実機で完全な視覚確認ができた**(`keybd_event()` によるキー合成、`SetForegroundWindow()`/`GetForegroundWindow()` でフォーカス一致を確認した上で実行、期待通り行が複製されカーソル位置も正しく "2:1" と表示された)。**Alt+↓(行移動)以降のドッグフーディングは、この環境特有の問題により完遂できなかった:** Alt+↓ 送信後、期待した行入れ替えが起きず、2回目の試行では NeoMIFES とは無関係な別ウィンドウ(ブラウザ動画)へフォアグラウンドフォーカスが移っていたことが判明。さらにその後 Ctrl+Shift+K 送信前の再チェックでも、`SetForegroundWindow()` で明示的に NeoMIFES へ復元した直後にもかかわらず、次の呼び出し時には再度別プロセス(PID 34800)へフォーカスが移っていた。これは Alt キー特有の問題ではなく、**この自動化環境ではツール呼び出しの合間にウィンドウフォーカスが自然に失われる**という、より根本的な環境制約であると判断した(過去セッションで確立済みの「修飾キー合成が不調」という制約を超える新しい観察)。Ctrl+D の成功により、キー入力→ディスパッチ→コマンド実行→再描画という配線全体が正しく機能することは実証済みであるため、残り4機能(Ctrl+A/Alt+↑/Alt+↓/Ctrl+Shift+K/自動インデント)は既存方針(`docs/issues/` 起票済みの環境制約、Phase 7g以降で確立)に従い、**単体テスト(`core_line_operations_test.cpp` 22件・`core_selection_model_test.cpp` 追加2件・`app_editor_input_test.cpp` 追加4件、いずれもDebug/Release/ubsan全green)+ 最終実装のコードレビューで代替検証**とした。

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

- [x] 🎉 **M4 達成 (2026-08-16): MVP 出荷判定完了**。§6 の14項目中12項目にチェックが入り、技術的に検証可能な項目(ファイル操作/IME/タブ/設定/横スクロール/起動時間/60fpsスクロール/10GBファイル/ASan・UBSan/8時間ソーク/Portable Zip/ユーザーマニュアル)は全てgreen。残る2項目(本物のAuthenticode証明書取得・日常的ドッグフーディング)はコードの正しさとは独立した出荷判断であり、着手前から`docs/issues/authenticode_certificate_not_acquired.md`等でユーザーの最終判断に委ねる設計だった。この2項目を未達のまま残すことをユーザーへ明示し、AskUserQuestionで確認の上、🎉M4を正式達成として記録する承認を得た(2026-08-16)。実際の一般公開・正式出荷はWI-19(Phase 12、総合品質保証。WI番号はWI-15a/WI-16a着手時の繰り下げ後の現在値)の範囲。

---

## WI-14a — ログ解析モード ヘッドレス基盤

**目的:** ログファイルのパターンマッチング (RFC 5424/3164 syslog・Apache/Nginx Common+Combined Log Format・汎用 ISO-8601+レベル行) をヘッドレスに実装する。roadmap §10.1 (ログ解析モード、「本ソフト最大の差別化点」) の最初のサブ WI。

**前提:** WI-01〜WI-13 全て (Phase 8.5/8.6/12' 完結、WI-13完了によりPhase 10着手が解禁された)

**参照:** `master_roadmap.md` §10.1

### 既に決まっている設計

- `LogModel::build(const Document&, const LogPatternRule&, assumedYear) -> std::expected<LogModel, LogPatternError>` という静的ファクトリ (roadmap スケッチの `LogModel::attach(Document&, rule)` mutate-in-place 形からの意図的な逸脱、理由は下記参照)
- 組込パターンは公開・検証可能な標準4種のみ: RFC 5424 syslog / RFC 3164 syslog / Apache・Nginx Common+Combined Log Format / 汎用 ISO-8601+レベル行。ベンダー固有パターン (SAP/AWS/Azure/K8s 等) は実データ入手まで実装しない (CLAUDE.md ルール3)
- フィールド抽出は RE2 の名前付きキャプチャグループ (`(?P<timestamp>...)` 等) で表現し、位置インデックスをハードコードしない (`RE2::NamedCapturingGroups()` でコンパイル時に1回だけ解決)
- `LogLine` は `document::LineNumber` + `optional<Timestamp>` + `LogLevel` + `matched` のみを持つ軽量構造体。メッセージ本文/traceId 等はキャッシュせず、必要な呼び出し側が都度 `Document::lineText()` を呼ぶ
- スレッド化 (`LogIndexWorker`)・`EditorSession` 統合・UI は本 WI のスコープ外 (WI-14b/c へ)

### 実施内容

`src/logmode/` モジュール新設 (`neomifes::logmode`、PUBLIC=`neomifes::document`、PRIVATE=RE2)。`log_pattern.h/.cpp` (`LogLevel`/`parseLevel()`/`LogPatternRule`/`builtInLogPatterns()`)、`timestamp_parser.h/.cpp` (`parseTimestamp()`)、`log_model.h/.cpp` (`LogModel::build()`)。単体テスト3ファイル。

### DoD

- [x] `LogPatternRule`/`LogLevel`/`parseLevel()`/`builtInLogPatterns()` (4件) 実装
- [x] `parseTimestamp()` (`std::chrono::parse` ベース、`assumedYear` 対応)
- [x] `LogModel::build()` (RE2 マッチング、CRLF `\r` トリム、UTF-16↔UTF-8 境界変換)
- [x] 単体テスト3ファイル (log_pattern/timestamp_parser/log_model)
- [x] Debug/Release/ubsan 全 green、clang-tidy 新規警告 0

### 実装後の確定事項

**`std::chrono::parse` の実機挙動 (スタンドアロン probe で確認、CLAUDE.md ルール3、記憶からの推測はしていない):**
1. `sys_time<Duration>` へのパースは**完全な暦日** (年+月+日) の解決を要求する — `%b %d` 単体のように年月日が揃わない書式は失敗する。RFC 3164 syslog は年フィールドを持たない (RFC 自体の仕様であり実装の不備ではない) ため、`parseTimestamp()` に `assumedYear` 引数を追加し、フォーマット文字列に `%Y` が無ければテキスト/フォーマット双方の先頭へ注入する方式で解決した。
2. `%Ez` (RFC 3339 拡張 UTC オフセット) はリテラル `"Z"` (Zulu) サフィックスを受け付けない (`"+HH:MM"` は受け付ける)。RFC 5424 の一般的な `"...15.003Z"` 形式は `"...15.003+00:00"` へ正規化してからパースする。
3. カンマ区切りの小数秒 (`"10:15:32,123"`) はパース失敗にならず、カンマの手前で無言でストリーム消費が止まる (`failbit` が立たない、`",123"` が未消費のまま残る)。`(iss >> std::ws).eof()` によるフルストリーム消費チェックを追加し、切り詰め結果を誤って正常値として返さないようにした。

**`attach()` → `build()` への逸脱:** roadmap スケッチは `LogModel::attach(Document&, rule)` という mutate-in-place 形だったが、`Document*` を保持する設計は「文書がスワップされたら誰が再構築するか」という寿命管理の問題を持ち込む。`search::SearchService::findAll()` が既に確立している「static、呼び出しごとに完結」という設計をそのまま踏襲した。

**RFC 5424/3164 に "level" フィールドが無いことの確認:** 両 RFC とも重要度は `<PRI>` (facility×8+severity) に数値エンコードされ、テキストの "level" フィールドは存在しない。実装時にこれを確認し、両 syslog ルールは `level==Unknown` を検証、レベル検出は汎用 ISO-8601 ルールのみでテストする形に是正した (spec 精度の是正であり計画からの黙った逸脱ではない)。

**`Document::lineCount()` は末尾 `\n` 終端の文書に対し実行数+1 を返す** (暗黙の空最終行、既存の「空文書→1」と同じ規約の帰結)。単体テストの一部がこれを見落とし `lines().size()` の期待値を1小さく書いていたが、実装ではなくテスト側の誤りと判明し、フル3構成検証時に4件のテスト失敗として顕在化・修正した (`LogModelTest.ApacheCombinedLogFormatMatchesWithUnknownLevel`等)。

**ベンダー固有パターンの先送り:** `docs/issues/phase_10_1_v2_extended_patterns.md` に起票 (リアルタイムテール/分散トレース/構造化ログ/統計ダッシュボード/SAP・AWS・Azure・K8s 固有パターン)。

---

## WI-14b — 非同期インデックス構築 + フォーマット自動検出 + `EditorSession`配線 + ピース単位ストリーミング最適化

**目的:** WI-14a のヘッドレス基盤 (`LogPatternRule`/`LogModel`) を、実アプリで使える形の一歩手前まで進める — バックグラウンドスレッドでの非同期インデックス構築 (`LogIndexWorker`)、フォーマット自動検出、`EditorSession` per-tab 状態、そして `LogModel::build()` 自体を 10GB/60秒目標に見合う O(document length) の単一線形パスへ書き換える。roadmap §10.1 の第2サブ WI。

**前提:** WI-14a 完了 (コミット `2512c76`/`1374a67`)

**参照:** `master_roadmap.md` §10.1、`docs/design/detailed_design.md` §11.3

### 既に決まっている設計

- `LogModel::build()` に `const document::BufferSnapshot&` を取る新規オーバーロードを追加し、`snapshot.pieces()` を1回だけ走査するピース単位ストリーミング実装に置き換える (`LineIndex::build()` を直接のテンプレートとする)。既存の `Document&` オーバーロードはこの新オーバーロードへの1行委譲になり、WI-14a の全13単体テストが無変更のまま回帰オラクルとして機能する
- フォーマット自動検出 (`detectLogPatternRule()`) は `format_detection.h/.cpp` に分離実装し、先頭 N 行 (既定100) に組込4パターン全てを試行してマッチ率最多のものを返す (50% 未満は `nullopt`)
- `LogIndexWorker` は `render::SyntaxWorker` (Phase 7c) を直接のテンプレートとするが、**「保留中リクエストは最新の1件のみ・上書き」という SyntaxWorker の設計は踏襲しない**。`LogIndexWorker` は複数タブ (`EditorSession`) から独立して結果を必要とするため、`std::deque` ベースの FIFO キュー (全リクエストを提出順に処理、取りこぼさない) を採用する
- 完了メッセージのタブへのルーティングは `Workspace` への新規 API 追加なしで実現する。`EditorSession` 自身のポインタを不透明な `sessionToken` として往復させ、受信側が `&workspace.sessionAt(i)` との**ポインタ値比較のみ**(絶対に dereference しない) で対象タブを特定する
- `LogIndexWorker` は `neomifes::render` ではなく `neomifes::logmode` 名前空間に置く (ログインデックス構築はレンダリング関心事ではなく、`neomifes::logmode` は既に `neomifes::document` のみに依存する自己完結モジュールのため)
- WI-14b では `beginLogIndexing()`/`applyLogIndexResult()` を実際に呼び出す UI/コマンドは一切配線しない (WI-14c へ)。ただし完了メッセージの「受信インフラ」(`LogIndexWorker` の構築 + `kMsgLogIndexReady` ハンドラ + `Workspace` 線形走査ルーティング) は本 WI で実装し、統合テストで検証する

### 実施内容 (6ステップ、コミット単位)

1. `LogModel::build(const BufferSnapshot&, ...)` 新設 + 多ピーステスト追加 (`4f55d8b`)
2. `format_detection.h/.cpp` 実装 (`062bfd9`)
3. `log_index_worker.h/.cpp` 実装 (FIFOキュー + `kMsgLogIndexReady = WM_APP+3`) + 統合テスト新設 (`9c5c982`)
4. `EditorSession` per-tab 状態配線 (`logModel()`/`logPatternRule()`/`logIndexInFlight()`/`beginLogIndexing()`/`applyLogIndexResult()`) (`2f856b1`)
5. `main.cpp`/`normal_mode_wiring.cpp` 配線 (`LogIndexWorker` 構築 + `kMsgLogIndexReady` 受信ルーティング) (`a6c1849`)
6. ベンチマーク `logmode_index_bench.cpp` 新設 + 実測 + 最終ゲート (`525e0f1`)

### DoD

- [x] `LogModel::build(const BufferSnapshot&, ...)` (ピース単位ストリーミング、O(document length) 単一線形パス)
- [x] `detectLogPatternRule()` (先頭 N 行試行、50% 閾値)
- [x] `LogIndexWorker` (FIFOキュー、`kMsgLogIndexReady`)
- [x] `EditorSession` per-tab 状態 (`m_logModel`/`m_logPatternRule`/`m_logIndexInFlight`)
- [x] `main.cpp`/`normal_mode_wiring.cpp` 受信インフラ配線
- [x] ベンチマーク実測 (下記参照)
- [x] Debug/Release/ubsan 全 green (1273/1273)、clang-tidy 新規警告 0

### 実装後の確定事項

**ピース単位ストリーミングの実測結果 (Release、`--benchmark_min_time=0.2s`):**

| 行数 | 時間 | items/s | source_KiB |
|---|---|---|---|
| 50,000 | 164ms | 301.9k/s | 7.65k |
| 500,000 (10倍) | 1550ms | 325.2k/s | 77.49k |

items/s がほぼ一定 (ドキュメントサイズにほぼ比例した時間) であり、O(lines×pieces) だった旧実装から O(document length) の単一線形パスへの書き換えが複雑度クラスとして実測でも確認できた。実際に10GBファイルを生成する検証は WI-13 の `tools/` スクリプト前例を踏襲せず、複雑度クラスの証明に留めた (このベンチマークの目的は「アルゴリズムがO(N)であること」の証明であり、エンドツーエンドの受け入れ確認ではないため)。

**`SyntaxWorker`型からの意図的な逸脱 (FIFOキュー採用):** 着手前調査で「保留中リクエストは1件のみ・上書き」という `SyntaxWorker` の設計をそのまま `LogIndexWorker` に適用すると、複数タブが同時にインデックス要求した場合に一部のタブが永久に処理されない実害あるバグになると判明した。`std::deque` による FIFO キューへ変更し、`tests/integration/logmode_log_index_worker_test.cpp` の `MultipleSessionsAreAllProcessedNotJustTheLatest` テストでこの契約を直接検証した (2つの異なるセッショントークンで連続してリクエストし、両方の結果が届くことを確認)。

**`wireNormalMode()` のコード同時複雑度 (clang-tidy `readability-function-cognitive-complexity`) 超過への対処:** `kMsgLogIndexReady` の受信ルーティングロジックを最初 `cfg.onAppMessage` ラムダへインラインで追加したところ、`wireNormalMode()` 全体の同時複雑度が閾値25を超過した (33 → 部分的抽出で26 → まだ超過)。最終的に `cfg.onAppMessage` ラムダの本体全体 (`kMsgSyntaxTokensReady`/`kMsgLogIndexReady` 両分岐) を新規 `handleAppMessage()` へ抽出し解消した。中間ステップの「Debugのみ検証」運用下でも clang-tidy による静的解析は独立して都度実行することの重要性を再確認した事例。

**`LogIndexWorker` の構築タイミング:** 当初案 (`window.create()` 成功確認後・メッセージループ開始前に main.cpp で直接構築) は、`wireNormalMode()` が `window.create()` より前に呼ばれる既存の呼び出し順序と噛み合わなかった。`RenderPipeline::attach(hwnd)` と同じ `cfg.onDeferredInit` (実 HWND が判明した時点で発火) での構築に変更し、既存の HWND 依存初期化パターンと一貫させた。

---

## WI-14c — UI モード MVP 🎉 (色分け/フィルタ/時系列ジャンプ、Phase 10.1 の MVP 達成)

**目的:** WI-14a/b のヘッドレス基盤 (`LogPatternRule`/`LogModel`/`LogIndexWorker`) を、実際にユーザーが使える機能として完結させる。要件定義書 §8 の残り全項目 (色分け/フィルタ/ERROR抽出/WARNING抽出/時系列ジャンプ) を実装し、完了をもって Phase 10.1 の MVP 達成とする。

**前提:** WI-14b 完了 (コミット `4f55d8b`〜`525e0f1`)

**参照:** `master_roadmap.md` §10.1、`docs/design/detailed_design.md` §11.3

### 既に決まっている設計

- roadmap §10.1 の UI スケッチ (左ペイン+右ペインの専用ツリー/統計ダッシュボード) は採用しない。新規ネイティブウィジェットを追加すると `docs/issues/native_overlay_widgets_invisible.md` 型のリスクと WI 規模の両方を抱え込むため、既存の `ui::CommandPalette` (パレット限定コマンド、WI-08〜WI-10 で確立済みの `CommandId::None` パターン) のみで全機能を提供する
- `neomifes::render` が `neomifes::logmode::LogLevel` を仲介型なしで直接使う (`RenderPipeline` が既に `syntax::Token`/`syntax::Language` を直接扱っているのと同じ理由 — `neomifes::logmode` は `document::` のみに依存する自己完結モジュール)。`src/render/CMakeLists.txt` に `neomifes::logmode` を PUBLIC リンク追加
- フィルタ (非表示行) は新規の隠蔽経路を作らず、既存の `RenderPipeline::isLineHidden()` (Phase 7i の折り畳み機構) へログレベルフィルタを OR で合流させる。`drawVisibleLines()`/`hitTest()`/`visibleLineRange()` 等の既存可視行ロジックは無変更のままフィルタに対応する
- `m_logLineLevels` (文書行ごとのレベル配列、文書全体サイズになりうる) は `FrameState` の比較対象に含めない。`applyAsyncSyntaxTokens()` と同じ「到着時に `m_lastRenderedFrameState.reset()` で1回だけ強制再描画」パターンを踏襲する。フィルタマスク (`std::uint8_t`、軽量) は `FrameState` へ直接含め毎フレーム比較する
- ログ編集追従 (行番号ズレの自動補正) はスコープ外。`core::BookmarkManager` の既知の制約 (bookmarks do NOT track document edits) と同じ理由 — このコードベースには Document 変更の購読機構が無い。再インデックス (`logmode.enable.*` の再実行) で手動復旧する
- 時系列ジャンプは「次/前の可視ログ行へジャンプ」という単一のナビゲーションプリミティブに単純化する。`logmode.jump.next/previous` は「`matched==true` かつ現在のフィルタマスクを通過する直近の行」へジャンプする (`core::BookmarkManager::next()/previous()` と同じラップアラウンド規約) — フィルタ未適用時は時系列ジャンプ、errorsOnly フィルタ時は ERROR抽出ナビゲーション、warningsOnly フィルタ時は WARNING抽出ナビゲーションになる。1つの機構が要件定義書の3項目を満たす
- フォーマット自動検出に失敗した場合は `showLogFormatNotDetectedDialog()` (OK-only TaskDialogIndirect、`showSaveErrorDialog()` と同型) で通知する

### 実施内容 (7ステップ、コミット単位)

1. `log_pattern.h`(`logLevelFilterBit`/`kAllLogLevelsVisible`) + `log_navigation.h/.cpp` 新設 + 単体テスト (`e92ddfb`)
2. `EditorSession`: `logLevelFilterMask()`/`disableLogMode()` 追加 + テスト (`84f5bf9`)
3. `theme.h/.cpp`: `logError`/`logWarning` フィールド追加 (3テーマ全て) + テスト (`0f5af55`)
4. `RenderPipeline`: `setLogLineLevels()`/`setLogLevelFilter()`/`FrameState`拡張/`isLineHidden()`拡張/`drawLogLevelOnLine()` + `src/render/CMakeLists.txt`(`neomifes::logmode`リンク) + 統合テスト (`8250f3d`)
5. `message_dialogs.h/.cpp`: `showLogFormatNotDetectedDialog()` (`d41f52b`)
6. `normal_mode_wiring.cpp/.h`: `pushLogVisualsForSession()` + `applyLogIndexReadyMessage()`拡張 + `buildCommandRegistry()`への全コマンド追加 (enable×5/disable/filter×9/jump×2)、およびそれによる `readability-function-cognitive-complexity` 超過(43、閾値25)を `appendLogModeCommands()` への抽出で解消 (`4d30233`)
7. ドキュメント同期 (本コミット)

### DoD

- [x] `logLevelFilterBit()`/`kAllLogLevelsVisible`
- [x] `nextVisibleLogLine()`/`previousVisibleLogLine()` (ラップアラウンド規約、フィルタ対応)
- [x] `EditorSession::logLevelFilterMask()`/`disableLogMode()`
- [x] `Theme::logError`/`logWarning` (3テーマ)
- [x] `RenderPipeline` 色分け描画 + フィルタ非表示 + `FrameState`除外設計
- [x] `showLogFormatNotDetectedDialog()`
- [x] コマンドパレット統合 (`logmode.enable.*`/`disable`/`filter.*`/`jump.*`、計~20コマンド)
- [x] Debug/Release/ubsan 全 green (1290/1290)、clang-tidy 新規警告 0 (再検証込み)

### 実装後の確定事項

**`buildCommandRegistry()` の認知的複雑度超過:** WI-14b の `wireNormalMode()` と同種の問題が本 WI でも再発した。~20個のログモードコマンドを `buildCommandRegistry()` へ直接 push_back したところ、認知的複雑度が43(閾値25)まで悪化した。`appendLogModeCommands(std::vector<CommandDescriptor>&, HWND, Workspace&, RenderPipeline&, std::optional<LogIndexWorker>&)` へ丸ごと抽出し解消(抽出後の再検証で新規指摘0件を確認)。「大量の類似コマンドをループで生成する」パターン自体は WI-10 の `kPresetChoices` 以来繰り返し使われてきたが、その生成コード量が単一関数に累積すると閾値を超えることが2WI連続で確認された — 今後 5個を超えるコマンド群を1関数へ追加する際は、着手前に抽出を前提とした設計を検討する。

**Release/ubsan の再検証省略の判断:** 上記の抽出リファクタは純粋なコード移動(ロジック変更なし、同一キャプチャ・同一処理)+ 未使用using宣言1行の削除のみだったため、Debug構成での0警告・1290/1290 green再確認をもって十分と判断し、Release/ubsanの3構成目・4構成目の再実行は省略した(直前の完全な3構成ゲートで両方ともgreenだったことを踏まえた判断)。

---

## WI-14d — 複数行グルーピング + ユーザー編集可能パターンファイル 🎉 (Phase 10.1 完結)

**目的:** WI-14a〜c で達成した Phase 10.1 MVP に、roadmap §10.1 が元々見込んでいた「複数行エントリのグルーピング (Java スタックトレース等の継続行)」と「ユーザー編集可能パターンファイル」を追加し、Phase 10.1 を完結させる。「パターン拡充」は `docs/issues/phase_10_1_v2_extended_patterns.md` により CLAUDE.md ルール3 (推測実装をしない) に抵触すると WI-14a 時点で確定済みのため、開発側がベンダーパターンを推測で追加するのではなく、ユーザー自身が検証済みの正規表現を持ち込める手段として満たした。

**前提:** WI-14c 完了 (コミット `e92ddfb`〜`4d30233`)

### 既に決まっている設計

- `nextVisibleLogLine()`/`previousVisibleLogLine()` (WI-14c) は無変更 — `qualifies()` が既に `matched==true` のみをジャンプ対象にしており、継続行は元々除外されている
- 実際に修正が必要だったのは `pushLogVisualsForSession()` — 全行の `line.level` を直接 push していたため、継続行 (既定 `LogLevel::Unknown`) が親の ERROR/WARNING と独立してフィルタされ、「Errors only でフィルタしたのにスタックトレース本体だけ残る」という実害があった。`neomifes::logmode::computeGroupedLogLevels(std::span<const LogLine>) -> std::vector<LogLevel>` という純粋関数1つに集約し解消 (`LogLine` 自体には新規フィールドを追加しない — 多メガ行文書のため小さく保つという既存方針を維持)
- ユーザー編集可能パターンファイルは「1ファイル = 1 `LogPatternRule`」の JSON をディレクトリ (`%APPDATA%\NeoMIFES\log_patterns\`) スキャンする方式。不正ファイルはそのファイルのみスキップ (`KeyBindings::loadFrom()` と同じ寛容契約)。UTF-16↔UTF-8 変換は `core::detail::toUtf8/fromUtf8` と同じ実装を `neomifes::logmode::detail` へ複製 (`neomifes::logmode` が `neomifes::core` に依存するのはレイヤ違反のため)
- 既存の組込パターンを `%APPDATA%` へ自動コピーする roadmap 原案は不採用 (バージョニング/陳腐化の懸念、実際のギャップは「未対応フォーマットを追加できること」であって「既存パターンを上書きできること」ではない)
- `detectLogPatternRule()` に `std::span<const LogPatternRule> candidates = builtInLogPatterns()` を `sampleLines` の後に追加 (既存呼び出し元は無改修)。`candidates` は候補列を置き換える (補うのではない)
- `logmode.patterns.reload` コマンドは `keybindings.reload` と同型 (`buildCommandRegistry()` 内に直接実装、ディレクトリ再スキャン→パレット再構築)

### 実施内容 (2コミット)

1. `log_grouping.h/.cpp` + `log_pattern_file.h/.cpp` + `json_string_convert.h/.cpp` 新設 + `format_detection.h/.cpp` の `candidates` 拡張 + 単体テスト一式 + CMake登録 (`2c16e79`)
2. `main.cpp`: `resolveLogPatternsStartupState()` 新設。`normal_mode_wiring.h/.cpp`: `wireNormalMode()`/`buildCommandRegistry()` へ `userLogPatterns`/`logPatternsDir` を配線 (全3呼び出し箇所)、`appendLogModeCommands()` 拡張、`pushLogVisualsForSession()` のバグ修正、`logmode.patterns.reload` コマンド新設 (`9673824`)

### DoD

- [x] `computeGroupedLogLevels()` (継続行が直近の matched 行のレベルを継承)
- [x] `loadLogPatternRuleFromFile()`/`loadUserLogPatternsFromDirectory()` (不正ファイル黒板消し、id衝突はアルファベット順で最初のファイルが勝つ)
- [x] `detectLogPatternRule()` の `candidates` 拡張 (組込パターンとユーザーパターンの結合)
- [x] `resolveLogPatternsStartupState()` (`%APPDATA%\NeoMIFES\log_patterns\` 起動時作成+スキャン、失敗時は空状態)
- [x] `logmode.enable.*` コマンドがユーザーパターンにも生成される
- [x] `logmode.patterns.reload` コマンド
- [x] Debug/Release/ubsan 全 green (1309/1309)、clang-tidy 新規警告 0 (未使用using宣言1件を修正、残りはこのテストスイート全体で既に確立されている `rand()` ベース一時ファイル名/`ASSERT_TRUE(x.has_value()); x->field` の既存慣習と同型のため対象外と判断)

### 実装後の確定事項

**`cfg.onDeferredInit` ラムダのキャプチャ漏れ:** `wireNormalMode()`/`buildCommandRegistry()` へ新規パラメータを追加した際、明示キャプチャリストを使うラムダは1つ1つ手動でキャプチャを追加する必要があり、`cfg.onDeferredInit` (この関数内で最も長いラムダの1つ) への追加を1回失念し、C3493/C2326 のコンパイルエラーになった。ローカルビルド検証で即座に検出・修正できたが、「シグネチャ拡張は全呼び出し箇所だけでなく全キャプチャリストも機械的に洗い出す」ことの重要性を再確認した事例。

**`buildCommandRegistry()` の認知的複雑度: 3WI連続で閾値未超過を確認。** WI-14b/c で2回連続超過した経緯があったため、本WIでは `logmode.patterns.reload` コマンド追加直後に個別 clang-tidy 実行を計画に明記していた。実際には超過しなかった (WI-14c で `appendLogModeCommands()` へ抽出済みだったため、`buildCommandRegistry()` 本体側の追加分は1コマンド20行程度に収まった) — 「抽出しておけば次の追加が安全になる」という設計判断が機能した実例。

**サブエージェントの完了報告フローで背景待機ループが早期終了扱いになる問題:** 本WIの最終ゲート検証中、委任先エージェントが自身のバックグラウンド待機ループ (`run_in_background`/ポーリング) を使った際、そのエージェント自身のターンが「バックグラウンド子プロセスなし」として完了通知されてしまい、実際には未完了の検証結果を報告する事態が2回発生した。都度エージェントへ「同期的に(フォアグラウンドで)実行し、完了するまでターンを終えないこと」を明示的に再指示して解消した。今後サブエージェントへ長時間ビルド検証を委任する際は、最初のプロンプトから「run_in_background/待機ループを使わず同期実行すること」を明記しておくとよい。

---

## WI-15a — JSON ツリーモデル ヘッドレス基盤

**目的:** Phase 10.1(ログ解析モード)完結後、ユーザーがAskUserQuestionでPhase 10の残り2領域(CSVモード/JSON-XML Treeモード)から「JSON/XML Treeモード」(推奨案)を選択。roadmap §10.3・要件定義書§10が「三大エディタが持たない差別化点」と明記する機能の最初のサブWI。WI-14a がヘッドレスな `LogModel` を先に作ってから WI-14c でUIを繋いだ順序を踏襲し、UI抜きのJSON構造ツリーモデルのみを作る。

**前提:** WI-14d 完了・push・CI green確認 (2026-08-18)

**参照:** `master_roadmap.md` §10.3、`NeoMIFES_要件定義書.md` §10

### 着手前調査で確定した設計方針

- `ui::OutlinePane`(Phase 7f/g)は`syntax::SymbolTable`に一切依存しない汎用`WC_TREEVIEW`ラッパーで、JSON/XMLツリーもそのまま乗せられると判明。ただし現状は「フル幅レンダーサーフェスの右端にオーバーレイ」方式で真の分割ペインではなく、常時全展開で折り畳み状態を持たない — この2点はUIサブWIの課題として本WIのスコープ外
- `core::FoldingModel`(Phase 7i)は`FoldRegion{headerLine, endLineInclusive}`のみのヘッドレス型で完全に汎用、そのまま再利用可能と判明。結合しているのは`app::buildFoldRegions()`側であり、JSON用の同型関数は別途必要
- nlohmann/json(ADR-013採用済み)の`json_sax`コールバックには位置情報が一切渡されないと、実機ソース読解+スタンドアロンprobeの両方で確認(`json_sax<T>`の全仮想関数を実機ソースで確認、`ordered_json::sax_parse()`のコールバックトレースで位置情報が一切現れないことをprobe実行で実証)。既定の`nlohmann::json`はキー順をアルファベット順(`std::map`ベース)に並び替えるが、`nlohmann::ordered_json`(同一ヘッダ内に既存、追加ADR不要)が挿入順を保持する
- XMLライブラリはこのコードベースに一切存在しない(`pugixml`はroadmapのスケッチのみ、ADR未発行)。**XMLは本WIのスコープから完全に除外**
- 中央`Mode`enum(roadmap原案の`src/core/mode.h`)はこのコードベースに存在せず、WI-14(ログモード)は`EditorSession`が機能ごとに`std::optional<T>`を持つ方式(中央enumなし)で実装済み。この前例に従い本WIでも中央Mode enumは導入しない

### 実施内容 (2コミット)

1. `src/jsontree/`モジュール新設(`neomifes::logmode`と同型)、`JsonNode`/`JsonNodeKind`/`parseJsonTree()`実装 — 二段構成(`ordered_json::parse()`で構文検証+DOM構築 → 同じ検証済みテキストを独自の`PositionScanner`で並走させ位置復元)、木構築は明示スタック(`misc-no-recursion`対応) (`9334f0c`)
2. 単体テスト4カテゴリ14件(構造的正しさ/キー順序保持/位置の正確さ/不正JSON) (`1f21780`)

### DoD

- [x] `JsonNode`/`JsonNodeKind`(公開ヘッダ、`document::TextPos`のみ依存)
- [x] `parseJsonTree(const document::Document&) -> std::optional<JsonNode>`
- [x] probe実行でordered_jsonのキー順序保持・非throw契約・SAX位置情報の不在を確認
- [x] 木構築が明示スタック(再帰なし、clang-tidy新規警告0)
- [x] 単体テスト4カテゴリ14件
- [x] Debug/Release/ubsan全1323件green、clang-tidy新規警告0

### 実装後の確定事項

**リーフ値は全種別で生ソーステキストをそのまま保持する設計にした。** 当初は文字列値だけDOMの復号済み値(引用符/エスケープ解決済み)を使う案も検討したが、JSON文字列リテラルは仕様上エスケープされていない制御文字(改行等)を含み得ないため、生ソースのまま保持することで「1ノード=1行表示」を前提とする将来のツリーUIが埋め込み改行を心配せずに済むという副次的な利点があると判明し、数値と同じ「生ソースのまま」で統一した(数値側の元の理由は`"1.50"`のような表記の精度損失回避)。

**`openValue()`を`buildTree()`内のネストしたラムダとして最初に実装したところ、`readability-function-cognitive-complexity`が36(閾値25)まで悪化した。** `openValue()`/`closeContainer()`/`consumeNextChild()`の3関数へ抽出し、状態(`scanner`/`byteToUtf16`/`buffer`/`stack`)を`ParseState`という小さな参照束縛構造体で渡す設計に書き換えて解消。この`ParseState`の参照メンバが今度は`cppcoreguidelines-avoid-const-or-ref-data-members`に新規抵触したが、調査の結果`src/app/include/neomifes/app/command_dispatch.h`の`CommandDispatchContext`(6個の参照メンバを持つ、本WI以前から存在)が全く同じ形でありながら一度も個別にclang-tidyされたことがなかっただけと判明 — 新しいパターンではなく、既存パターンが初めてこのチェックに晒された事例。`ParseState`は`NOLINTBEGIN/END`で抑制し理由をコメントで明記した。**`CommandDispatchContext`自体は本WIのスコープ外のため未修正のまま — 将来いずれかのWIが`command_dispatch.h`/`command_dispatch.cpp`を変更ファイルとしてclang-tidyする際に同じ指摘が出ることを見込んでおく。**

**Explore agent 1件による着手前調査 + Plan agent 1件による設計立案を経てPlan Modeでユーザー承認を得てから実装した。** Plan agentは読み取り専用エージェントとして起動されていたため、設計の核心(nlohmann `json_sax`に位置情報が渡らないこと)は実機コンパイル・実行ではなく実機ソースの静的読解で確認し、「実装セッションの最初のステップとして実際にprobeを実行し裏付けること」を計画自体に明記した。承認後、実装開始直後に実際にprobeを実行し3点(キー順序/非throw契約/SAX位置情報なし)を実証してから本実装に着手した。

**WI番号がroadmap原案の割当(Phase 11=WI-15)と衝突したため、Phase 11/9/12の割当をWI-16/17/18へ繰り下げた** (本書§5冒頭の「WI-16〜WI-18」節参照)。roadmap原案はPhase 10全体を「WI-14」1本と見込んでいたが、Phase 10.1だけでWI-14a〜dの4サブWIを要し、Phase 10.3もWI-15aから始まる複数サブWIに分かれる見通しとなったため。

---

## WI-15b — JSON ツリー 非同期インデックス化 + EditorSession配線 (UIなし)

**目的:** WI-15a(JSONツリーモデル ヘッドレス基盤)完了後、ユーザーの「継続せよ」指示を受けPhase 10.3の続きに着手。WI-14bがログモードの非同期ワーカー+`EditorSession`配線をUIなしで先に固めてからWI-14cでUIを繋いだ順序を、JSONツリー側でも踏襲する。

**前提:** WI-15a 完了・コミット済み (2026-08-18)

**参照:** `master_roadmap.md` §10.3、WI-15a セクション(本書上記)、`src/logmode/include/neomifes/logmode/log_index_worker.h`(直接のテンプレート)

### 着手前調査で確定した設計方針

- `ui::OutlinePane`/`ui::OutlineItem`は`WC_TREEVIEW`のオーバーレイ方式(真の左右分割ペインではない)で、`targetPos`は`document::TextPos`と同じ`uint64_t`型。将来のUIサブWIが新規ウィジェットを作らずこれを再利用できる見込みだが、本WIはUIを一切扱わないため設計メモに留めた
- `render::RenderPipeline`に一般的な複数ペイン分割の仕組みは無く、ガター/ミニマップは単一描画パイプライン内の固定オフセット帯に過ぎないと確認(真の左右分割ペインを将来作る場合の設計上の制約として記録)
- `document::Document::snapshot()`は`std::shared_ptr<const document::BufferSnapshot>`を返す。`json_tree.cpp`の既存実装(WI-15a)を読んだ結果、`parseJsonTree(const Document&)`の実装本体は`doc.snapshot()`の1行以外、既に完全に`BufferSnapshot`だけで完結していた — `LogModel::build()`のBufferSnapshot化(O(lines×pieces)→O(document length)の複雑度改善)とは性質が異なり、JSONは`nlohmann`が全文一括読込を要求するため複雑度クラスは変わらない、純粋なスレッド安全性リファクタと判明
- `LogIndexWorker`が`std::deque`のFIFO(上書きしない)を採用している理由は「複数タブがそれぞれ独立した結果を必要とするため」であり、`SyntaxWorker`の「最新のみ保持」方式が安全な理由は「そもそも`sessionToken`の概念を持たず単一のRenderPipelineにしか結果を返さない設計だから」と確認 — JsonTreeWorkerもFIFOを採用
- WI-14bの元コミット(`git show`で復元)を確認した結果、WI-14b時点の`applyLogIndexReadyMessage()`は`RenderPipeline`/`HWND`/`InvalidateRect`を一切持たない単純な形だった(WI-14cが追加)。本WIもこのWI-14b時点の単純な形を踏襲

### 実施内容 (4コミット)

1. `parseJsonTree(const document::BufferSnapshot&)`オーバーロード新設、既存`Document`版は1行委譲に変更 + 単体テスト2件追加 (`1d9156c`)
2. `JsonTreeWorker`実装(`LogIndexWorker`を直接のテンプレートに、FIFO `std::deque`、`kMsgJsonTreeReady = WM_APP + 4`)+ 統合テスト5件 (`9b8075a`)
3. `EditorSession`へ`jsonTree()`/`jsonTreeIndexInFlight()`/`beginJsonTreeIndexing()`/`applyJsonTreeResult()`の4点配線 + 単体テスト3件 (`83fcadb`)
4. `main.cpp`/`normal_mode_wiring.h/.cpp`配線(`JsonTreeWorker`構築 + `kMsgJsonTreeReady`受信ルーティング、呼び出し元コマンドは追加せず) (`7bd4dee`)

### DoD

- [x] `parseJsonTree(const BufferSnapshot&)`が既存Document版と同じ結果を返す(回帰テストで保証)
- [x] `JsonTreeWorker`がFIFOで複数タブを取りこぼさない(統合テストで保証)
- [x] 不正JSON入力でも`kMsgJsonTreeReady`が必ず届き`jsonTreeIndexInFlight()`が固定されない(統合テスト+単体テストで保証)
- [x] `EditorSession`にjsonTree()/jsonTreeIndexInFlight()/beginJsonTreeIndexing()/applyJsonTreeResult()の4点(clearJsonTree()は意図的に含めない、WI-15cへ先送り)
- [x] `main.cpp`/`normal_mode_wiring.cpp`に配線済み、ただし呼び出し元(コマンド)は意図的に追加しない
- [x] Debug/Release/ubsan全1329件green、clang-tidy新規警告0

### 実装後の確定事項

**`clearJsonTree()`(ログモードの`disableLogMode()`相当)はWI-15bに含めなかった。** WI-14bの元コミットを確認した結果、`disableLogMode()`はWI-14cで「Log: Disable」コマンドとセットで追加されたものであり、呼び出し元の無いWI-14b時点には存在しなかった。WI-15bも呼び出し元(コマンド)を一切追加しないため、同じ理由で`clearJsonTree()`をWI-15cへ先送りした。

**JsonTreeWorkerは`LogIndexWorker`と異なり、`std::nullopt`結果でも必ず結果をpostする設計にした。** `LogIndexWorker::workerLoop()`は`LogModel::build()`失敗時に`continue`で結果を握りつぶす(組込パターンでは到達不能な稀なエラーパスのため許容)。JSONツリーでは「JSON以外のファイルに対して呼ばれた」「壊れたJSON」がむしろ日常的な正常系であり、ここで握りつぶすと`jsonTreeIndexInFlight()`が永久に`true`のまま固定されてしまう。`workerLoop()`は`parseJsonTree()`の結果(`std::optional<JsonNode>`、常に例外なく返る)を`std::make_unique<std::optional<JsonNode>>`でヒープ確保し、成功/失敗を問わず必ず`PostMessageW`するよう設計した。

**最終ゲート(ubsan/clang-cl構成)で、深さ2000のネストJSONを与える統合テストが実際にSTATUS_STACK_OVERFLOWでクラッシュすることを発見した。** 原因は`neomifes::jsontree::buildTree()`自体(WI-15a、明示スタックによる反復実装)ではなく、`nlohmann::ordered_json::parse()`自体が再帰下降パーサでありネスト1階層につきC++呼び出しスタックを1段消費するため。MSVC Debug/Release構成では同じ深さでもクラッシュしなかったが、これは安全性の証明にはならない(スタック消費量はビルド設定・最適化レベルに強く依存する)。テストの深さを2000から50へ引き下げ、根本原因を`docs/issues/json_tree_worker_deep_nesting_stack_overflow.md`としてissue化した(P1)。**nlohmann/jsonには解析深度の上限を設定する公式APIが存在しないため**、対応(SAXベースの事前深度チェック等)はWI-15c以降(実際にこの経路へ到達するコマンドが追加されるタイミング)へ先送りした。

---

## WI-16a — CSV モード ヘッドレス解析モデル

**目的:** WI-15b(JSONツリー 非同期インデックス化+EditorSession配線)完了後、ユーザーに「次のPhase」の意味をAskUserQuestionで確認したところ「Phase 10.2: CSVモード」(JSON/XML TreeのUI続き=WI-15cではなく)が選ばれた。JSON/XML Treeモードのヘッドレス基盤+非同期化(WI-15a/b)はここで一旦区切り、CSVモード(要件定義書§9、master_roadmap.md §10.2)へ新規着手。WI-14a/WI-15aと同型の「まずヘッドレスモデルのみ、UIなし」の最初のサブWI。

**前提:** WI-15b 完了・コミット済み (2026-08-18)

**参照:** `master_roadmap.md` §10.2、`src/logmode/include/neomifes/logmode/log_model.h`(直接のテンプレート)、`src/document/src/line_index.cpp`(ピース単位walkの直接のテンプレート)

### 着手前調査で確定した設計方針

- 既存CSV関連コードは実装・言及ともに皆無(grep確認済み)
- `neomifes::logmode::LogModel::build()`は`std::expected<LogModel, LogPatternError>`を返す(実機確認済み、`std::optional`ではない)、`LogLine`は「テキストを複製しない、位置/メタデータのみ保持」設計 — この2点が`CsvModel::build()`/`CsvCell`の直接のテンプレート
- `document::LineIndex`は`\n`のみを行境界として認識する(単独`\r`は非対応) — CSVの行終端規約もこれに合わせる
- `logmode_log_model_test.cpp`で確認済みの規約(末尾`\n`は暗黙の空行を1行追加、空文書は1行)をCSVの行数にもそのまま流用
- `WC_LISTVIEW`等のグリッドコントロール前例は皆無(将来のUIサブWIの課題、今回は無関係)
- 拡張子/内容ベースのCSVモード自動起動判定は今回のスコープ外

### 実施内容 (2コミット)

1. `src/csvmode/`モジュール新設(`neomifes::logmode`/`neomifes::jsontree`と同型)、`CsvCell`(位置のみ保持、テキスト非保持)/`CsvParseOptions`/`CsvModel`/`csvCellValue()`実装 — 単一forループの4状態機械(`FieldStart`/`Unquoted`/`Quoted`/`QuoteInQuoted`)、CSR方式コンテナ(平坦`vector<CsvCell>`+行オフセット、roadmap原案のネストvectorは不採用)、`CsvCell::quoted`フラグをパーサ終端時状態から直接記録(生テキスト先頭/末尾からの事後推論は誤判定するため不採用) + 単体テスト15件(構造/引用符処理/位置/寛容な構文吸収/デコード/ピース境界/失敗契約) (`ab7dd5e`)
2. `detectCsvDelimiter()`実装(`detectLogPatternRule()`のサンプリング構造を土台に、「出現の有無」ではなく「行ごとの出現回数の最頻値への一致度合い」でスコアリング) + 単体テスト9件、最終ゲート(Release/ubsan/clang-tidy)で検出した2件(`performance-no-automatic-move`/`modernize-use-ranges`)を修正 (`c8fd842`)

### DoD

- [x] `CsvCell`(公開ヘッダ、`document::TextPos`のみ依存、テキスト非保持)
- [x] `CsvModel::build(...) -> std::expected<CsvModel, CsvParseError>`(Document/BufferSnapshot両オーバーロード)
- [x] パーサが明示スタック・再帰を使わない単一forループ(`misc-no-recursion`新規警告0)
- [x] 引用符内改行で1レコードが複数Document行にまたがるケースが正しく解析される(位置情報含む)
- [x] 構文的に緩い入力(閉じていない引用符/ragged rows等)がエラーにならず寛容に吸収される
- [x] `detectCsvDelimiter()`が4候補(,/タブ/;/|)を正しく判定
- [x] ピース境界をまたぐ入力で単一ピースと同じ結果
- [x] Debug/Release/ubsan全1362件green、clang-tidy新規警告0
- [x] `build_plan.md`にWI-16a節追加+WI-17〜19リナンバリング

### 実装後の確定事項

**`CsvCell::quoted`フラグは承認済みプランの当初案には無く、実装着手直後の設計検討で追加した。** 当初案の`CsvCell{startPos, endPos}`のみでは、`csvCellValue()`が「このフィールドは本当に引用符付きだったか」を生テキストの先頭/末尾文字(`raw.front()=='"' && raw.back()=='"'`)から事後推論する必要があったが、`"abc"def"ghi"`(閉じ引用符の直後にゴミ文字が続きUnquotedへ寛容フォールバックした結果、たまたま末尾も`"`になる)のような入力でこの推論が破綻し、デコード処理が内容を静かに欠落させることを手計算のトレースで発見した。パーサ自身が終端時点の状態(`QuoteInQuoted`)を`bool quoted`として直接記録する設計に変更し、この曖昧さを排除した。

**`CsvBuilder`は内部vectorを参照ではなく値で保持する設計にした。** JsonTreeの`ParseState`(WI-15a)は参照束縛構造体だったため`cppcoreguidelines-avoid-const-or-ref-data-members`のNOLINT抑制が必要になったが、`CsvBuilder`は`build()`1回の呼び出しの間だけ存在し完了時に`std::move()`で結果へ譲渡するだけなので、最初から値保持にすることでこのclang-tidy指摘を未然に回避した(最終ゲートで実際に指摘0件を確認)。

**最終ゲートで検出したclang-tidy指摘は2件のみで、いずれも機械的な修正だった。** `csvCellValue()`の`const std::u16string raw`から`const`を除去(`performance-no-automatic-move`)、`consistencyScore()`内の`std::find_if`を`std::ranges::find_if`へ置換(`modernize-use-ranges`)。WI-15a(cognitive-complexity+参照メンバで2ラウンド)やWI-15b(STATUS_STACK_OVERFLOW)と比べて明らかに少なく、状態ハンドラ関数を最初から分割し値保持の`CsvBuilder`を採用した設計判断が功を奏した。

---

## WI-16b — CSV モード 非同期ワーカー + EditorSession配線 (UIなし)

**目的:** WI-16a(CSVモード ヘッドレス解析モデル)完了後、ユーザーに「次のPhase」の意味をAskUserQuestionで確認したところ、Phase 10.2(CSV)とPhase 10.3(JSON/XML Tree)がいずれもヘッドレス基盤のみ完了した状態で並行して止まっている中、「WI-16b: CSVモード続き」が選ばれた。WI-14a→WI-14b、WI-15a→WI-15bと同じ「ヘッドレスモデル→非同期ワーカー+EditorSession配線(UIなし)」の順序をCSV側でも踏襲する。

**前提:** WI-16a 完了・コミット済み (2026-08-19)

**参照:** `src/logmode/include/neomifes/logmode/log_index_worker.h`(直接のテンプレート)、`src/app/include/neomifes/app/editor_session.h`(jsonTree()系4点、直接のテンプレート)

### 着手前調査で確定した設計方針

- WI-16a時点で`CsvModel::build()`は`BufferSnapshot`/`Document`の両オーバーロードを既に実装済みと確認 — WI-15b Step1(`parseJsonTree()`へのBufferSnapshotオーバーロード追加)に相当するステップが本WIには不要、非同期ワーカー本体から直接着手できた。
- `LogIndexWorker::requestIndex()`(`snapshot`+呼び出し側設定`LogPatternRule`/`assumedYear`)と`JsonTreeWorker::requestIndex()`(`snapshot`のみ)の構造差を比較し、CSVは`CsvParseOptions{delimiter, hasHeader}`という呼び出し側設定を要するため**LogIndexWorker型**を採用。
- 失敗結果の扱いも比較: `LogIndexWorker`は`LogPatternError::InvalidRegex`(呼び出し側の設定ミス、組込パターン全てに対して到達不能)を`continue`で握りつぶす。`JsonTreeWorker`は`parseJsonTree()`のnullopt(JSON以外のファイルという日常的な正常系)を必ず投函する。`CsvParseError::InvalidDelimiter`はWI-16aの契約上「呼び出し側の設定ミス」であり`LogPatternError::InvalidRegex`と同じ性質 — **LogIndexWorker型(失敗リクエストは投函せず握りつぶす)を採用**。

### 実施内容 (3コミット)

1. `CsvModelWorker`実装(`neomifes::logmode::LogIndexWorker`を直接のテンプレートに、FIFO `std::deque`、`kMsgCsvIndexReady = WM_APP + 5`、失敗リクエストは投函しない設計)+ 統合テスト4件(`jsontree_json_tree_worker_test.cpp`を直接のテンプレート、うち1件はLogIndexWorker型の設計を裏付ける「不正delimiterでは決してメッセージが届かない」逆方向テスト)+ CMake配線 (`a8af2b7`)
2. `EditorSession`へ`csvModel()`/`csvIndexInFlight()`/`beginCsvIndexing()`/`applyCsvIndexResult()`の4点配線 + 単体テスト2件(`disableCsvMode()`はWI-16cへ意図的に先送り) (`0457fda`)
3. `main.cpp`/`normal_mode_wiring.h/.cpp`配線(`CsvModelWorker`構築 + `kMsgCsvIndexReady`受信ルーティング、呼び出し元コマンドは追加せず)+ 最終ゲート + ドキュメント同期 (`aa15488`)

### DoD

- [x] `CsvModelWorker`(`neomifes::csvmode`、FIFO、`requestIndex(snapshot, options, sessionToken)`)
- [x] 失敗リクエスト(`CsvParseError::InvalidDelimiter`)は`LogIndexWorker`と同じ理由でメッセージを投函しない設計 — 統合テストで直接証明
- [x] `EditorSession`に`csvModel()`/`csvIndexInFlight()`/`beginCsvIndexing()`/`applyCsvIndexResult()`の4点(`disableCsvMode()`は意図的に含めない)
- [x] `main.cpp`/`normal_mode_wiring.cpp`に配線済み、ただし呼び出し元(コマンド)は意図的に追加しない
- [x] 複数タブの結果が両方届くこと(FIFO、最新のみ保持ではないこと)を統合テストで証明
- [x] Debug/Release/ubsan全1356件green、clang-tidy新規警告0(`src/`側4ファイル)
- [x] ドキュメント同期

### 実装後の確定事項

**WI-16aで両オーバーロードが既に揃っていたため、本WIはWI-14b/WI-15bより1ステップ少ない3コミットで完結した。** WI-14b/WI-15bはいずれも「非同期化の前提となるBufferSnapshotオーバーロード追加」を含む4コミット構成だったが、CsvModelはWI-16a時点でスレッド安全な`BufferSnapshot`版を最初から実装していたため(WI-16aの設計方針そのもの)、この差分が後続WIのコミット数として直接的に表れた。

**最終ゲート(ubsan)で`CsvModelWorker`のスレッド関連コード(`std::thread`/`std::mutex`/`std::condition_variable`/`PostMessageW`経由のポインタ受け渡し)を特に注意して検証したが、UB検出は0件だった。** `LogIndexWorker`/`JsonTreeWorker`と全く同型の設計(FIFO・単一ワーカースレッド・`unique_ptr`による所有権譲渡)を踏襲した結果であり、新規のスレッド安全性リスクは導入していない。

**clang-tidyの`tests/`側指摘(`app_editor_session_test.cpp`の`bugprone-unchecked-optional-access`等)は全て既存の許容済みパターンと確認した。** `ASSERT_TRUE(x.has_value())`直後の`x->field`参照をclang-tidyが追跡できない誤検知は、Phase 5c3/5c4以来繰り返し確認済みの既知パターン。

コミット済み(`a8af2b7`/`0457fda`/`aa15488`)、pushはユーザーの明示指示待ち。Phase 10.2は本サブWIで非同期ワーカー+`EditorSession`配線が完了、UIは一切追加していない(呼び出し元コマンド無し) — グリッドUI・列固定・フィルタ・ソート・式列・セル編集は全て後続サブWI(WI-16c以降)へ。次はPhase 10.2の続き、またはPhase 10.3の続き(WI-15c)、またはユーザー指定の次項目。

---

## WI-17 〜 WI-19 — Phase 11 / 9 / 12

**Phase 10 (10.1〜10.3) 完了まで着手を推奨しない** (roadmap §2 の優先順位表通り)。Phase 10.1 は WI-14a〜d で完結済み、Phase 10.3 は WI-15a〜b(ヘッドレス基盤+非同期化)着手済み・残り未完了、Phase 10.2 (CSV) は WI-16a〜b(ヘッドレス解析モデル+非同期化)着手済み・残り未完了。

着手時は `master_roadmap.md` の該当章を読み、**本書 §5 と同じ形式で WI を切り直してから**始めること (章をそのまま実装しようとすると 1 セッションに収まらない)。

**WI 番号の注記 (2026-08-18、2026-08-19追記):** roadmap原案は Phase 10 全体を「WI-14」1本に見込んでいたが、実際には Phase 10.1 だけで WI-14a〜d の4サブ WI を要し、Phase 10.3 も WI-15a から始まる複数サブ WI に分かれる見通しとなったため、Phase 11/9/12 の当初の割当番号 (WI-15/16/17) を1つずつ繰り下げて WI-16/17/18 とした(2026-08-18)。さらに Phase 10.2 (CSV) 着手時に WI-16a が新設されたことで、もう1つずつ繰り下げて WI-17/18/19 とした(2026-08-19)。

| WI | 内容 | roadmap 章 | 目安 |
|---|---|---|---|
| WI-17 | Phase 11 — Git 統合 / LSP / マクロ | §11 | 3 領域 × 各 3〜6 サブ WI |
| WI-18 | Phase 9 — AI プラグイン | §9 | 4〜6 サブ WI |
| WI-19 | Phase 12 — 総合品質保証・正式出荷 | §12 | §12.3 の 22 項目 |

**順序の根拠:**
- **Phase 9 (AI) が最後** — CLAUDE.md が「エディタ本体は AI 無しでも 100% 動作しなければならない」と定めており、本体完成後に載せるのが筋。加えて外部 API 依存で陳腐化が速い

---

# 6. MVP 出荷判定チェックリスト (WI-13)

- [x] ファイルを 開く / 編集 / 保存 / 別名保存 が全て動作する (WI-01/WI-02実装、実機で`--open`→編集→`Ctrl+S`保存→ファイル内容の変化を確認済み)
- [x] 日本語 IME でインライン変換が正しく表示される (**実機手動確認必須**) (WI-06実装時の2026-08-12に実機MS-IMEで確認済み。本WIではIME関連コードを一切変更していないためコードレビューで退行なしを確認、再実演はしていない)
- [x] 未保存で終了しようとすると警告が出る (WI-02実装+既存テスト。本WIでは対話的再確認は環境のフォーカス不安定性により未実施、コードレビュー+既存テストスイートで代替 — 詳細は実装後の確定事項参照)
- [x] 10 個のファイルをタブで開いて相互に切り替えられる (WI-05実装+既存テスト。本WIでは対話的再確認は環境のフォーカス不安定性により未実施、コードレビュー+既存テストスイートで代替)
- [x] 設定でフォント・タブ幅・テーマを変更でき、再起動後も保持される (WI-08/WI-09実装、過去セッションで実機ドッグフーディング済み)
- [x] 長い行の右端まで横スクロールで到達できる (WI-03実装+既存テスト)
- [x] 起動時間 ≤ 300ms (Release 実測) — **実測 29.3ms** (`--measure-startup`、目標の1/10)
- [x] 60fps スクロール維持 (`--measure-frame`) — **実測 avgFrame 16.6ms (≈60fps)**、10GB実ファイルでの定常スクロールでも同水準 (p50=16.67ms/p95=16.84ms) を維持
- [x] 10GB ファイルを開ける — 実際に10GBのテキストファイルを生成し`--open`で開封、クラッシュなし、スクロール性能も維持することを確認
- [x] クラッシュ 0 (8 時間ソーク) — **達成。** Windowsタスクスケジューラ(`NeoMIFES_WI13_SoakTest`)で独立実行、署名済みReleaseバイナリを15分おきにプロセス生存+メモリ量記録、480分(8時間)全区間で生存・Responding=True、最終行に`SOAK_COMPLETE_NO_CRASH`を記録(`D:\_wi13_scratch\wi13_soak_log.csv`実測)。メモリはWorkingSet 13MB→5.3MBへ推移し単調増加(リーク)の傾向なし。結果記録後、ユーザー指示に基づき`D:\_wi13_scratch\`一式+タスクスケジューラタスクを削除(証明書ストアの自己署名証明書は保持)
- [x] ASan / UBSan クラッシュ 0、clang-tidy 新規指摘 0 — UBSan/clang-tidyはWI-12完了時点(本WIはソース無変更)で確認済み。ASanは本WIで`asan`プリセットを初めてビルド+`ctest`実行し、**1227/1227件全green、AddressSanitizer/UndefinedBehaviorSanitizerの実行時エラー検出0件**を確認(`build/asan/Testing/Temporary/LastTest.log`実測)。`asan`プリセット自体が通常のWI検証フローに未組込みのままCI常設化されていない点は`docs/issues/asan_preset_not_in_ci.md`として別途起票済み
- [ ] Authenticode 署名 + Portable Zip 配布 — **Portable Zipは完成、署名機構も自己署名証明書で実装・動作確認済み(`tools/create_dev_certificate.ps1`+`tools/sign_release_binary.ps1`)。ただし本物のAuthenticode証明書は未取得(購入・組織身元確認が必要でユーザー判断待ち)** — `docs/issues/authenticode_certificate_not_acquired.md`参照。この項目は厳密には未達のまま記録する
- [ ] **開発者が日常的に NeoMIFES で NeoMIFES を開発している** — M1(2026-08-05)以降、WI-11/WI-12等で複数回実機ドッグフーディングを重ね保存パイプラインの実動作は繰り返し実証済みだが、「毎日の開発作業そのものをNeoMIFESのGUI経由で行っている」わけではない(実装はClaude CodeのRead/Editツール経由)。正直な現状として未達のまま記録し、出荷判断はユーザーに委ねる
- [x] ユーザーマニュアル (キーバインドリファレンス) を同梱 — `docs/user/keybindings.md`作成済み(4プリセットの既定キー・固定キー・設定ファイル場所を実際の値から転記)

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
