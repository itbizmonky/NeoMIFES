# NeoMIFES マスターロードマップ v1.0

> 対象要件: [`NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md) v1.0
> 上位ガイド: [`CLAUDE.md`](../../CLAUDE.md) / 基本設計: [`basic_design.md`](basic_design.md) / 実装詳細: [`detailed_design.md`](detailed_design.md)
> 発行: 2026-07-19 (Phase 5b1 完了後)

本書は Phase 4b8・5b2・5b3・6〜12 の **実装着手時に迷わない詳細設計** を一気通貫で規定する。個別フェーズ着手時に本書の該当章をベースに詳細プランを Plan Mode で起こし、実装後は `detailed_design.md` の対応節へ確定内容を吸収する。

---

## 0. 位置づけ・関連文書

### 0.1 なぜ本書が必要か

Phase 5b 着手時点で「フェーズごとに実装内容が未確定」というブレが顕在化した。要件定義書 §20 の最終目標 (「秀丸の軽さ / MIFES の操作性 / サクラの拡張性」の融合 + AI + ログ解析 + 巨大ファイル + Git + LSP + 検索) を一気通貫で写像し、各フェーズの成果物・凌駕ポイント・妥協点を先に確定させることで、実装セッションごとの判断ゆらぎと 「これで完成に近づいているか」の再確認コストを排除する。

### 0.2 本書と他文書の役割分担

| 文書 | 責務 | 更新タイミング |
|---|---|---|
| 要件定義書 | 何を作るか (What) | v1.0 凍結 |
| 基本設計書 | どういうレイヤ構成か (Structure) | 破壊的変更時のみ |
| **本書 (マスターロードマップ)** | **各フェーズで何をどう作るか (Plan-of-record)** | **各フェーズ完了時に「実装差分」を確定内容として吸収** |
| 詳細設計書 | 実装済み機能のリファレンス (Reference) | フェーズ完了時に本書から吸収 |
| ADR | 個別技術判断の記録 | 判断発生時 |
| RESUME_HERE / TIMELINE | セッション間の受け渡し・時系列 | 各セッション |

### 0.3 更新運用

- **各フェーズ着手前:** 本書の該当章を読み、Plan Mode でセッション個別の詳細プランを起こす
- **各フェーズ完了時:** 実装で確定した詳細を `detailed_design.md` の対応節に吸収し、本書の該当章末尾に「実装後の確定事項/変更点」を追記する
- **凍結セクション:** 本書は「実装前の計画」を残し続けるための文書。実装で確定した内容は詳細設計書側の一次情報となり、本書は歴史的計画として残る

---

## 1. 完成イメージ

### 1.1 一言で

「秀丸の軽さ・MIFES の操作性・サクラの拡張性を、モダン C++23 と Direct2D で書き直した、AI 時代の Windows 標準テキストエディタ」。

### 1.2 差別化される 5 つの体験

1. **起動 0.3s、初期メモリ 20MB 以下** — Electron 系エディタと同居していても常駐できる
2. **60fps を維持したまま 10GB ファイルを開ける** — 秀丸/MIFES 系の伝統を Direct2D で再定義
3. **ログ解析モード** — 数十 GB のログを ERROR/WARNING 抽出しながら時系列ジャンプで探索できる、本ソフト最大の差別化点
4. **AI 統合が完全プラグイン境界** — オフラインでも 100% 動作、AI キーが漏れない
5. **キーボード完結** — 全機能にキーバインド、マウス依存操作なし。VSCode 相当のコマンドパレットに加えて、秀丸/サクラ/MIFES ユーザーが即使えるキー体系プリセットを備える

### 1.3 三大エディタからの継承マトリクス

| カテゴリ | 秀丸 | サクラ | MIFES | NeoMIFES での実現 |
|---|---|---|---|---|
| 起動速度 | ◎ | ○ | ◎ | Phase 1 (実測 148ms) |
| 巨大ファイル | △ | △ | ◎ | Phase 2 + 6 (mmap + lazy line index) |
| マクロ | ◎ (独自) | ○ | ○ | Phase 11 (Lua/QuickJS) |
| Grep | ◎ | ○ | ○ | Phase 5c (数 GB/s 目標) |
| 矩形選択 | ○ | ○ | ◎ | Phase 4b8 |
| 縦編集 | ○ | ○ | ◎ | Phase 4b8 |
| 複数カーソル | ✕ | △ | ✕ | Phase 4b6 (既に他エディタを凌駕) |
| DIFF/Merge | ○ | ✕ | ○ | Phase 11 (Git 統合) |
| アウトライン | ◎ | ○ | ○ | Phase 7 |
| 折り畳み | ○ | ○ | ○ | Phase 7 |
| CSV/TSV | △ | ○ | ✕ | Phase 10 |
| JSON/XML tree | ✕ | ✕ | ✕ | Phase 10 (差別化点) |
| ログ解析 | △ | △ | △ | **Phase 10 (最大差別化点)** |
| プラグイン | ○ | ○ (js/net) | ✕ | Phase 8 (C ABI + hot-load) |
| LSP | ✕ | △ | ✕ | Phase 11 (差別化点) |
| AI 統合 | ✕ | ✕ | ✕ | **Phase 9 (差別化点)** |
| キーバインド完結 | ○ | ◎ | ◎ | Phase 4b6 + 5b3 + 全体 |

「三大エディタが持たない要素 (LSP / AI / JSON tree / モダン複数カーソル)」で差別化し、「三大エディタが持つ要素」は同等以上の速度・使いやすさで実装するのが本書の骨子。

---

## 2. 全フェーズ俯瞰

| Phase | 内容 | 状態 | 本書該当章 |
|---|---|---|---|
| 0 | 要件・設計 | ✅ 完了 | — |
| 0.5 | ビルド基盤 | ✅ 完了 | — |
| 1 | Win32 骨組み | ✅ 完了 | — |
| 2a/2b | Document Engine | ✅ 完了 | — |
| 3a/3b/3c | Rendering | ✅ 完了 | — |
| 4a〜4b7 | Editor Core | ✅ 完了 | — |
| 4b8 | 矩形選択・タブ変換・N対N分配 | ⏸️ 保留 | §3 |
| 5a | Search Engine 基盤 | ✅ 完了 | — |
| 5b1 | 複数行マッチ対応 | ✅ 完了 | — |
| 5b2 | 置換 (ReplaceAllCommand) | ⏭️ 次候補 | §4 |
| 5b3 | Find bar UI | 未着手 | §5 |
| 5c | Grep / 複数フォルダ検索 | 未着手 | §5.5 |
| 6 | エンコーディング + 自動判定 | 未着手 | §6 |
| 7 | シンタックス / アウトライン / 折り畳み | 未着手 | §7 |
| 8 | プラグインエンジン + SDK | 未着手 | §8 |
| 9 | AI プラグイン (Claude 統合) | 未着手 | §9 |
| 10 | ログ解析 / CSV / JSON-XML tree | 未着手 | §10 |
| 11 | Git / LSP / マクロ | 未着手 | §11 |
| 12 | 総合品質保証 | 未着手 | §12 |

---

## 3. Phase 4b8 — 矩形選択・タブ⇔スペース変換・複数カーソルクリップボードN対N分配

### 3.1 機能ビジョン

- **凌駕元:** 主に MIFES (矩形編集の元祖) と VSCode/Sublime Text (複数カーソルの現代版)
- **凌駕ポイント:** MIFES 系の矩形選択は「矩形範囲を保持し、範囲内文字を一括編集する」が主眼だが、本ソフトは既存の複数カーソル基盤 (`SelectionModel` / `MultiCursorEditCommand`) の上に「矩形 = 各行 1 カーソルの集合」というモデルで実装し、矩形と複数カーソルをシームレスに切替可能にする
- **タブ⇔スペース変換** は秀丸/サクラで慣れた機能を、複数カーソル・矩形選択と組み合わせても正しく動く形で実装する
- **N対N分配** は VSCode 由来: N 個のカーソルで Ctrl+C するとクリップボードに改行区切りで N 行入り、別の N 個のカーソルで Ctrl+V すると N 個に分配貼り付けされる

### 3.2 UI/UX

**キーバインド (デフォルト):**
- `Alt + LMouse ドラッグ` / `Alt + Shift + カーソル移動` — 矩形選択開始・拡大
- `Shift + Alt + I` — 選択範囲を「各行末尾に 1 カーソル」に変換 (VSCode 互換)
- `Ctrl + Shift + P` → コマンドパレット → `Convert Tabs to Spaces` / `Convert Spaces to Tabs` / `Convert Indentation to Tabs` / `Convert Indentation to Spaces`
- `Ctrl + C / Ctrl + V` — 矩形/複数カーソルの N対N分配

**視覚要素:**
- 矩形選択中は既存の `SelectionVisual` の描画パスを流用 (行ごとに矩形描画するのは既存の複数カーソル選択と同じ)
- 矩形の右端が行末より右にはみ出る場合、はみ出し部分は薄く塗る (「その行にはテキストが無いが、貼り付け時にはこの列位置に挿入される」の視覚表現)

**ASCII モックアップ (矩形選択の視覚):**
```
   1  int foo = [BAR         ]12345;    // BAR = 矩形選択、[ ] は選択範囲
   2  int qu  = [BAZ]xxxxxxxxxx;        // 短い行は右側の薄い矩形が「仮想空白」
   3  int abc = [FOOBAR    ]789;        //
```

### 3.3 データ構造・アルゴリズム

**矩形選択の内部表現 = 既存 `SelectionModel::m_cursors` の集合:**
```cpp
// core/selection_model.h に追加:
enum class SelectionMode { Normal, Rectangular };

// SelectionModel に追加:
[[nodiscard]] SelectionMode mode() const noexcept { return m_mode; }
void setRectangularSelection(TextPos anchor, TextPos active) noexcept;
// → 内部で anchor.line〜active.line の各行に 1 カーソル生成、
//   各カーソルの anchor.column = min(anchor.col, active.col)、
//   active.column = max(anchor.col, active.col) を設定

private:
    SelectionMode m_mode = SelectionMode::Normal;
```

**「仮想空白」の扱い:**
- 各行の実文字数 < 矩形の右端列 の場合、`Selection::activeColumn` は「実文字数」に丸めるのではなく **仮想列位置** をそのまま保持する
- 挿入時は `Document::apply` の前に「各行を選択右端列まで空白でパディングする patch」を挿入する Command を発行 (ネスト Command)

**タブ⇔スペース変換:**
- 新規 Command: `ConvertIndentationCommand { enum class Target { TabsToSpaces, SpacesToTabs, Auto }; int tabWidth; TextRange scope; };`
- 実装は各行の先頭連続空白を計算 → 変換後文字列 → `MultiCursorEditCommand` に相当する複数の `TextEdit` を発行 (既存 Undo 基盤を再利用)
- `Auto` はドキュメント統計で多数派を採用 (行数比 8:2 で偏っている場合のみ変換、それ以外は「混在のため手動選択」ダイアログ)

**N対N分配クリップボード:**
- `ClipboardService` (新規 `src/core/clipboard_service.{h,cpp}`) に「マルチカーソルクリップボード」機能を追加
- コピー時: `N` カーソル → `N` 行を `\r\n` で結合してクリップボードへ書き込む + カスタムクリップボードフォーマット `CF_NEOMIFES_MULTICURSOR` にも書き込む (この値は自身が MC 由来であることのマーカ)
- 貼り付け時: クリップボードが `CF_NEOMIFES_MULTICURSOR` を持ち、行数 = カーソル数 なら分配、それ以外はカーソル数だけ同一テキストを繰り返す (VSCode 互換)

### 3.4 性能目標
- 矩形選択作成 (1000 行): ≤ 5ms
- タブ⇔スペース変換 (100000 行): ≤ 100ms
- 10 万カーソル貼り付け: ≤ 200ms

### 3.5 テスト戦略
- 単体: 矩形範囲 anchor/active の swap、仮想空白の挿入、N対N の行数不一致時の VSCode 互換動作
- 統合: 矩形選択 → Ctrl+C → 別ドキュメントで Ctrl+V → 一致
- Undo/Redo: 矩形挿入・矩形削除の逆操作、タブ⇔スペース変換の完全逆操作
- 回帰: 既存の複数カーソル (Phase 4b6) が挙動を変えないこと

### 3.6 影響ファイル (想定)
- **新規:** `src/core/clipboard_service.{h,cpp}`、`src/core/convert_indentation_command.{h,cpp}`
- **変更:** `src/core/selection_model.{h,cpp}` (Rectangular mode)、`src/core/multi_cursor_edit_command.cpp` (仮想空白パディングとの相互作用)、`src/ui/main_window.cpp` (Alt+マウス/Alt+Shift+矢印のフック)、`src/render/render_pipeline.cpp` (仮想空白の薄い塗り)
- **新規テスト:** `tests/unit/core_rectangular_selection_test.cpp`、`tests/unit/core_convert_indentation_test.cpp`、`tests/integration/clipboard_multi_cursor_test.cpp`

---

## 4. Phase 5b2 — 置換 (ReplaceAllCommand)

### 4.1 機能ビジョン
- **凌駕元:** サクラエディタ・秀丸の「全置換」
- **凌駕ポイント:** 100 万件置換を 1 個の Undo/Redo エントリで戻せる (差分エンコード + 圧縮スナップショット、後述)。RE2 のパフォーマンスと組み合わせて数 GB ファイルにも耐える

### 4.2 UI/UX
- Phase 5b2 時点では **UI 無し** (ヘッドレスコア実装)
- Phase 5b3 の Find bar 完成後、Find bar 内の `Replace` ボタン/`Ctrl+H` で発火

### 4.3 データ構造・アルゴリズム

**新規 `core::ReplaceAllCommand`:**
```cpp
// src/core/replace_all_command.h
namespace neomifes::core {

class ReplaceAllCommand : public ICommand {
public:
    ReplaceAllCommand(std::vector<search::Match> matches,
                      std::u16string replacement,
                      SelectionModel::Snapshot cursorsBefore) noexcept;

    document::EditResult execute(document::Document& doc) override;
    document::EditResult undo(document::Document& doc) override;
    SelectionModel::Snapshot cursorsAfterExecute() const noexcept override;
    SelectionModel::Snapshot cursorsAfterUndo() const noexcept override;

private:
    struct AppliedEdit {
        document::TextRange rangeBefore;
        std::u16string      originalText;
        std::u16string      replacementText;
    };
    std::vector<AppliedEdit>      m_edits;
    std::u16string                m_replacement;
    SelectionModel::Snapshot      m_cursorsSnapshot;
    bool                          m_executed = false;
};

}  // namespace neomifes::core
```

**アルゴリズム:**
1. `search::SearchService::findAll(doc, query)` で全マッチを取得 (逆順にソート)
2. `execute()` は末尾から順に `doc.replace(range, replacement)` を呼ぶ (先頭から適用するとオフセットが崩れる)
3. `undo()` は先頭から順に `AppliedEdit::rangeBefore` へ元テキストを戻す
4. `cursorsAfterExecute()` / `cursorsAfterUndo()` は `m_cursorsSnapshot` を返す (カーソル移動を起こさない設計、`MultiCursorEditCommand` の分析で判明した `CommandDispatcher::dispatch()` の制約対応)

**キャプチャグループ対応 ($1, $2, ...):**
- `search::Query::regex = true` の場合、`SearchService::findAll` は現状「マッチ範囲のみ」を返す
- Phase 5b2 で `Match` に `std::vector<std::u16string> captures;` を追加、RE2 の `re2::RE2::Match()` 呼び出し時に captures 数分の `re2::StringPiece` を要求
- `ReplaceAllCommand` 実行時に `$1..$9` を captures[N-1] で置換 (`$$` はリテラル `$`、`$0` は全マッチ、それ以外はリテラル)

### 4.4 性能目標
- 100 万マッチ置換: ≤ 5 秒 (差分エンコード適用時 ≤ 2 秒)
- Undo/Redo: ≤ 100ms (差分再適用のみ)
- メモリ: 元テキスト合計サイズ + オフセット表 (置換前後のオフセットマップ)

### 4.5 テスト戦略
- 単体: 空文字列への置換、重複しないマッチ、キャプチャグループ、$$、regex fail 時の no-op
- Undo/Redo: 置換後の Undo で完全に元テキスト復元、Redo で再現
- 統合: 検索 → 置換 → もう一度検索 (置換文字列が新たにマッチしない、または意図通りマッチする)
- ベンチマーク: 100 万件の置換

### 4.6 影響ファイル (想定)
- **新規:** `src/core/replace_all_command.{h,cpp}`、`tests/unit/core_replace_all_command_test.cpp`、`tests/bench/replace_all_bench.cpp`
- **変更:** `src/search/include/neomifes/search/search_service.h` (`Match` に captures 追加)、`src/search/src/search_service.cpp` (RE2 の N-arg match 呼び出し)

---

## 5. Phase 5b3 — Find bar UI 配線 (+ Phase 5c: Grep)

### 5.1 機能ビジョン
- **凌駕元:** VSCode の Find bar 品質を Win32 ネイティブで再現
- **凌駕ポイント:** IME/クリップボード/カーソル点滅は OS に委譲 (WC_EDIT 子コントロール、決定済み) しつつ、マッチハイライトは D2D で全画面 60fps 維持。日本語検索が最初から自然に動く

### 5.2 UI/UX

**視覚:**
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow                                                          │
│  ┌────────────────────────────────────────────────┐                  │
│  │ Find:  [                              ] Aa Ww .*  ↑ ↓ x   3/12  │  ← Find bar (トップに固定)
│  │ Repl:  [                              ] Replace  All            │  ← Replace 行 (Ctrl+H 時のみ)
│  └────────────────────────────────────────────────┘                  │
│                                                                      │
│    text text [MATCH] text text text                                  │  ← マッチはハイライト、現在の位置は濃く
│    text [match] text [match] text                                    │
│    ...                                                               │
└──────────────────────────────────────────────────────────────────────┘
```

**キーバインド:**
- `Ctrl+F` — Find bar を開く (フォーカスを WC_EDIT へ)
- `Ctrl+H` — Replace 行も表示
- `F3 / Shift+F3` — 次/前のマッチへジャンプ
- `Alt+C / Alt+W / Alt+R` — Case / Word / Regex トグル
- `Esc` — Find bar を閉じる (フォーカスをテキスト領域へ)
- `Enter` (Find フォーカス時) — F3 と同じ
- `Enter` (Replace フォーカス時) — 現在マッチを置換
- `Ctrl+Enter` (Replace フォーカス時) — Replace All

### 5.3 データ構造・アルゴリズム

**新規状態 (Phase 4b6d の `altCursorAnchor` パターンに倣う):**
```cpp
// wWinMain スコープに追加、wireNormalMode() へ参照渡し:
struct FindBarState {
    HWND hwndFindEdit    = nullptr;
    HWND hwndReplaceEdit = nullptr;
    HWND hwndInfoLabel   = nullptr;   // "3/12" 表示
    bool visible         = false;
    bool replaceMode     = false;
    search::Query        currentQuery;
    std::vector<search::Match> currentMatches;
    std::size_t          currentMatchIndex = 0;
};
```

**Find bar の作成 (初回 `Ctrl+F` 時):**
- `CreateWindowExW(0, WC_EDITW, L"", WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, x, y, w, h, parent, id, hInstance, nullptr)`
- `SendMessageW(hwndFindEdit, WM_SETFONT, ...)` で `Yu UI Gothic` 相当の日本語対応フォントを設定
- サブクラス化: `SetWindowSubclass()` で Enter/Esc/F3/Ctrl+H を親へ転送 (Win32 標準の Edit control では飲まれるため)

**マッチハイライトの描画:**
- 新規 `render::MatchVisual { document::TextRange range; bool isCurrent; };`
- `RenderPipeline::setMatchVisuals(std::vector<MatchVisual>)` (既存 `CursorVisual`/`setCursorVisuals` と同じパターン)
- `drawMatchesOnLine()` で行の描画パス内に埋め込み (既存 `drawSelectionsOnLine` と同じ位置)
- 現在マッチ (`isCurrent = true`) はより濃い色

**インクリメンタル検索:**
- `WM_COMMAND / EN_CHANGE` を受けてクエリを更新
- **デバウンス:** `SetTimer(hwnd, ID_FIND_DEBOUNCE, 150, nullptr)` で 150ms 待ってから `SearchService::findAll` を呼ぶ (タイマは再入時にリセット)
- 検索は UI スレッドで実行 (現状 `findAll` は同期)。ドキュメントが大きい場合は Phase 5c で Worker 化

**「3/12」表示:**
- 現在マッチ = カーソル位置以降で最初のマッチ (無ければ先頭)、`currentMatchIndex + 1` / `currentMatches.size()` を `SetWindowTextW(hwndInfoLabel, ...)`

### 5.4 性能目標
- Ctrl+F → Find bar 表示 → フォーカス: ≤ 50ms
- インクリメンタル検索 (10MB ファイル、100 マッチ): ≤ 100ms
- マッチハイライト描画: 60fps を維持 (可視領域のマッチのみ描画)

### 5.5 Phase 5c — Grep / 複数フォルダ検索

**機能ビジョン:**
- **凌駕元:** 秀丸の Grep、サクラの Grep
- **凌駕ポイント:** Piece Table + RE2 のスループット (10MB/s 以上を目標) を、複数ファイル並列で発揮

**設計要点:**
- 新規 `search::GrepService` (Search Worker Pool、論理コア数-1 スレッド)
- `GrepQuery { std::vector<std::filesystem::path> roots; std::vector<std::u16string> includeGlobs; std::vector<std::u16string> excludeGlobs; Query query; };`
- 結果は `std::function<void(GrepMatch)>` コールバック (ストリーミング、UI は途中結果を表示)
- Grep 結果は新規モード `Mode::GrepResult` (基本設計 §3.3) で表示、行クリックで元ファイルへジャンプ

**性能目標:**
- 数 GB (100 万ファイル) の Grep: ≤ 30 秒
- 途中結果の最初の 100 件表示: ≤ 500ms
- CPU 論理コア数-1 での並列化効率 > 70%

**キーバインド:**
- `Ctrl+Shift+F` — Grep ダイアログ
- Grep 結果ウィンドウ: `F3/Enter` で次結果、`Ctrl+Enter` で元ファイル

### 5.6 テスト戦略 (Phase 5b3 + 5c)
- 単体: Find bar の Show/Hide 遷移、F3 のラップアラウンド、Escape でフォーカス復元
- 統合: Ctrl+F → 日本語入力 → インクリメンタル結果表示、Ctrl+H → 置換 (5b2 と結合)
- Grep: 10000 ファイル、include/exclude glob、大文字小文字、正規表現
- 手動 (UI): マッチハイライトが 60fps を維持することを目視確認

### 5.7 影響ファイル (Phase 5b3 + 5c)
- **新規:** `src/ui/find_bar.{h,cpp}`、`src/render/match_visual.h`、`src/search/src/grep_service.{h,cpp}`、`src/ui/grep_result_view.{h,cpp}`
- **変更:** `src/app/main.cpp` (FindBarState、Ctrl+F/Ctrl+H 配線)、`src/render/render_pipeline.{h,cpp}` (setMatchVisuals / drawMatchesOnLine)、`src/core/mode.h` (Mode::GrepResult)、`src/search/include/neomifes/search/search_service.h` (async 版 `findAllAsync` の追加)
- **新規テスト:** `tests/unit/ui_find_bar_test.cpp`、`tests/unit/search_grep_service_test.cpp`

---

## 6. Phase 6 — エンコーディング + 自動判定

### 6.1 機能ビジョン
- **凌駕元:** サクラの多言語対応・秀丸の Shift-JIS 品質
- **凌駕ポイント:** 全対応エンコーディングを **自前実装** (依存追加ゼロ、20MB メモリ目標に貢献)、自動判定は 3 段階で 99% 以上の正確性

### 6.2 対応エンコーディング (要件定義書 §6)
UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE / UTF-32 (LE/BE) / Shift-JIS / EUC-JP / ISO-2022-JP

### 6.3 データ構造・アルゴリズム

**新規モジュール `src/encoding/`:**
```cpp
// include/neomifes/encoding/encoding.h
namespace neomifes::encoding {

enum class Encoding {
    Unknown,
    Utf8, Utf8Bom,
    Utf16Le, Utf16LeBom,
    Utf16Be, Utf16BeBom,
    Utf32Le, Utf32LeBom,
    Utf32Be, Utf32BeBom,
    ShiftJis,
    EucJp,
    Iso2022Jp,
};

enum class LineEnding { Crlf, Lf, Cr, Mixed };

struct DecodeResult {
    std::u16string  text;
    Encoding        detectedEncoding;
    LineEnding      detectedLineEnding;
    std::size_t     invalidByteCount;   // 不正バイト数 (置換文字で埋めた個数)
};

class Encoder {
public:
    [[nodiscard]] static DecodeResult decode(std::span<const std::byte> bytes,
                                              Encoding hint = Encoding::Unknown);
    [[nodiscard]] static std::vector<std::byte> encode(std::u16string_view text,
                                                        Encoding target,
                                                        LineEnding lineEnding);
};

class EncodingDetector {
public:
    [[nodiscard]] static Encoding detect(std::span<const std::byte> head64k);
};

}  // namespace neomifes::encoding
```

**自動判定の 3 段階:**
1. **BOM 判定** (1μs 以下): 先頭 4 バイトで UTF-8 BOM / UTF-16 LE/BE BOM / UTF-32 LE/BE BOM を確定
2. **文字分布統計** (BOM 無し時、数 ms): ISO-2022-JP のエスケープシーケンス (`ESC $ B`, `ESC ( B`) の検出 → UTF-8 バリデーション (RFC 3629) → 失敗時 Shift-JIS/EUC-JP 判定
3. **N-gram モデル** (統計で確信度低い時): 日本語 2-gram 頻度表 (組込リテラル、~4KB) と照合し確信度算出。ラフに Shift-JIS/EUC-JP を区別

**Shift-JIS 判定のポイント:**
- Shift-JIS 第 1 バイト範囲: `0x81..0x9F` / `0xE0..0xFC`、第 2 バイト範囲: `0x40..0x7E` / `0x80..0xFC`
- EUC-JP 第 1 バイト範囲: `0xA1..0xFE`、第 2 バイト範囲: `0xA1..0xFE`
- Shift-JIS で有効かつ EUC-JP で無効なバイト列 (`0x80..0xA0` 領域など) を優先マーカとして使用

**行末コード判定:**
- 先頭 64KB 中の `\r\n` / `\n` / `\r` の出現回数を数え、多数派を採用
- 混在は `LineEnding::Mixed` として記録、UI で警告

**メモリマップドファイル対応:**
- 10GB ファイルでは全体をデコードせず、`Document::load` が要求した範囲のみデコード
- Piece Table の Original Buffer は「元バイト列 + Encoding タグ」を保持し、pieceView 要求時に該当範囲をデコード
- 遅延デコードキャッシュを `docs/issues/lazy_decode_mmap.md` で先取り予告済み — Phase 6 の副産物として実装

### 6.4 性能目標
- 自動判定 (64KB head): ≤ 5ms
- 1MB Shift-JIS ファイル読込 + 全デコード: ≤ 50ms
- 10GB UTF-8 ファイル読込 (mmap + 表示範囲のみデコード): ≤ 100ms
- 全 8 エンコーディング × 全 3 行末で「往復して同一バイト列」を確認するラウンドトリップテスト全通過

### 6.5 テスト戦略
- 単体: 各エンコーディングの代表ファイル、BOM 有無、不正バイト、境界文字 (半角/全角混在、絵文字)
- ラウンドトリップ: `encode(decode(bytes)) == bytes` を全エンコーディングで確認
- 自動判定: 「日本語文学作品を各エンコーディングでエンコードしたコーパス」100 ファイルで 99% 以上の判定正確性
- 統合: メモ帳/秀丸/サクラで作った実ファイルを開き、化けないこと

### 6.6 影響ファイル
- **新規:** `src/encoding/{encoding.cpp, encoder_utf8.cpp, encoder_utf16.cpp, encoder_utf32.cpp, encoder_shift_jis.cpp, encoder_euc_jp.cpp, encoder_iso_2022_jp.cpp, detector.cpp}`、`include/neomifes/encoding/encoding.h`、`tests/unit/encoding_*_test.cpp` 群、`tests/integration/encoding_roundtrip_test.cpp`
- **変更:** `src/document/document.cpp` (エンコーディング指定つき load、遅延デコード)、`src/app/main.cpp` (「エンコーディング指定して開く」メニュー、ステータスバー表示)

---

## 7. Phase 7 — シンタックスハイライト・アウトライン・折り畳み

### 7.1 機能ビジョン
- **凌駕元:** 秀丸のアウトライン解析、VSCode のシンタックスハイライト
- **凌駕ポイント:** ハイライトは非同期増分解析で 60fps を絶対落とさない。アウトラインは秀丸並みに賢い階層抽出。折り畳みは 100 万行対応

### 7.2 対応言語 (Phase 7 の一次スコープ、要件定義書 §6 の「対応ファイル」に対応)
必須: C / C++ / TypeScript / JavaScript / Python / Java / Go / Rust / PHP / HTML / CSS / JSON / XML / YAML / SQL / Markdown / PowerShell / VB / VBS / BAT / Shell / INI

### 7.3 データ構造・アルゴリズム

**シンタックスハイライトエンジン選定 (要件定義書と ADR-003 の再確認):**
- **一次候補:** TextMate grammar (VSCode エコシステム流用、`.tmLanguage.json`)
- **二次候補:** tree-sitter (WASM 除外版、Rust 依存回避のため C API を静的リンク)
- **決定基準:** Phase 7a で両方の PoC を行い、C++/TS/Python の 3 言語で以下を比較:
  - 起動時のパーサ ready 時間 (目標 ≤ 50ms)
  - 100 万行ファイルの初回全解析時間 (目標 ≤ 5 秒)
  - 増分解析 (1 文字入力後の再解析範囲): tree-sitter は真の増分解析、TextMate はライン単位再解析
  - バイナリサイズ (tree-sitter は言語ごと数百 KB のパーサを同梱するため膨張リスク)
- **決定は ADR-013 として発行** (Phase 7a 完了時)

**モジュール構成 `src/syntax/`:**
```cpp
// include/neomifes/syntax/syntax.h
namespace neomifes::syntax {

enum class TokenKind {
    Text, Keyword, Type, Function, Variable, Number, String,
    Comment, Operator, Punctuation, Preprocessor, Attribute,
    Error, /* ...拡張予定 */
};

struct Token {
    document::TextRange range;
    TokenKind           kind;
    std::uint16_t       userKind = 0;   // TextMate scope 用の追加識別子
};

struct FoldRange {
    document::TextRange range;
    std::u16string      preview;    // 折り畳んだ時に表示する 1 行
    bool                folded = false;
};

struct OutlineNode {
    std::u16string           name;
    document::TextPos        pos;
    int                      level;
    std::vector<OutlineNode> children;
};

class SyntaxEngine {
public:
    void registerLanguage(std::u16string_view id, std::unique_ptr<ILanguageDefinition>);
    void attachToDocument(document::Document& doc, std::u16string_view languageId);
    // 増分解析: DocumentChanged イベントを購読し、影響範囲のみ再解析
    // 解析結果は Rendering / Outline / Folding へ通知
};

class ILanguageDefinition {
public:
    virtual ~ILanguageDefinition() = default;
    virtual std::vector<Token>       tokenize(std::u16string_view text) = 0;
    virtual std::vector<FoldRange>   computeFolds(std::u16string_view text) = 0;
    virtual std::vector<OutlineNode> computeOutline(std::u16string_view text) = 0;
};

}  // namespace neomifes::syntax
```

**Rendering との統合:**
- Rendering の `LineLayout` が `std::vector<Token>` を保持
- `DirectWrite` の `IDWriteTextLayout::SetDrawingEffect` でトークンごとにブラシを設定 (色分け)
- 色定義は Theme (`docs/design/detailed_design.md` §5 の Theme に統合)

**非同期増分解析:**
- Syntax Worker Thread (1 本、基本設計 §2.4)
- `DocumentChanged` イベントを受け、変更範囲を含む「解析単位」(TextMate: 影響行〜次の中立点、tree-sitter: 影響サブツリー) だけ再解析
- 解析中は古いトークンを描画に使い続ける (60fps 死守)
- 解析完了後 `PostMessageW(WM_APP+SYNTAX_READY, ...)` で UI スレッドへ通知、`invalidate(range)` を呼ぶ

**折り畳み:**
- `FoldingModel` (新規 `src/core/folding_model.{h,cpp}`) がドキュメント論理行 → 表示行の対応表を持つ
- 表示行 = 論理行 - 折り畳まれた行数 (先頭からの累積)
- `Viewport` が表示行で管理、`Rendering` は表示行で描画、内部で論理行に変換
- 折り畳みマーカは Line Gutter (基本設計 §3.1) の右端に `+/-` 記号

**アウトライン:**
- 新規 `src/ui/outline_pane.{h,cpp}` — 右側に折り畳み可能なツリーコントロール (Win32 `WC_TREEVIEW`)
- ツリーノードクリックで該当行へジャンプ
- 秀丸のようにドキュメント種類ごとに階層抽出ルール (関数/クラス/セクションヘッダなど)

### 7.4 性能目標
- 100 万行 C++ ファイルの初回全解析: ≤ 5 秒 (バックグラウンド)
- 1 文字入力後の増分解析: ≤ 50ms (視覚遅延無し)
- 折り畳み展開/折りたたみ: ≤ 100ms (10000 fold)
- スクロール時のシンタックス再計算デバウンス: 30ms 遅延 (基本設計 §4.4)

### 7.5 テスト戦略
- 単体: 各言語のトークン分類、折り畳み範囲、アウトライン抽出
- 増分: 「関数の中に 1 文字入力 → 影響範囲のみ再トークン化」の再解析範囲検証
- 統合: 大規模ファイル (Linux kernel の C ファイル、1MB 級 JSON) を開いてハイライトが正しいこと
- ベンチマーク: 全解析・増分解析

### 7.6 影響ファイル
- **新規:** `src/syntax/{syntax_engine.cpp, textmate_grammar.cpp, treesitter_language.cpp (二次候補), token_stream.cpp, outline_extractor.cpp, folding_computer.cpp}`、`include/neomifes/syntax/syntax.h`、`src/core/folding_model.{h,cpp}`、`src/ui/outline_pane.{h,cpp}`、`third_party/` にパーサ (選定結果次第)
- **変更:** `src/render/render_pipeline.cpp` (トークンごと着色)、`src/render/line_layout.cpp` (Token 保持)、`src/app/main.cpp` (アウトラインペイン配線)、`src/document/document.cpp` (DocumentChanged 通知)

---

## 8. Phase 8 — プラグインエンジン + SDK

### 8.1 機能ビジョン
- **凌駕元:** サクラの JS プラグイン、秀丸のマクロ、VSCode の拡張機能
- **凌駕ポイント:** C ABI で公開し、ホットロードでプラグイン開発体験を大幅短縮。**AI/Git/LSP を含む全上位機能をプラグイン境界で切り離し**、本体はプラグイン無しで 100% 動作 (要件定義書 §7 の絶対条件)

### 8.2 UI/UX
- `Ctrl+Shift+X` — プラグイン管理ウィンドウ
- 一覧・有効/無効切替・アンロード・リロード
- プラグイン設定 (JSON5 でスキーマ駆動 UI)

### 8.3 データ構造・アルゴリズム

**C ABI 境界 `include/neomifes/plugin_sdk.h`:**
```c
// C ヘッダ (C++ からも使える)、ABI 安定性のため C linkage
#ifdef __cplusplus
extern "C" {
#endif

typedef struct NeoMifesPluginContext NeoMifesPluginContext;
typedef struct NeoMifesDocument      NeoMifesDocument;

typedef struct NeoMifesPluginInfo {
    const wchar_t* id;              // 例: "ai.claude"
    const wchar_t* name;
    const wchar_t* version;
    const wchar_t* author;
    unsigned int   apiVersion;      // NEOMIFES_PLUGIN_API_VERSION と一致必須
} NeoMifesPluginInfo;

typedef struct NeoMifesPluginVTable {
    void (*onLoad)(NeoMifesPluginContext* ctx);
    void (*onUnload)(NeoMifesPluginContext* ctx);
    void (*onDocumentChanged)(NeoMifesPluginContext* ctx, NeoMifesDocument* doc,
                              const wchar_t* changeJson);  // JSON で構造化
    // 追加コールバックは後方互換のため vtable 末尾追加のみ、削除禁止
} NeoMifesPluginVTable;

// DLL 側が実装するエントリポイント (規約):
__declspec(dllexport) const NeoMifesPluginInfo*  neomifes_plugin_info(void);
__declspec(dllexport) const NeoMifesPluginVTable* neomifes_plugin_vtable(void);

// 本体側が提供する API (関数ポインタで渡す、DLL の import 依存を避ける):
typedef struct NeoMifesCoreApi {
    unsigned int apiVersion;
    // ドキュメント操作 (全て Command 経由で発火、直接変更は不可)
    void   (*insertText)(NeoMifesDocument* doc, const wchar_t* text, unsigned line, unsigned column);
    void   (*deleteRange)(NeoMifesDocument* doc, unsigned lineStart, unsigned columnStart,
                          unsigned lineEnd,   unsigned columnEnd);
    // 読み取り
    unsigned (*getLineCount)(NeoMifesDocument* doc);
    void   (*getLineText)(NeoMifesDocument* doc, unsigned line, wchar_t* buffer, unsigned bufferLen);
    // コマンド登録
    void   (*registerCommand)(NeoMifesPluginContext* ctx, const wchar_t* id,
                              void (*callback)(NeoMifesPluginContext*));
    // UI
    void   (*showToast)(NeoMifesPluginContext* ctx, const wchar_t* message);
    // 追加 API も末尾追加のみ、削除禁止
} NeoMifesCoreApi;

#ifdef __cplusplus
}
#endif
```

**プラグインホスト (本体側 `src/plugin/plugin_host.{h,cpp}`):**
- `LoadLibraryW` で DLL ロード → `GetProcAddress` で 2 エントリ取得 → `apiVersion` 検証 → `onLoad` 呼出
- ホットアンロード: `onUnload` 呼出 → プラグイン参照カウント 0 まで待機 (セーフポイント) → `FreeLibrary`
- クラッシュ隔離: 各コールバック呼出を SEH `__try/__except` で囲み、クラッシュ時はプラグイン無効化 + ログ
- 権限モデル: プラグインは `CoreApi` を通じてしかドキュメントを変更できない = 内部で自動的に Command 化・Undo 対応

**マニフェスト検証:**
- `%APPDATA%\NeoMIFES\plugins\<id>\manifest.json5` を DLL と一緒に配置
- スキーマ: `id, name, version, author, apiVersion, permissions (network, filesystem, subprocess), signature`
- 未署名プラグインは初回ロード時に確認ダイアログ (Enterprise 設定で無効化)

### 8.4 性能目標
- プラグインロード: ≤ 100ms/個
- コールバック 1 回のオーバーヘッド: ≤ 10μs
- 10 個ロード状態でも起動時間 ≤ 500ms (200ms 分の余裕、200 個までは lazy load)

### 8.5 テスト戦略
- サンプルプラグイン: `plugins/samples/hello_plugin/` (最小)、`plugins/samples/word_count/` (ドキュメント読み取り)、`plugins/samples/uppercase_command/` (Command 登録)
- 単体: apiVersion 不一致で拒否、`onLoad` throw で無効化、二重ロード検出、ホットアンロードで参照カウント確認
- ソークテスト: 100 回ロード/アンロードでハンドルリーク無し、メモリリーク無し (Phase 12 で再走)
- 統合: プラグインからの Command 発行が Undo/Redo に正しく積まれる

### 8.6 影響ファイル
- **新規:** `src/plugin/plugin_host.{h,cpp}`、`src/plugin/plugin_manifest.{h,cpp}`、`src/plugin/plugin_permission.{h,cpp}`、`include/neomifes/plugin_sdk.h`、`plugins/samples/` 3 種、`tests/integration/plugin_load_test.cpp`
- **変更:** `src/app/main.cpp` (プラグインロード配線、`Ctrl+Shift+X`)、`src/core/command_dispatcher.cpp` (プラグイン発行 Command の受入)、`CMakeLists.txt` (SDK ヘッダ配布、サンプルビルド)

---

## 9. Phase 9 — AI プラグイン (Claude 統合)

### 9.1 機能ビジョン
- **凌駕元:** どの三大エディタも未対応 (完全に新規領域)
- **凌駕ポイント:** AI 機能は完全にプラグイン境界で動作。API キーは Windows Credential Manager (DPAPI) 経由で暗号化保存、コアには一切漏れない

### 9.2 対応 AI (Phase 9 の一次スコープ)
- **Claude** (Anthropic Messages API、`claude-opus-4-8` / `claude-sonnet-5` / `claude-haiku-4-5`)
- **ChatGPT** (OpenAI Chat Completions API)
- **Gemini** (Google Generative Language API)
- **OpenAI 互換 API** (Groq / DeepSeek / Ollama など、URL 差替のみ)

### 9.3 提供機能 (要件定義書 §7)
| コマンド | UI | プロンプト戦略 |
|---|---|---|
| コードレビュー | 選択範囲 + `Ctrl+Alt+R` | プロジェクト設定の Coding Guide を system prompt に |
| コード生成 | インラインチャット (`Ctrl+Alt+G`) | 選択位置の前後 300 行を context、指示を user prompt |
| ログ解析 | ログ解析モード連携 (Phase 10) | ERROR 抽出済み行を context |
| SQL 改善 | 選択範囲 + `Ctrl+Alt+S` | SQL dialect 自動判定 |
| Shell 生成 | インラインチャット | OS = Windows、PowerShell を優先指定 |
| 文章要約 | 選択範囲 + `Ctrl+Alt+U` | 出力言語は入力と同一 |
| 翻訳 | 選択範囲 + `Ctrl+Alt+T` | 対象言語をコマンドパレットで選択 |
| 説明 | 選択範囲 + `Ctrl+Alt+E` | プログラムなら「何をしているか」、文章なら「要点」 |
| コメント生成 | 選択関数 + `Ctrl+Alt+C` | 言語別 doc-comment スタイル自動選択 |
| リファクタリング | 選択範囲 + `Ctrl+Alt+F` | ADR-013 (シンタックス) 選定に依らず AST 情報を context に含めない (シンプルさ優先) |
| エラー解析 | LSP 診断メッセージから (Phase 11 連携) | 診断メッセージ + 該当行を context |

### 9.4 データ構造・アルゴリズム

**AI プラグイン `src/ai/` (別ビルドターゲット、DLL):**
```cpp
// src/ai/ai_provider.h
namespace neomifes::ai {

struct ChatMessage {
    enum class Role { System, User, Assistant };
    Role role;
    std::u16string content;
};

struct GenerateRequest {
    std::vector<ChatMessage> messages;
    int maxTokens         = 4096;
    double temperature    = 0.7;
    std::u16string model;  // 例: "claude-sonnet-5"
};

class IAiProvider {
public:
    virtual ~IAiProvider() = default;
    // ストリーミング応答: chunk callback は AI Worker Thread から呼ばれる
    virtual void generate(const GenerateRequest& req,
                          std::function<void(std::u16string_view chunk)> onChunk,
                          std::function<void(std::optional<std::u16string> error)> onComplete) = 0;
};

class ClaudeProvider  : public IAiProvider { /* ... */ };
class OpenAiProvider  : public IAiProvider { /* ... */ };
class GeminiProvider  : public IAiProvider { /* ... */ };

}  // namespace neomifes::ai
```

**HTTP クライアント選定 (ADR-004 で保留):**
- **一次候補:** WinHTTP (Windows 標準、依存無し) — HTTP/2 対応の Windows 10 以降で十分
- **二次候補:** libcurl (静的リンク) — 動作確実性優先の場合
- **決定基準:** Phase 9a で PoC。ストリーミング応答 (chunked transfer / SSE) の実装容易性で判断
- **決定は ADR-004 の Superseded で明記** (現状 `docs/decisions/ADR-004-http-client.md` は保留状態)

**API キー保管:**
- Windows Credential Manager (`CredWriteW` / `CredReadW`)
- Target Name: `NeoMIFES/AI/<provider>` (例: `NeoMIFES/AI/Claude`)
- Type: `CRED_TYPE_GENERIC`
- DPAPI (自動でユーザー鍵で暗号化)
- 設定ダイアログでの入力時のみメモリ上に平文、AI Worker Thread で HTTP リクエストヘッダに埋めた後即メモリ zero-fill

**インラインチャット UI:**
- カーソル位置に半透明パネル (Win32 レイヤードウィンドウ)
- 入力欄 (WC_EDIT) + 応答領域 (Direct2D 描画)
- Enter で送信、Esc でキャンセル (ストリーミング中もキャンセル可能、`CancelHttpRequest`)
- 応答完了後 `Ctrl+Enter` でカーソル位置に挿入 (通常の InsertTextCommand として Undo 可)

**プレビュー UI (コードレビュー・リファクタなど):**
- 応答が「Diff 形式」の場合、`git diff` 風の差分ビューを表示
- 承認 → Command 化して apply、拒否 → 破棄

### 9.5 セキュリティ・プライバシー
- **AI コンテキストに含めるデータの明示:** ユーザー選択範囲 + カーソル前後 N 行以外は送信しない、と設計で確定 (プロジェクトファイル全体を勝手に送らない)
- **オプトイン明示:** 初回起動時「AI プラグインを有効化しますか」のダイアログ、無効時は 100% ネットワーク I/O 発生させない (要件定義書 §7 の絶対条件)
- **監査ログ:** `%LOCALAPPDATA%\NeoMIFES\logs\ai-YYYYMMDD.jsonl` にリクエスト/応答の要約 (トークン数のみ、内容は含めない) を記録
- **オフライン開発:** OpenAI 互換 API のエンドポイントを `http://localhost:11434` (Ollama) 等に切替可能に

### 9.6 性能目標
- API キー未設定時は AI プラグイン非ロード (起動時間 20MB / 300ms への影響ゼロ)
- API キー設定時、AI プラグイン初期化: ≤ 50ms
- ストリーミング応答の最初のチャンク受信: 各プロバイダの API 素の応答時間 + 10ms 以内
- HTTP エラー時のリカバリ: 5 秒以内にタイムアウト・ユーザー通知

### 9.7 テスト戦略
- 単体: プロバイダごとのリクエスト JSON 組み立て、レスポンス JSON パース、SSE ストリーミング分割
- モック: `IHttpClient` インターフェース化してテスト用モック、本物 API はエンドツーエンドテストのみ
- 統合: プロバイダ切替、キャンセル、タイムアウト
- 手動: 各プロバイダで代表機能を実行し、応答品質を目視確認 (Phase 9 完了判定条件)

### 9.8 影響ファイル
- **新規:** `src/ai/{ai_provider.h, claude_provider.cpp, openai_provider.cpp, gemini_provider.cpp, http_client.cpp, sse_parser.cpp, credential_store.cpp}`、`src/ui/inline_chat.{h,cpp}`、`src/ui/ai_diff_preview.{h,cpp}`、`plugins/ai_plugin/` (DLL エントリ)、`tests/unit/ai_*_test.cpp` 群
- **変更:** `src/app/main.cpp` (Ctrl+Alt+* 配線、初回起動時のオプトインダイアログ)、`docs/decisions/ADR-004-http-client.md` (Superseded 記録)

---

## 10. Phase 10 — ログ解析モード / CSV モード / JSON-XML Tree モード

要件定義書 §8-10 が指定する **本ソフト最大の差別化要素**。Phase 10 は 3 つの新規モードを実装する。

### 10.1 ログ解析モード (要件定義書 §8) — 本ソフト最大の特徴

#### 機能ビジョン
数十 GB のログを ERROR/WARNING 抽出しながら時系列ジャンプで探索できる、Windows で類を見ないログエディタ。

#### 対象
SAP / AWS / Azure / Linux syslog / Windows Event Log Text Export / Apache / Nginx / Oracle alert.log / SAP HANA / Tomcat catalina.out / Java (log4j/logback) / Docker / Kubernetes (kubectl logs)

#### UI/UX
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow (Log Mode)                                               │
│  ┌─────────┬────────────────────────────────────────────────────┐   │
│  │ Filter  │  2026-07-19 10:15:32 INFO  App started              │   │
│  │ □ INFO  │  2026-07-19 10:15:33 WARN  Config missing default   │   │
│  │ ☒ WARN  │  2026-07-19 10:15:34 ERROR Failed to connect DB     │   │
│  │ ☒ ERROR │  ...                                                 │   │
│  │ Time    │                                                      │   │
│  │ ├ 10:15 │                                                      │   │
│  │ ├ 10:30 │                                                      │   │
│  │ └ 11:00 │                                                      │   │
│  └─────────┴────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

- 左ペイン: レベルフィルタ (☒でチェック時のみ表示)、時系列ツリー (時間帯ジャンプ)
- 右ペイン: 通常のテキスト表示、行の色分け (INFO=灰, WARN=黄, ERROR=赤, FATAL=濃赤太字)

#### データ構造・アルゴリズム
```cpp
// src/logmode/log_pattern.h
struct LogPatternRule {
    std::u16string id;           // "log4j", "syslog", "nginx-access"
    std::u16string regex;        // タイムスタンプ抽出
    std::u16string levelField;   // "$3" 等のキャプチャ参照
    std::vector<std::u16string> levelMap;  // ["INFO", "WARN", "ERROR"]
};

// 組込パターンを %APPDATA% にコピーしてユーザーが編集可能に

class LogModel {
public:
    void attach(document::Document& doc, const LogPatternRule& rule);
    // 各行のインデックス:
    struct LogLine {
        document::TextPos    pos;
        std::optional<Timestamp> timestamp;
        LogLevel             level;
    };
    [[nodiscard]] std::span<const LogLine> lines() const noexcept;
    void applyFilter(LogFilter filter);   // レベルフィルタ・時間帯フィルタ
};
```

- **インデックス構築は非同期・チャンク単位** (Piece Table のピース単位、100 万行以上でも UI ブロックなし)
- インデックス構築中はプログレスバー表示、確定した範囲から順に色分け反映
- タイムスタンプ検出は「先頭 100 行で最頻の日付フォーマットを推定」→ 全体に適用
- 時系列ジャンプは B+Tree (`docs/issues/line_index_o_log_n.md` の共通化)

#### 性能目標
- 10GB ログの初回インデックス構築: ≤ 60 秒 (バックグラウンド)
- インデックス構築中のスクロール: 60fps 維持
- レベルフィルタ切替: ≤ 100ms
- 時系列ジャンプ: ≤ 50ms

#### 影響ファイル
- **新規:** `src/logmode/{log_pattern.h, log_pattern_loader.cpp, log_model.cpp, log_filter.cpp, timestamp_parser.cpp}`、`src/ui/log_mode_pane.{h,cpp}`、`assets/log_patterns/*.json5` (組込パターン 12 種)、`tests/unit/logmode_*_test.cpp`
- **変更:** `src/app/main.cpp` (ログモード検出・切替)、`src/core/mode.h` (Mode::Log)

### 10.2 CSV モード (要件定義書 §9)

#### 機能ビジョン
Excel を使わず 1000 万行の CSV を閲覧・軽編集できる。

#### UI/UX
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow (CSV Mode)                                               │
│  ┌───┬─────────┬──────────┬──────────┬──────────┐                    │
│  │ # │ Name    │ Age      │ City     │ Score    │  ← 列固定 (Freeze)  │
│  ├───┼─────────┼──────────┼──────────┼──────────┤                    │
│  │ 1 │ Alice   │ 30       │ Tokyo    │ 85.5     │                    │
│  │ 2 │ Bob     │ 25       │ Osaka    │ 92.1     │                    │
│  │...│ ...     │ ...      │ ...      │ ...      │                    │
│  └───┴─────────┴──────────┴──────────┴──────────┘                    │
│  [Filter: City == Tokyo] [Sort: Score desc]                          │
└──────────────────────────────────────────────────────────────────────┘
```

- 列固定 (Excel の「先頭行の固定」)、フィルタ、ソート
- セル単位のクリック編集 (WC_EDIT 子コントロールを出現させる)
- TSV 対応、区切り文字自動判定 (`,` / `\t` / `;` / `|`)

#### データ構造・アルゴリズム
```cpp
// src/csvmode/csv_model.h
class CsvModel {
    document::Document* m_doc = nullptr;
    // 各行の列オフセット表 (先頭バイト位置)、遅延構築
    std::vector<std::vector<std::uint32_t>> m_columnOffsets;
    std::vector<std::u16string>            m_headers;
    // フィルタ・ソートはインデックス列 (view) を返す
    std::vector<std::size_t>               m_visibleRows;
};
```

- 列オフセット表は遅延構築 (可視範囲のみ)
- フィルタ後の順序は `m_visibleRows` で保持し、実データを並べ替えない (メモリ効率、Undo 容易性)
- 1000 万行対応: `m_columnOffsets` は `std::uint32_t` (4 バイト/オフセット × 列数 × 行数、100 列 × 1000 万 = 4GB — 現実的には行数超過時は列ヘッダ + 都度パース戦略に切替)

#### 性能目標
- 1000 万行 CSV の初回パース: ≤ 30 秒
- 列固定スクロール: 60fps
- フィルタ適用: ≤ 1 秒 (100 万行)
- ソート: ≤ 3 秒 (100 万行、int/文字列)

#### 影響ファイル
- **新規:** `src/csvmode/{csv_model.cpp, csv_parser.cpp, csv_filter.cpp, csv_sorter.cpp}`、`src/ui/csv_grid_view.{h,cpp}`、`tests/unit/csvmode_*_test.cpp`
- **変更:** `src/app/main.cpp` (CSV モード検出・切替)、`src/core/mode.h` (Mode::Csv)

### 10.3 JSON / XML Tree モード (要件定義書 §10)

#### 機能ビジョン
JSON / XML の階層をツリーで見つつ、テキストとしても編集できる。**三大エディタが持たない差別化点**。

#### UI/UX
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow (JSON Mode)                                              │
│  ┌──────────────────┬─────────────────────────────────────────────┐  │
│  │ ▼ root           │ {                                            │  │
│  │   ▼ users [3]    │   "root": {                                  │  │
│  │     ▼ [0]        │     "users": [                               │  │
│  │       name: Alice│       {                                      │  │
│  │       age: 30    │         "name": "Alice",                     │  │
│  │     ▶ [1]        │         "age": 30                            │  │
│  │     ▶ [2]        │       },                                     │  │
│  │   ▶ config       │       ...                                    │  │
│  └──────────────────┴─────────────────────────────────────────────┘  │
│  [Format] [Validate] [XPath: /root/users[0]/name] [Copy Path]        │
└──────────────────────────────────────────────────────────────────────┘
```

- 左ペイン: ツリー、右ペイン: テキスト
- ノードクリックでテキスト側のスクロール、逆にテキストのカーソル位置に対応するノードをハイライト
- 整形 (Format) / バリデーション / XPath (XML) / JSONPath (JSON)

#### データ構造・アルゴリズム
- JSON: `simdjson` 検討 → 判断は Phase 10 着手時 ADR
- XML: `pugixml` (MIT ライセンス、軽量) 検討
- 巨大 JSON (1GB+) は SAX 解析 + 部分ツリー展開 (ノード展開時にサブツリーを解析)
- XPath / JSONPath は自前実装 (依存追加を避ける、共に文法が単純)

#### 性能目標
- 100MB JSON のツリー構築 (先頭部分のみ): ≤ 500ms
- ノード展開: ≤ 100ms
- 整形: ≤ 1 秒 (10MB JSON)

#### 影響ファイル
- **新規:** `src/tree/{json_parser.cpp, xml_parser.cpp, tree_model.cpp, xpath.cpp, jsonpath.cpp, formatter.cpp}`、`src/ui/tree_view_pane.{h,cpp}`、`tests/unit/tree_*_test.cpp`
- **変更:** `src/app/main.cpp` (JSON/XML モード検出)、`src/core/mode.h` (Mode::JsonTree / Mode::XmlTree)

---

## 11. Phase 11 — Git 統合 / LSP 統合 / マクロ

### 11.1 Git 統合 (要件定義書 §11)

#### 機能ビジョン
- **凌駕元:** 秀丸の DIFF ビュー
- **凌駕ポイント:** libgit2 で Diff / 3-Way Merge / Blame / Commit / Branch 切替を本体に統合

#### UI/UX
- 左ガター (Line Gutter の左) に差分マーカ (Modified/Added/Deleted)
- `Ctrl+Shift+G` — Git ペイン (右側、ステータス / ブランチ / コミット履歴)
- Diff ビュー: `Alt+D` — 現在ファイルと HEAD の diff、side-by-side / inline 切替
- Blame: `Alt+B` — 行ごとに commit hash + author を左に表示

#### データ構造・アルゴリズム
```cpp
// src/git/git_repo.h
class GitRepo {
public:
    static std::optional<GitRepo> discover(const std::filesystem::path& start);
    [[nodiscard]] std::vector<DiffHunk> diffAgainstHead(std::u16string_view path) const;
    [[nodiscard]] std::vector<BlameLine> blame(std::u16string_view path) const;
    [[nodiscard]] std::vector<Branch>    branches() const;
    void checkout(std::u16string_view branch);
    // ...
};
```

- libgit2 を静的リンク (LGPL じゃなく GPL with linking exception、ライセンス互換確認)
- Git 操作は Git Worker Thread、UI ブロックしない
- Diff マーカは `IO Watcher` (ファイル変更監視) と連動して自動更新

#### 影響ファイル
- **新規:** `src/git/{git_repo.cpp, diff_computer.cpp, blame_reader.cpp, commit_dialog.cpp}`、`src/ui/git_pane.{h,cpp}`、`src/ui/diff_view.{h,cpp}`、`tests/integration/git_*_test.cpp`
- **変更:** `src/render/line_gutter.cpp` (差分マーカ描画)、`src/app/main.cpp` (Git ペイン配線)、`third_party/libgit2/`

### 11.2 LSP 統合 (要件定義書 §17 と CLAUDE.md 7)

#### 機能ビジョン
- **凌駕元:** サクラの LSP (△レベル)、VSCode の LSP
- **凌駕ポイント:** LSP 3.17 準拠、C++ / TypeScript / Python の 3 言語をまず完全対応 (basic_design.md R4 のリスク対策通り)

#### UI/UX
- 補完 (`Ctrl+Space`) — ドロップダウン (自前 Direct2D 描画、Win32 標準は使わない)
- 定義ジャンプ (`F12`) — 該当ファイルを開いて位置へ
- ホバー情報 (`Ctrl+マウスホバー`) — ツールチップ表示
- 診断メッセージ — 該当行にアンダーライン + ガター警告アイコン + 別ペインで一覧

#### データ構造・アルゴリズム
```cpp
// src/lsp/lsp_client.h
class LspClient {
public:
    // 各言語ごとに 1 プロセス、stdio JSON-RPC
    static std::unique_ptr<LspClient> spawn(const LspServerConfig& config);
    void initialize(const std::filesystem::path& workspaceRoot);
    void didOpen(const std::filesystem::path& file, std::u16string_view text);
    void didChange(const std::filesystem::path& file, const std::vector<TextEdit>& changes);
    void completion(const std::filesystem::path& file, TextPos pos,
                    std::function<void(CompletionList)> onResponse);
    // ...
};
```

- 対応サーバ:
  - C++: clangd
  - TypeScript: typescript-language-server (Node.js 必須)
  - Python: pylsp (`pip install python-lsp-server`)
- サーバは子プロセス、stdio 通信
- JSON-RPC 実装は自前 (nlohmann/json などを避ける、`std::format` で組み立て + 手書きパース)
- サーバの発見: `%PATH%` から自動検出、無ければ設定ダイアログで指定

#### 影響ファイル
- **新規:** `src/lsp/{lsp_client.cpp, lsp_protocol.cpp, lsp_config.cpp, lsp_message.cpp}`、`src/ui/{completion_popup.h, hover_tooltip.h, diagnostics_pane.h}` (+ cpp)、`tests/integration/lsp_*_test.cpp`
- **変更:** `src/app/main.cpp` (LSP 起動配線)、`src/render/render_pipeline.cpp` (診断アンダーライン)

### 11.3 マクロ (要件定義書 §12)

#### 機能ビジョン
- **凌駕元:** 秀丸マクロ (独自スクリプト言語)
- **凌駕ポイント:** Lua と JavaScript の両対応。両方ともプラグイン境界の上で動作 (プラグインとしてマクロランタイム DLL を配布)

#### 一次言語選定
- **Lua 5.4** (LuaJIT は Windows での JIT サポートが不安定、5.4 通常版を採用)
- **JavaScript** は QuickJS (軽量、埋込前提の設計、ISC ライセンス)

#### API 公開範囲
```lua
-- 例: Lua マクロ
local doc = neomifes.currentDocument()
local text = doc:getText(1, 1, 10, 1)   -- 1行1列〜10行1列
doc:insertText(1, 1, "-- header\n")
neomifes.showToast("Inserted!")
```

- `neomifes.currentDocument()` / `openFile(path)` / `saveFile()` / `showToast()` / `command(id)` (Command 呼出)
- ドキュメント API はプラグイン SDK と同じ Command 経由 (Undo 対応)

#### マクロ管理
- `%APPDATA%\NeoMIFES\macros\*.lua` / `*.js`
- ホットリロード
- キーバインド割当 (設定ダイアログから)
- マクロ記録機能 (キー入力を記録 → 生成された Lua/JS スクリプトを再生)

#### 影響ファイル
- **新規:** `src/macro/{macro_engine.cpp, lua_bindings.cpp, quickjs_bindings.cpp, macro_recorder.cpp}`、`plugins/macro_runtime/` (Lua/QuickJS を組み込んだプラグイン DLL)、`tests/unit/macro_*_test.cpp`
- **変更:** `src/app/main.cpp` (マクロランタイムロード配線、Ctrl+Shift+M 記録)

---

## 12. Phase 12 — 総合品質保証・出荷判定

### 12.1 目的
全機能を「出荷可能な品質」に引き上げる最終フェーズ。基本設計 §7 の品質保証方針の実現度を検証する。

### 12.2 実施項目

**静的解析:**
- MSVC `/analyze` を **CI に統合** (Phase 0.5 で導入見送りだった項目、Phase 12 で最終確認)
- clang-tidy: 既存の警告 0 を維持しつつ、`WarningsAsErrors: '.*'` を Release ビルドで有効化検討

**動的解析:**
- ASan / UBSan / CRT Leak Detection: 全テスト + 24 時間クラッシュソーク
- Application Verifier (Windows 標準): ハンドルリーク・ヒープ破壊検出
- MSVC `/GS /guard:cf` (Control Flow Guard) 有効化

**巨大ファイル検証 (手動):**
- 10GB UTF-8 テキストを開いて 60fps スクロール確認
- 10GB ログをログ解析モードで開いて時系列ジャンプ確認
- 1000 万行 CSV を CSV モードで開いてフィルタ・ソート確認

**Undo ソーク:**
- 100 万回連続 Undo/Redo を 24 時間繰り返す

**プラグインソーク:**
- 100 個のプラグインを 24 時間ロード/アンロードを繰り返す
- ハンドルリーク・メモリリーク 0

**AI 統合の網羅テスト:**
- 各プロバイダで代表機能を全て実行
- API キー未設定・オフライン・タイムアウト・レート制限のエラーパス

**LSP の網羅テスト:**
- C++ / TS / Python の 3 言語で補完・定義ジャンプ・診断
- サーバクラッシュ時の自動再起動

**アクセシビリティ・国際化:**
- スクリーンリーダ対応 (UI Automation)
- IME (日本語・中国語・韓国語) 入力
- 高 DPI (100%, 125%, 150%, 200%) で全 UI が破綻しないこと
- HDR モニタでのカラー精度

**コード署名・配布:**
- Authenticode 署名 (basic_design.md §6.6)
- MSIX パッケージング検討 (未決事項 #2)
- Portable Zip 版

### 12.3 出荷判定チェックリスト
- [ ] 起動時間 ≤ 300ms (Release、実機実測、統計 p95)
- [ ] 初期メモリ ≤ 20MB (Release、Working Set)
- [ ] 60fps スクロール (10GB ファイル、Release)
- [ ] 100 万 Undo (24 時間ソーク、メモリ膨張なし)
- [ ] 10GB ファイル対応 (全モード)
- [ ] 数 GB Grep ≤ 30 秒
- [ ] クラッシュ 0 (24 時間ソーク)
- [ ] メモリリーク 0 (Application Verifier)
- [ ] MSVC `/analyze` 新規指摘 0
- [ ] clang-tidy 新規指摘 0
- [ ] ASan/UBSan クラッシュ 0
- [ ] 全単体テスト pass (差分カバレッジ 80% 以上を全 PR で維持していたことの累積結果)
- [ ] AI プラグイン無効時に本体 100% 動作
- [ ] 日本語 IME 入力・スクリーンリーダ動作確認
- [ ] Windows 10 21H2 と Windows 11 での動作確認
- [ ] Authenticode 署名済みバイナリ配布

---

## 13. UI/UX トップレベル方針 (全フェーズ共通)

### 13.1 キーバインドプリセット
- **NeoMIFES 標準** (VSCode ベース、Ctrl+K/Ctrl+P など)
- **秀丸互換** (F5=マクロ実行、Alt+F=ファイルメニューなど)
- **サクラ互換** (Ctrl+Enter=改行挿入、Alt+↑↓=行移動など)
- **MIFES 互換** (F9=保存、F10=閉じるなど、キー割当は要件定義書と MIFES の実際のキー体系の交差を Phase 4b8 で確定)
- 設定ダイアログでプリセット選択、個別にカスタマイズも可

### 13.2 コマンドパレット
- `Ctrl+Shift+P` (VSCode 互換)
- 全コマンドを ID + 説明 + 現在のキーバインドで表示、あいまい検索
- Phase 4b8 完了後の Phase 5b3 と同時に着手 (Find bar と実装パターンが共通)

### 13.3 ダーク/ライトテーマ
- Windows のシステム設定 (`SPI_GETIMMERSIVECOLORSET` 相当) を初期値として反映、ユーザー設定でオーバーライド可
- Windows 11 Mica/Acrylic は Phase 3 で `DwmSetWindowAttribute` を PoC 済み、Phase 12 で正式対応

### 13.4 高 DPI / HDR
- Per-Monitor DPI V2 (Phase 3 で対応済み)
- HDR: `IDXGISwapChain::SetColorSpace1` を Phase 3c 完了後の追加改修で対応 (Phase 12 の一環)

---

## 14. パフォーマンス予算表 (全機能横断)

| 機能 | 目標 | 測定条件 | 対応 Phase |
|---|---|---|---|
| 起動 (splash 消失まで) | ≤ 300ms | Release、SSD、Windows 11、統計 p95 | Phase 1 (完了、実測 148ms) |
| 初期メモリ | ≤ 20MB | Release、Working Set | Phase 1 |
| 10GB ファイルオープン (先頭表示) | ≤ 100ms | mmap 前提 | Phase 6 |
| スクロール | 60fps | 10GB ファイル、Release | Phase 3 (完了) |
| 100 万 Undo | メモリ ≤ 500MB | 差分エンコード + 圧縮 | Phase 4b (完了、圧縮は要検証) |
| 検索 (1MB) | ≤ 100ms | Phase 5a 実測範囲 | Phase 5a (完了、33-39ms/1MB) |
| 検索 (1GB) | ≤ 30 秒 | チャンク並列化 | Phase 5c |
| Grep (数 GB) | ≤ 30 秒 | 論理コア数-1 並列 | Phase 5c |
| 置換 (100 万件) | ≤ 5 秒 | 差分エンコード | Phase 5b2 |
| エンコーディング判定 | ≤ 5ms | 64KB head | Phase 6 |
| シンタックス初回全解析 | ≤ 5 秒 | 100 万行 C++ | Phase 7 |
| シンタックス増分解析 | ≤ 50ms | 1 文字入力後 | Phase 7 |
| プラグイン 1 個ロード | ≤ 100ms | サンプルプラグイン | Phase 8 |
| AI 応答 (最初のチャンク) | プロバイダ API 素 + 10ms | ストリーミング | Phase 9 |
| ログインデックス (10GB) | ≤ 60 秒 | バックグラウンド | Phase 10 |
| CSV パース (1000 万行) | ≤ 30 秒 | 遅延構築 | Phase 10 |
| JSON tree 構築 (100MB) | ≤ 500ms | 先頭部分のみ | Phase 10 |
| LSP 補完応答 | ≤ 100ms | サーバ準備完了後 | Phase 11 |

---

## 15. リスク・未決事項の再整理

| # | リスク/未決 | 対応 Phase | 判断方法 |
|---|---|---|---|
| R2 (再) | 正規表現エンジン | Phase 5a で RE2 採用済 (ADR-002)、Hyperscan 再評価は Phase 5c の Grep 実測後 | 計測 |
| R3 (再) | シンタックス定義 (TextMate vs tree-sitter) | Phase 7a で PoC | 計測 (§7.3) |
| R4 (再) | LSP 統合の複雑性 | Phase 11a で C++/TS/Python の 3 言語限定 | スコープ制限 |
| R5 (再) | プラグイン DLL ホットアンロードでのリーク | Phase 8 + Phase 12 ソーク | Application Verifier |
| U#3 (再) | 正規表現エンジン最終選定 | R2 と同じ | — |
| U#4 (再) | シンタックス定義形式 | R3 と同じ (ADR-013) | — |
| U#5 (再) | マクロ言語同梱範囲 | Phase 11.3 で Lua + QuickJS の両対応で確定 | 本書 |
| U#6 (再) | LSP 初期対応言語 | Phase 11.2 で C++/TS/Python 確定 | 本書 |
| U#7 (再) | 設定ファイル形式 | JSON5 を第一候補 (basic_design.md §6.1)、Phase 6 完了後に最終確定 | — |
| U#8 (再) | 自動更新機構 | Phase 12 で MSIX 採用検討時に確定 | — |
| U#9 (新規) | AI プロバイダの HTTP クライアント | Phase 9a で PoC (WinHTTP vs libcurl) | ADR-004 更新 |
| U#10 (新規) | JSON パーサ (simdjson の Windows ABI 適合性) | Phase 10 着手時 | ADR |
| U#11 (新規) | Git 統合の libgit2 ライセンス互換 | Phase 11.1 着手時 | 弁護士確認不要範囲で自己判断可 |

---

## 16. 更新履歴

| 日付 | 版 | 変更 |
|---|---|---|
| 2026-07-19 | v1.0 | 初版発行 (Phase 5b1 完了後、Phase 4b8/5b2/5b3/6-12 の実装詳細を一気通貫で規定) |
