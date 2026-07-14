# NeoMIFES 基本設計書 v1.0

> 対象要件: [`NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md) v1.0
> 上位ガイド: [`CLAUDE.md`](../../CLAUDE.md)

本書はプロジェクト全体のアーキテクチャ・レイヤ責務・非機能要件の実現方針を規定する「What」レベルのドキュメントである。実装レベル (「How」) は [`detailed_design.md`](detailed_design.md) を参照。

---

## 1. 目的とスコープ

### 1.1 目的
- Windows 環境で最速・最軽量・AI 親和のネイティブテキストエディタを実現する。
- 秀丸/サクラ/MIFES の思想を継承しつつ、モダン C++23 と Direct2D 系 API で再設計する。

### 1.2 スコープ (v1.0 で扱う範囲)
- 単一プロセスの Windows デスクトップアプリ (x64)
- 標準テキスト編集機能、ログ解析モード、CSV/JSON/XML モード、AI プラグイン、Git/LSP 統合
- Windows 10 21H2 以降 / Windows 11 対応

### 1.3 スコープ外 (v1.0)
- macOS / Linux 対応
- ARM64 対応 (将来検討)
- ネットワークファイル共有プロトコル固有最適化 (通常 SMB 等の範囲で動作)
- クラウドリアルタイム共同編集

---

## 2. 全体アーキテクチャ

### 2.1 レイヤ構成 (トップダウン)

```
┌─────────────────────────────────────────────────────────┐
│  L7: UI Shell            Win32 ウィンドウ, タブ, ダイアログ  │
├─────────────────────────────────────────────────────────┤
│  L6: Application         Command Dispatcher, Undo/Redo, Session │
├─────────────────────────────────────────────────────────┤
│  L5: Editor Core         Selection, Cursor, Viewport, Mode      │
├─────────────────────────────────────────────────────────┤
│  L4: Rendering Engine    Direct2D / DirectWrite / Layout Cache  │
├─────────────────────────────────────────────────────────┤
│  L3: Domain Engines      Document | Search | Encoding | Syntax  │
├─────────────────────────────────────────────────────────┤
│  L2: Plugin Host         DLL loader, API 境界, IPC (in-proc)     │
├─────────────────────────────────────────────────────────┤
│  L1: Platform            Win32 RAII wrapper, FileIO, Threading  │
└─────────────────────────────────────────────────────────┘
```

### 2.2 依存方向
- **上位 → 下位** のみ。下位は上位のインターフェース (抽象) を知らない。
- 横方向 (同一レイヤ内モジュール間) は **メッセージバス** または **Command** で疎結合化。
- AI 機能は **L2 Plugin Host** の下に外部プロセスまたは DLL として分離。**L3 以下のコアは AI を一切知らない**。

### 2.3 プロセス構成
- **本体プロセス:** UI + Core + Rendering + Domain (シングルプロセス、マルチスレッド)
- **複数ウィンドウ:** 単一プロセス内で `MainWindow` を複数インスタンス化して実現 (VS Code / Sublime Text と同方式)。プロセス分離は起動 0.3s 要件を満たせないため採用しない。ウィンドウは独立した Session を持ち、`SessionManager` が集約管理する
- **2 つ目以降の起動:** シングルインスタンス化 (Named Mutex) し、コマンドライン引数を IPC で先行プロセスへ委譲、そちらが新規 `MainWindow` を開く
- **AI プラグイン:** 別 DLL、ホットロード可能。ネットワーク I/O は AI プラグインのワーカースレッドで隔離
- **LSP サーバ:** 各言語ごとに子プロセス (stdio 通信)

### 2.4 スレッドモデル
| スレッド | 役割 |
|---|---|
| UI Thread | Win32 メッセージポンプ、Direct2D 描画コマンド発行 |
| Document Worker | ファイル読込/保存、大規模編集の非同期処理 |
| Search Worker Pool | 検索/Grep の並列実行 (論理コア数-1) |
| Syntax Worker | シンタックス解析・折り畳み計算 |
| Plugin Worker | 各プラグインが所有 (AI プラグインは HTTP 待機用) |
| IO Watcher | ファイル変更監視 (`ReadDirectoryChangesW`) |

- **UI ↔ Worker 間通信** は Lock-free MPSC キュー + `PostMessageW` (WM_APP+n)。
- コア共有データ (ドキュメント) は **RCU 風スナップショット** で読み取り並列化 (詳細設計参照)。

---

## 3. モジュール責務

### 3.1 UI Shell (L7)
- Main Window / Tab Bar / **Line Gutter (行番号・折り畳みマーカ・ブックマークマーカ)** / Status Bar / Command Palette / Dialogs
- 高 DPI (Per-Monitor V2) 対応
- ダーク/ライトテーマ、Windows 11 Mica/Acrylic は任意
- **キーボード主体** の操作性。マウス依存機能は必ずキーバインド代替を持つ
- **Line Gutter** は Rendering Engine のサブレンダラとして独立、幅は表示中最大行数の桁数から動的算出

### 3.2 Application (L6)
- **Command Dispatcher:** 全操作を Command オブジェクト化 (undo/redo/マクロ/AI 呼出の統一境界)
- **Undo Stack:** 100万件対応 (パッキング + 圧縮スナップショット)
- **Session Manager:** ワークスペース状態の永続化 (JSON)
- **Config Manager:** ユーザー設定 (JSON/TOML) の読み書きと動的反映

### 3.3 Editor Core (L5)
- **Cursor / Selection:** 複数カーソル、矩形選択、縦編集
- **Viewport:** 論理行/表示行変換、折り畳み、水平/垂直スクロール
- **Mode:** 通常 / ログ / CSV / JSON-Tree / Grep 結果 表示モード
- **Bookmark / Outline:** ドキュメント横断のメタ情報管理

### 3.4 Rendering Engine (L4)
- Direct2D デバイス管理 (DXGI スワップチェイン)
- DirectWrite テキストフォーマット/レイアウトキャッシュ
- **可視領域のみレイアウト計算** (仮想スクロール)
- グリフキャッシュ・行キャッシュで再描画コスト最小化
- 60fps を厳守 (v-sync 準拠、フレーム予算 16.6ms)
- **和文欧文混植 (日本語最適化):**
  - `IDWriteTextFormat` にフォントフォールバックビルダで日本語フォントを追加
  - フォント優先順 (デフォルト): 欧文 = `Cascadia Mono` → `Consolas`、和文 = `BIZ UDGothic` → `Yu Gothic UI` → `Meiryo`
  - CJK 文字は等幅グリッド (欧文の 2 セル幅) に整列
  - IME 変換中文字列は `WM_IME_COMPOSITION` 経由で下線付きインライン表示
  - 全角/半角混在時のカーソル位置は UTF-16 CU オフセットで一貫管理

### 3.5 Document Engine (L3)
- **Piece Table** をコアデータ構造とする (詳細設計 §3 参照)
- 巨大ファイル対応:
  - 10GB は**メモリマップドファイル + 遅延ロード**で扱う
  - 編集は Add Buffer 側に追加、原本は変更しない
- 改行インデックス、UTF-16 コードユニットオフセット → 論理行変換の高速化
- スナップショット共有で読み取り並列化

### 3.6 Search Engine (L3)
- **Boyer-Moore / SIMD (SSE4.2 pcmpistri, AVX2)** による通常検索
- 正規表現は **std::regex 非採用**。**Google RE2** または **Hyperscan** を候補 (ADR 化)
- Grep は Search Worker Pool で並列化。結果は Grep 表示モードにストリーム表示
- 巨大ファイル検索は Piece Table のチャンク単位で並列走査

### 3.7 Encoding Engine (L3)
- 対応: UTF-8/8-BOM/16LE/16BE/32/Shift-JIS/EUC-JP/ISO-2022-JP
- **第一候補:** 自前実装 (依存最小化)
- **第二候補:** ICU の一部リンク (バイナリ膨張とトレードオフ検討)
- 自動判定は 3 段階: (1) BOM / (2) 文字分布統計 / (3) N-gram モデル

### 3.8 Syntax Engine (L3)
- **TextMate 互換文法定義** を採用検討 (VSCode 系エコシステム流用)
- ハイライトは非同期増分解析
- 折り畳み範囲・アウトラインも本エンジンが提供

### 3.9 Plugin Host (L2)
- **C ABI** で公開 (C++ ABI 非互換問題回避)
- DLL ホットロード対応 (アンロード時は参照カウント + セーフポイント)
- SDK ヘッダ `include/neomifes/` を配布
- 権限モデル: プラグインは Document 変更に **Command 経由** でのみアクセス

### 3.10 AI Plugin (L2 下位)
- 外部 API (Claude / GPT / Gemini / OpenAI 互換) を叩く
- **本体コアは AI を一切参照しない**。UI からのトリガは Command Dispatcher → Plugin Host → AI Plugin
- APIキーは Windows Credential Manager (DPAPI 経由) に格納

### 3.11 Platform (L1)
- Win32 ハンドル RAII (`HandleGuard`, `HdcGuard`, `HwndGuard`)
- ファイル I/O (`CreateFileW` + Overlapped)
- スレッドプール (Windows Thread Pool API)
- レジストリ/クレデンシャル/COM 初期化

---

## 4. 非機能要件の実現方針

### 4.1 起動時間 ≤ 0.3s
- 静的リンク中心。動的 DLL は最小限
- 起動時初期化を **必要最小限のみ同期**、残りは Worker で遅延ロード
- Direct2D デバイス生成もメインウィンドウ表示後に非同期化
- COM / DirectWrite ファクトリはシングルトンで再利用
- Startup Profiling を CI に組み込み、退化検知

### 4.2 メモリ ≤ 20MB (初期起動)
- STL コンテナは Small Buffer Optimization を活用
- 文字列内部表現は UTF-16 で、余計な変換を発生させない
- キャッシュはサイズ上限付き LRU
- プラグイン未使用時は関連メモリ確保しない

### 4.3 巨大ファイル (10GB)
- **Memory-mapped File** + **Piece Table** の組合せ
- 全行を最初にスキャンしない。**表示範囲だけ改行インデックスを構築**
- 改行インデックスは B+Tree または Skip-list で O(log n) アクセス

### 4.4 60fps スクロール
- 表示行キャッシュ + グリフキャッシュ
- ダーティ矩形更新 (画面全体を再描画しない)
- スクロール中はシンタックス再計算をデバウンス
- Direct2D の Present は `DXGI_PRESENT_DO_NOT_SEQUENCE` 検討

### 4.5 100万 Undo
- Command は差分エンコード + 連続入力のパッキング
- 一定件数ごとに圧縮 (zstd 検討)
- ディスクスワップ (`%LOCALAPPDATA%\NeoMIFES\undo\<session>\`) は最終手段

### 4.6 クラッシュゼロ
- 例外はレイヤ境界で catch し、上位に構造化エラーとして返す
- SEH は `__try/__except` で最外郭のみ捕捉、クラッシュダンプ生成
- 自動保存 (5秒間隔、差分のみ)、次回起動時に復元プロンプト

### 4.7 メモリリークゼロ
- RAII 徹底 + `unique_ptr` 中心
- Debug ビルドで CRT Leak Detection + ASan 常時実行
- CI に leak 検出ステップ

---

## 5. データフロー

### 5.1 文字入力フロー
```
KeyDown (Win32 WM_CHAR)
  → InputMapper (キーバインド解決)
  → Command (InsertTextCommand)
  → CommandDispatcher.execute()
     ├─ Document.apply(patch)  ── PieceTable 更新
     ├─ UndoStack.push(inverse)
     └─ EventBus.publish(DocumentChanged)
        ├─ Rendering.invalidate(range)
        ├─ Syntax.scheduleReparse(range)
        └─ Plugin.onDocumentChanged(range)
```

### 5.2 ファイルオープンフロー
```
User → OpenFileDialog → path
  → Encoding.detect(head 64KB)  [Worker]
  → Document.load(path, encoding) [Worker, memory-mapped]
  → LineIndex.buildLazy()
  → UI.attach(document)
  → Rendering.firstPaint()
```

### 5.3 AI 呼出フロー
```
User → InvokeAI Command
  → PluginHost.dispatch("ai.claude.review", context)
  → AI Plugin Worker → HTTP → Claude API
  → 結果を Command 化して Document へ提案 (プレビュー UI)
  → ユーザー承認で apply
```

---

## 6. 設定・拡張

### 6.1 設定ファイル
- 形式: **JSON5** (コメント可) を第一候補 / TOML を第二候補
- 場所: `%APPDATA%\NeoMIFES\config.json5`
- 変更検知でホットリロード

### 6.2 プラグイン API
- 詳細は [`detailed_design.md`](detailed_design.md) §8 参照
- 破壊的変更時はメジャーバージョン更新 + 移行ガイド必須

### 6.3 マクロ
- Lua 5.4 (LuaJIT 検討) を第一候補
- JavaScript は QuickJS 検討
- Python はプラグイン経由 (組込は膨張のため見送り)

---

## 6.5 ロギング

- **フォーマット:** 構造化 (JSON Lines)、UTF-8
- **保存先:** `%LOCALAPPDATA%\NeoMIFES\logs\neomifes-YYYYMMDD.jsonl`
- **ローテーション:** 日次 or 10MB 超で新ファイル、既定 14 日保持
- **レベル:** `trace / debug / info / warn / error / fatal`。デフォルト `info`、設定でオーバーライド可
- **実装:** 自前の軽量ロガー (依存追加なし、Lock-free MPSC + IO Worker)
- **クラッシュ時:** SEH ハンドラが直前 N 行をクラッシュダンプに同梱

## 6.6 コード署名

- **本体 exe (`NeoMIFES.exe`):** リリースビルドに **Authenticode 署名** を必須化 (CI の署名ステップ、証明書は Secrets 管理)
- **標準プラグイン DLL:** 本体と同一証明書で署名
- **サードパーティプラグイン:** 署名検証をオプション化 (Enterprise 設定で必須化可能)
- **タイムスタンプサーバ:** DigiCert / Sectigo などの RFC 3161 対応サーバ

---

## 7. 品質保証方針

- 単体テスト: **GoogleTest**、差分カバレッジ 80% 以上
- ベンチマーク: **google/benchmark**、起動・編集・検索の各シナリオ
- 静的解析: MSVC `/analyze` + clang-tidy
- 動的解析: ASan / UBSan / CRT Leak Detection
- CI: GitHub Actions (Windows Server 2022)
- リリース前: 24時間クラッシュソーク・巨大ファイル(10GB)手動検証

---

## 8. リスクと対策

| # | リスク | 影響 | 対策 |
|---|---|---|---|
| R1 | 10GB ファイル対応と 20MB メモリ制約の両立 | 起動要件未達 | Memory-mapped + 遅延ロードで達成見込み。要 PoC |
| R2 | 正規表現ライブラリ選定 (`std::regex` は性能不足) | 検索性能未達 | RE2 or Hyperscan を PoC で比較、ADR 化 |
| R3 | シンタックスハイライトのエコシステム自作コスト | 開発遅延 | TextMate grammar 流用、tree-sitter は将来検討 |
| R4 | LSP 統合の複雑性 | Phase 11 遅延 | 最初は C++/TS/Python の 3 言語に絞る |
| R5 | プラグイン DLL のホットアンロードでのリーク | 品質未達 | 参照カウント + セーフポイント。ソークテスト必須 |
| R6 | Win32 のみで Windows 11 の Mica/Acrylic 対応 | UI 見劣り | DwmSetWindowAttribute で対応可能。要検証 |
| R7 | 巨大 Undo のメモリ膨張 | 品質未達 | 圧縮 + ディスクスワップ。上限は設定可能に |

---

## 9. 未決事項 (Issue 化対象)

1. ビルドシステム最終選定 (CMake / MSBuild / 独自)
2. パッケージング形式 (MSIX / インストーラ / Portable Zip)
3. 正規表現エンジン最終選定 (RE2 vs Hyperscan)
4. シンタックス定義形式 (TextMate vs tree-sitter)
5. マクロ言語の同梱範囲 (Lua のみ / +JS)
6. LSP 初期対応言語の確定
7. 設定ファイル形式最終決定 (JSON5 vs TOML)
8. 自動更新機構の有無

これらは `docs/decisions/` に ADR として記録すること。

---

## 10. Definition of Done (基本設計)

- [x] 全レイヤの責務が明確化されている
- [x] 非機能要件 (性能/メモリ) に対する具体的アプローチが記述されている
- [x] AI が完全プラグイン境界にあることが明記されている
- [x] 主要リスクが列挙され対策が記述されている
- [x] 未決事項が Issue 起票の対象として明示されている
