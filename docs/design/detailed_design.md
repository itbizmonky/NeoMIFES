# NeoMIFES 詳細設計書 v1.0

> 上位: [`basic_design.md`](basic_design.md) / 要件: [`../../NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md)

本書は各モジュールの内部データ構造・クラス設計・アルゴリズム・API 仕様を規定する「How」レベルのドキュメント。

---

## 1. 全体クラス構成 (概念図)

```
neomifes::
  app::         { WinMain, MessageLoop }
  ui::          { MainWindow, TabBar, StatusBar, CommandPalette, Dialogs }
  application:: { CommandDispatcher, UndoStack, SessionManager, ConfigManager }
  core::        { EditorView, Cursor, Selection, Viewport, Bookmark, ModeManager }
  render::      { RenderPipeline, TextLayoutCache, GlyphCache, DamageTracker }
  document::    { Document, PieceTable, LineIndex, TextBuffer, FileLoader }
  search::      { SearchService, RegexEngine, GrepWorkerPool }
  encoding::    { EncodingDetector, EncodingConverter }
  syntax::      { SyntaxHighlighter, GrammarLoader, FoldRangeProvider }
  plugin::      { PluginHost, PluginRegistry, PluginContext }
  ai::          { AIPluginBase, ClaudeProvider, ... (別 DLL) }
  platform::    { HandleGuard, FileMap, ThreadPool, ClockNS }
  util::        { Result<T,E>, SpanUtf16, Utf8Codec, LRUCache }
```

---

## 2. 主要インターフェース (抽象)

### 2.1 `ITextBuffer`
```cpp
namespace neomifes::document {

// 論理位置 = UTF-16 コードユニットオフセット
using TextPos = std::uint64_t;

struct TextRange {
    TextPos start;
    TextPos end; // exclusive
};

class ITextBuffer {
public:
    virtual ~ITextBuffer() = default;

    // 読み取り (スナップショット取得)
    [[nodiscard]] virtual std::shared_ptr<const class BufferSnapshot>
        snapshot() const noexcept = 0;

    // 書き込み (呼び出しは CommandDispatcher 経由のみ)
    virtual void replace(TextRange range, std::u16string_view text) = 0;

    [[nodiscard]] virtual std::uint64_t length() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t lineCount() const noexcept = 0;
};

} // namespace
```

### 2.2 `ICommand`
```cpp
namespace neomifes::application {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute(class ExecutionContext&) = 0;
    virtual void undo(class ExecutionContext&) = 0;
    [[nodiscard]] virtual std::size_t weight() const noexcept = 0; // Undo圧縮用
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
};

} // namespace
```

### 2.3 `IPlugin`
```cpp
// C ABI 境界 (プラグイン SDK が include するヘッダ)
extern "C" {

struct NmfsPluginApiV1;  // 本体 → プラグインへ渡す関数テーブル
struct NmfsPluginInfo {
    const char* id;
    const char* version;
    uint32_t api_version;   // 期待する本体 API バージョン
};

// プラグインが実装すべき関数
__declspec(dllexport) const NmfsPluginInfo* nmfs_plugin_info(void);
__declspec(dllexport) int  nmfs_plugin_init(const NmfsPluginApiV1* api);
__declspec(dllexport) void nmfs_plugin_shutdown(void);

} // extern "C"
```

---

## 3. Document Engine 詳細

### 3.1 Piece Table 設計

```cpp
namespace neomifes::document {

// Piece = "どのバッファのどこからどれだけ" を指すエントリ
struct Piece {
    enum class Source : std::uint8_t { Original, Add };
    Source        source;
    std::uint64_t offset;     // ソース内 UTF-16 CU オフセット (Add / Original 両方で統一)
    std::uint64_t length;     // UTF-16 CU 数
    std::uint32_t newlineCnt; // このピース内の改行数 (LineIndex 用)
};

// Piece の並びは Red-Black Tree (順序統計木) で保持
// → O(log n) で任意オフセット/行番号にアクセス
class PieceTable {
public:
    void insert(TextPos pos, std::u16string_view text);
    void erase(TextRange range);
    [[nodiscard]] std::u16string extract(TextRange range) const;

    // スナップショット (RCU 風): ツリーの immutable コピーを共有
    [[nodiscard]] std::shared_ptr<const PieceTree> snapshot() const;

private:
    std::shared_ptr<const OriginalBuffer> m_original;  // メモリマップ元
    std::shared_ptr<AddBuffer>            m_addBuffer; // append-only
    std::atomic<std::shared_ptr<PieceTree>> m_tree;    // RCU 対象
    LineIndex                             m_lineIndex;
};

} // namespace
```

**性能要件**
- 挿入/削除: O(log n)、n = piece 数
- 位置 → 行番号: O(log n)
- 行番号 → 位置: O(log n)
- スナップショット取得: O(1) (共有ポインタ複製)

**巨大ファイル対応**
- 原本は `CreateFileMappingW` + `MapViewOfFileEx` でビュー化 (1GB ずつマップ、LRU で解放)
- Add Buffer は 64MB チャンクの deque。編集を append しかしないので断片化なし

**Lazy Decode (原本の非UTF-16保持)**

- 10GB ファイルを起動時に UTF-16 全変換するとメモリが 2 倍膨張し 20MB 目標に反する。原本は**生バイトのまま**保持し、UTF-16 化は「表示/検索/編集」の対象になったチャンク単位で行う。
```cpp
class OriginalBuffer {
public:
    // 原本の生バイトを返す (memory-mapped view)
    std::span<const std::byte> rawBytes(std::uint64_t byteOffset, std::size_t len) const;

    // 指定バイト範囲を UTF-16 にデコードして返す (キャッシュ済みなら参照返し)
    const std::u16string_view decodeUtf16(std::uint64_t byteOffset, std::size_t len,
                                          encoding::Encoding enc);

private:
    encoding::Encoding                     m_encoding;
    util::LRUCache<ChunkKey, std::u16string> m_decodeCache;  // 64KB × 上限 N チャンク
};
```
- **Piece.offset は Add / Original どちらのソースでも UTF-16 CU オフセット** で統一する (Phase 2a 実装済)。Original 側は `OriginalBuffer` の内部でバイトオフセット ↔ CU オフセットのマッピングを保持し、外から見える offset は必ず CU。これにより PieceTable / BufferSnapshot / LineIndex は source を意識せず一様なアドレス空間で動作する。
- **編集された範囲は Add Buffer に UTF-16 で入る**ため二度目以降のアクセスは高速。
- **改行インデックスの初期構築も生バイト上で行う** (改行文字 `\n` / `\r` は UTF-8/Shift-JIS/EUC-JP のいずれもマルチバイト先頭バイトと衝突しない ASCII なので安全)。
- UTF-16LE/BE 原本は 2 バイト単位で走査する専用パス。

### 3.2 LineIndex

```cpp
class LineIndex {
public:
    [[nodiscard]] std::uint64_t lineToOffset(std::uint64_t line) const;
    [[nodiscard]] std::uint64_t offsetToLine(std::uint64_t offset) const;
    void onPieceInserted(const Piece&);
    void onPieceErased(const Piece&);
private:
    // 各 Piece の改行数を集約する順序統計木 (Piece Tree と一体化)
};
```

- 遅延構築: ファイル全体をスキャンせず、可視領域先行 + バックグラウンド埋め
- 10GB ファイルでも初期表示は先頭 N ページのみで着色可能

### 3.3 FileLoader

```cpp
class FileLoader {
public:
    struct LoadOptions { bool memoryMap = true; std::size_t detectHeadBytes = 65536; };
    struct LoadResult  { std::unique_ptr<OriginalBuffer> buf; Encoding enc; LineEnding le; bool bom; };

    static neomifes::util::Result<LoadResult, IoError>
        load(const std::filesystem::path&, LoadOptions);
};
```

---

## 4. Rendering Engine 詳細

### 4.1 クラス構成

```cpp
namespace neomifes::render {

class RenderPipeline {
public:
    void attach(HWND);
    void resize(std::uint32_t w, std::uint32_t h, float dpi);
    void render(const class RenderFrame&);   // UI Thread から呼ぶ
    void invalidate(TextRange);
private:
    Microsoft::WRL::ComPtr<ID2D1Factory7>          m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory7>        m_dwFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain2>        m_swapChain;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext6>    m_dc;
    TextLayoutCache                                m_layoutCache;
    GlyphCache                                     m_glyphCache;
    DamageTracker                                  m_damage;
};

} // namespace
```

### 4.2 レンダリング戦略
1. **Frame 開始:** DamageTracker から更新矩形一覧を取得
2. **可視行決定:** Viewport + Document の LineIndex から表示行番号を算出
3. **DirectWrite Layout:** 未キャッシュ行のみ `IDWriteTextLayout` を生成し LRU に挿入
4. **描画:** ダーティ矩形にクリッピングして DrawGlyphRun / FillRectangle
5. **Present:** `DXGI_PRESENT_DO_NOT_SEQUENCE` で低遅延

### 4.3 パフォーマンス目安
- 1 行のレイアウト生成: < 50µs
- キャッシュヒット時の 1 行描画: < 5µs
- フルフレーム描画予算: 16.6ms / 60fps

---

## 5. Editor Core 詳細

### 5.1 Cursor / Selection
```cpp
struct Cursor {
    TextPos position;
    TextPos anchor;   // == position なら選択なし
    bool    isPrimary;
};

class SelectionModel {
public:
    void addCursor(TextPos);
    void moveAll(MovementKind, MovementUnit);
    void setRectangular(TextPos anchor, TextPos active); // 矩形選択
    [[nodiscard]] std::span<const Cursor> cursors() const noexcept;
private:
    std::vector<Cursor> m_cursors; // 常にソート & マージ済み
};
```

- **複数カーソル操作** は全カーソルに同一 Movement を適用し、重複マージ
- **矩形選択** は Cursor 集合として展開 (視覚は Rendering 側で描画)

### 5.1.1 縦編集 (列編集 / MIFES 由来)

矩形選択で選んだ範囲の**各行同一列位置に対して同時に**挿入/削除/上書きを行う操作。以下の Command を提供する。

```cpp
namespace neomifes::application {

// 矩形範囲の各行 column に対して text を挿入
class ColumnInsertCommand final : public ICommand {
public:
    ColumnInsertCommand(std::vector<TextPos> perLinePositions, std::u16string text);
    // execute: 位置の降順に挿入 (先行挿入で後続オフセットがズレるのを防ぐ)
    // undo:    位置の昇順に削除
};

// 矩形範囲の各行 [colStart, colEnd) を削除
class ColumnDeleteCommand final : public ICommand { ... };

// 矩形範囲を text で上書き (行末より短い行はパディングしない=行末で停止)
class ColumnOverwriteCommand final : public ICommand { ... };

// 矩形範囲の各行末尾に text を追記 (MIFES の縦編集の主用途)
class ColumnAppendCommand final : public ICommand { ... };

} // namespace
```

- 全行に一意な `TextPos` を持たせて `SelectionModel::setRectangular` の展開結果と同期する。
- 位置計算は Piece Table のスナップショット下で一括計算し、apply は 1 トランザクションでまとめて Document へ適用。

### 5.2 Viewport
```cpp
class Viewport {
public:
    void scrollTo(std::uint64_t topLine);
    void ensureVisible(TextPos);
    [[nodiscard]] LineRange visibleLines() const noexcept;
    void setFoldingRanges(std::span<const FoldRange>);
private:
    std::uint64_t   m_topLine;
    std::uint32_t   m_visibleLineCount;
    FoldingMap      m_folds;  // 論理行 → 表示行 逆写像
};
```

---

## 6. Command / Undo 詳細

### 6.1 Command 例
```cpp
class InsertTextCommand final : public application::ICommand {
public:
    InsertTextCommand(TextPos pos, std::u16string text) noexcept;
    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    std::size_t weight() const noexcept override { return m_text.size() * 2 + 32; }
    std::string_view id() const noexcept override { return "edit.insert"; }
    // 連続入力パッキング
    bool tryMerge(const InsertTextCommand& next) noexcept;
private:
    TextPos          m_pos;
    std::u16string   m_text;
};
```

### 6.1.1 標準 Command 一覧 (要件対応)

| Command ID | 概要 | 要件 |
|---|---|---|
| `edit.insert` / `edit.delete` / `edit.replace` | 基本編集 | §6 |
| `edit.autoIndent` | 現在行/選択範囲の自動インデント | §6 |
| `edit.formatDocument` | ドキュメント全体を整形 (LSP 経由 or ビルトイン) | §6 |
| `edit.tabsToSpaces` / `edit.spacesToTabs` | タブ⇔スペース変換 | §6 |
| `column.insert` / `column.delete` / `column.overwrite` / `column.append` | 縦編集 (列編集) | §6 |
| `file.changeEncoding` | 文字コード変更 (再デコード or 変換) | §6 |
| `file.changeLineEnding` | 改行コード変換 (CRLF/LF/CR) | §6 |
| `file.toggleBom` | BOM 有無切替 | §6 |
| `file.autoSave` (Timer 起動) | 5 秒間隔差分自動保存 | §15 |
| `file.backup` (保存前フック) | 直前バージョンを `<name>.bak` に退避 | §6 |
| `file.recent.open` | 最近開いたファイル (Recent) から開く | §6 |
| `search.find` / `search.findIncremental` | 通常/インクリメンタル検索 | §6 |
| `search.replaceAll` / `search.replaceInFiles` | 置換/複数置換 | §6 |
| `search.grep` | 複数フォルダ Grep | §6 |
| `nav.bookmark.toggle` / `nav.bookmark.next` | ブックマーク | §6 |
| `view.fold.toggle` / `view.outline.jump` | 折り畳み/アウトライン | §6 |
| `ai.invoke` | AI プラグイン呼出 (プラグイン ID 引数) | §7 |

### 6.1.2 バックアップ / Recent Files

- **バックアップ (`file.backup`)**
  - 上書き保存時、直前バージョンを同一ディレクトリの `<name>.bak` に退避 (既存 `.bak` は上書き)
  - 世代数は設定で 1〜10 の範囲。世代管理時は `<name>.bak.<n>` サフィックス
  - 大容量 (>100MB) ファイルは設定でスキップ可
- **Recent Files**
  - `SessionManager` が管理、保存先 `%APPDATA%\NeoMIFES\recent.json5`
  - 最大 100 件 (LRU)、ピン留め機能あり
  - パスは正規化 (`std::filesystem::weakly_canonical`) して重複排除

### 6.2 Undo Stack

```cpp
class UndoStack {
public:
    void push(std::unique_ptr<ICommand>);
    bool undo(ExecutionContext&);
    bool redo(ExecutionContext&);

private:
    struct Bucket {
        std::vector<std::unique_ptr<ICommand>> commands;
        std::size_t totalWeight = 0;
        bool        compressed  = false;   // zstd 圧縮済みか
        std::vector<std::byte> blob;       // 圧縮時のバイナリ
    };
    std::deque<Bucket> m_undo;
    std::deque<Bucket> m_redo;
    std::size_t        m_memBudget = 256 * 1024 * 1024; // 256MB (設定変更可)
};
```

- 1000 件ごとに Bucket 化 → 閾値超過で古い Bucket から zstd 圧縮
- メモリ予算超過時はディスクスワップ (`%LOCALAPPDATA%\NeoMIFES\undo\`)
- 100万件のカーソル移動でも weight 小さいためメモリ数 MB 程度

---

## 7. Search Engine 詳細

### 7.1 SearchService
```cpp
class SearchService {
public:
    struct Query {
        std::u16string pattern;
        bool caseSensitive = true;
        bool wholeWord     = false;
        bool regex         = false;
    };
    struct Match { TextRange range; };

    // 非同期 (Search Worker Pool)
    std::future<std::vector<Match>> findAll(const BufferSnapshot&, const Query&);

    // ストリーム (Grep 用): 部分結果を随時 push
    void grep(std::span<const std::filesystem::path>, const Query&,
              std::function<void(GrepHit)> onHit,
              std::stop_token);
};

// インクリメンタル検索: ユーザー入力に応じて逐次前方最短マッチを返す
class IncrementalFindService {
public:
    // 直前結果を基点に差分検索 (追加/削除ともに O(区間長))
    struct State { TextPos origin; TextPos current; std::u16string pattern; };
    Match findNext(BufferSnapshot&, State& state, char16_t appendedChar);
    Match findPrev(BufferSnapshot&, State& state);
    void  cancel(State& state); // origin へカーソル戻し
};

// 全置換 Command (Undo 可能)
class ReplaceAllCommand final : public application::ICommand {
public:
    ReplaceAllCommand(SearchService::Query, std::u16string replacement);
    // execute: findAll → 位置降順で置換適用 (オフセットずれ回避)
    // undo:    逆順に元テキストへ復元
};

// 複数ファイル置換 Command (ファイル単位でトランザクション化)
class ReplaceInFilesCommand final : public application::ICommand {
public:
    ReplaceInFilesCommand(std::vector<std::filesystem::path>,
                          SearchService::Query, std::u16string replacement);
    // 各ファイルに ReplaceAllCommand を適用し、履歴を統合
};
```

### 7.2 アルゴリズム
| 種別 | アルゴリズム |
|---|---|
| 通常 (case-sensitive, ASCII) | Boyer-Moore-Horspool + AVX2 pcmpeqb |
| 通常 (Unicode) | Two-way String Matching (需要に応じ SIMD) |
| 正規表現 | **RE2 採用予定** (ADR-002 で確定させる) |
| Grep (複数ファイル) | Worker Pool、ファイル単位 memory-map |

### 7.3 巨大ファイル検索
- Piece Table のチャンク単位で並列走査
- キャンセルは `std::stop_token`

---

## 8. Plugin Host 詳細

### 8.1 API v1 (概略)

```cpp
// C ABI: 本体 → プラグインへ渡す関数テーブル
struct NmfsPluginApiV1 {
    uint32_t abi_version;

    // Document 操作 (全て Command 経由)
    int   (*document_replace)(NmfsDocId, uint64_t start, uint64_t end,
                              const char16_t* text, size_t len);
    int   (*document_read)(NmfsDocId, uint64_t start, uint64_t end,
                           char16_t* out, size_t* inout_len);
    uint64_t (*document_length)(NmfsDocId);

    // Command 登録
    int (*command_register)(const char* id, NmfsCommandFn fn, void* user);

    // UI (ステータスバー / 通知 / パネル)
    void (*ui_notify)(const char16_t* msg, int level);

    // 設定
    int (*config_get)(const char* key, char* out, size_t* inout_len);

    // ロギング
    void (*log)(int level, const char* msg);
};
```

### 8.2 ホットロード
```cpp
class PluginHost {
public:
    void loadDir(const std::filesystem::path&);
    void reload(std::string_view id);
    void unload(std::string_view id);
private:
    struct LoadedPlugin {
        platform::HandleGuard<HMODULE, FreeLibraryDeleter> module;
        NmfsPluginInfo info;
        std::atomic<int> refCount;
    };
    std::unordered_map<std::string, LoadedPlugin> m_plugins;
};
```

- アンロード時: 参照カウント 0 のセーフポイントに達するまで待機
- 実行中のプラグイン Command は cancellation token で中断要求

### 8.3 AI Plugin
```cpp
// ai/claude_provider.cpp (別 DLL)
namespace neomifes::ai {

class ClaudeProvider {
public:
    struct Request  { std::u16string prompt; std::u16string context; };
    struct Response { std::u16string text; std::optional<Error> err; };

    std::future<Response> ask(Request);
private:
    HttpClient m_http;              // WinHTTP ラッパ
    ApiKeyStore m_keyStore;         // DPAPI + Credential Manager
};

} // namespace
```

---

## 9. Encoding Engine 詳細

### 9.1 判定アルゴリズム
```
1. BOM チェック (UTF-8 EF BB BF / UTF-16 FF FE / FE FF / UTF-32)
2. 全 ASCII (< 0x80) → UTF-8 として扱う
3. UTF-8 妥当性検査 (invalid byte が無ければ UTF-8 確定)
4. ISO-2022-JP エスケープシーケンス検出
5. Shift-JIS / EUC-JP 判定:
   - 2byte 領域の出現頻度スコアリング
   - 日本語 N-gram 辞書とのマッチ
6. 最終フォールバック: Shift-JIS
```

### 9.2 変換
- 内部表現は **UTF-16LE (`char16_t`)**
- 変換テーブルはコンパイル時定数配列 (constexpr)
- 不正シーケンスは `U+FFFD` に置換し、警告を Result で返す

---

## 10. Syntax Engine 詳細

### 10.1 増分解析
- 変更範囲を含む Region を最小単位で再解析
- 結果はカラー ID の run-length で保持し、Rendering に渡す
- 折り畳み範囲は解析結果から生成

### 10.2 文法定義
- TextMate 互換 (JSON/XML) → 独自 IR にコンパイル
- ホットリロード可能

---

## 11. ログ解析モード 詳細

### 11.1 アーキテクチャ
```
LogModeController
  ├─ TimestampParser  (SAP/Apache/JSON/W3C)
  ├─ LevelExtractor   (ERROR/WARN/INFO/DEBUG)
  ├─ FilterStack      (正規表現/レベル/時刻範囲)
  ├─ TimelineIndex    (時刻 → オフセット B+Tree)
  └─ ColorScheme
```

### 11.2 時系列ジャンプ
- ファイルロード時に時刻抽出を Worker で並列 (行単位 chunk)
- TimelineIndex に (timestamp, offset) を挿入
- UI から 2010-01-01 12:34:56 のように入力 → 最近傍検索 → ジャンプ

---

## 12. CSV モード 詳細

### 12.1 データ表現
- 論理: Piece Table + 行スキーマ
- 表示: 仮想スクロール表 (最大 1000 万行)
- 編集は Command 化 (CSVUpdateCellCommand)

### 12.2 列固定/フィルタ/ソート
- ソートは B+Tree のインデックスを作成 (原本は不変)
- フィルタは Bitset で行可視性を管理

---

## 13. JSON / XML モード

- パーサ: 自作 pull parser (依存無し) / 大規模は SIMDJSON 検討 (ADR)
- Tree View + テキストビューの同期
- XPath: 自作最小実装 or pugixml (ADR)
- JSONPath: goessner 仕様準拠

---

## 14. Git 統合

- **libgit2** 静的リンク (ADR で確定)
- Diff は 3-way merge を UI で表示
- Blame は行ごとに commit hash キャッシュ
- Commit / Branch は Command 化

---

## 15. マクロ

要件定義書 §12 に準拠し、以下を**標準同梱**する。

| 言語 | 同梱方式 | 用途 |
|---|---|---|
| **Lua 5.4** | 本体組込 (静的リンク、~200KB) | 軽量スクリプト、キー割当拡張 |
| **JavaScript (QuickJS)** | 本体組込 (~500KB) | Web エコシステム互換、非同期処理 |
| **Python 3.12+** | **標準プラグイン `python_macro.dll`**。CPython を Embed し、初回起動時に別プロセスで初期化 | 業務スクリプト・データ処理 |
| **キー操作記録 (独自マクロ)** | 本体組込 (Command 列の記録/再生) | 秀丸/MIFES 風マクロ記録 |

### 15.1 キー操作記録マクロ

```cpp
namespace neomifes::application {

class MacroRecorder {
public:
    void start();                       // 記録開始
    std::vector<CommandInvocation> stop();
    void replay(std::span<const CommandInvocation>);
    void saveNamed(std::string_view name, std::span<const CommandInvocation>);
    void invokeNamed(std::string_view name);
private:
    // CommandDispatcher にフックし、実行された Command をシリアライズ形で記録
    std::vector<CommandInvocation> m_recording;
    bool                           m_recordingActive = false;
};

} // namespace
```

- Command は id + 引数を JSON にシリアライズし `%APPDATA%\NeoMIFES\macros\<name>.json5` に保存
- キー割当は設定ファイルから `"ctrl+shift+p": "macro.invoke:mymacro"` の形で紐付け

### 15.2 スクリプト言語共通 API

Lua / JS / Python の全てに対して**共通の C ABI (プラグイン SDK)** を各言語 binding でラップして公開する。実装差異を最小化。

- Python プラグインは初回のみ CPython ランタイム起動コスト (~150ms) がかかるため、遅延ロード対象

---

## 16. スレッド安全性

| 対象 | 方針 |
|---|---|
| `Document` 書き込み | UI Thread のみ (Command 経由) |
| `Document` 読み取り | `BufferSnapshot` を共有ポインタで配布し任意スレッドから参照可能 |
| `RenderPipeline` | UI Thread のみ |
| `SearchService` | Worker Pool、結果はキュー経由 |
| `PluginContext` | プラグインごとにアフィニティ (別スレッドから呼ばない) |

- **不変オブジェクト + shared_ptr 配布** を軸にロックを最小化
- どうしても必要な排他は `std::shared_mutex`、細粒度化

---

## 17. エラーハンドリング

```cpp
namespace neomifes::util {
template<class T, class E> using Result = std::expected<T, E>;
}

enum class IoError { NotFound, PermissionDenied, InvalidEncoding, Cancelled, Unknown };
```

- **回復可能:** `Result<T, E>` で返す
- **プログラマエラー:** `assert` + Debug でクラッシュ
- **回復不能な実行時異常:** 構造化例外 → 最外郭で捕捉 → クラッシュダンプ生成 → 自動保存復元

---

## 18. テスト設計

### 18.1 単体テスト (GoogleTest)
- Document: 挿入/削除/スナップショット/巨大ケース (1GB モック)
- LineIndex: エッジケース (0行/末尾改行/CRLF)
- Command/Undo: 100万件走行
- Encoding: 全対象エンコードの往復
- Search: BMH / RE2 の同期・非同期
- Piece Table Fuzz: ランダム操作 vs `std::u16string` の等価性検証

### 18.2 統合テスト
- 起動時間計測 (0.3s しきい値)
- メモリ計測 (Working Set 20MB)
- 10GB ファイル読込 → スクロール → 保存
- プラグインロード/アンロード ソーク (24h)

### 18.3 ベンチマーク (google/benchmark)
| Bench | 目標 |
|---|---|
| PieceTable::insert (small edit) | ≤ 500ns |
| PieceTable::snapshot | ≤ 100ns |
| Render frame (100 行) | ≤ 3ms |
| Search (1GB, plain) | ≥ 500MB/s |
| Undo/Redo (100k ops) | ≤ 50ms |

---

## 19. ビルド & CI 詳細

### 19.1 ビルド
```
CMake >= 3.28
MSVC v143 (VS2022)
/std:c++latest /W4 /permissive- /Zc:__cplusplus /EHsc /GR-
Debug: /fsanitize=address /Zi /Od
Release: /O2 /Ob3 /GL /LTCG /GS-
```
- `GR-` は RTTI 無効。プラグイン境界は C ABI なので影響なし。

### 19.2 CI (GitHub Actions)
- ジョブ: `build-msvc-debug`, `build-msvc-release`, `unit-tests`, `bench-guard`, `static-analysis`
- `bench-guard` は主要ベンチが 5% 以上退化したら fail

---

## 20. API バージョニング

- Plugin API: セマンティックバージョニング。破壊的変更で **メジャー+1**
- 本体は複数メジャーの Plugin API を同時サポート (旧 API はアダプタ層で吸収)

---

## 21. セキュリティ

- プラグイン DLL 署名検証 (オプション、Enterprise 向け)
- AI API キーは DPAPI で暗号化して Credential Manager に格納
- ネットワークアクセスは AI プラグインに限定 (本体コアからは行わない)
- クラッシュダンプはユーザー同意後にのみアップロード (Phase 12 で検討)

---

## 22. 実装ポリシー補足

### 22.1 文字型境界ヘルパ

内部は `char16_t` (UTF-16LE) で統一。Win32 API は `wchar_t` (LPCWSTR, LPWSTR) を要求するため、境界に**明示的な変換ヘルパ**を置く (`reinterpret_cast` を局所化)。

```cpp
namespace neomifes::util {

// ビット等価性を前提とした無コスト変換 (Windows は wchar_t == char16_t == 16bit)
// static_assert(sizeof(wchar_t) == sizeof(char16_t));

[[nodiscard]] inline const wchar_t* toWchar(const char16_t* s) noexcept {
    return reinterpret_cast<const wchar_t*>(s);
}
[[nodiscard]] inline wchar_t* toWchar(char16_t* s) noexcept {
    return reinterpret_cast<wchar_t*>(s);
}
[[nodiscard]] inline const char16_t* fromWchar(const wchar_t* s) noexcept {
    return reinterpret_cast<const char16_t*>(s);
}
[[nodiscard]] inline std::wstring_view toWstringView(std::u16string_view v) noexcept {
    return { toWchar(v.data()), v.size() };
}

} // namespace neomifes::util
```

- Win32 呼び出しはこのヘルパ経由に限定。プロジェクト全体で `reinterpret_cast<wchar_t*>` を grep で 0 件 (ヘルパ内除く) にする lint ルールを設ける。

### 22.2 `dynamic_cast` 禁止

`/GR-` (RTTI 無効) ビルドのため `dynamic_cast` は使用不可。多態的な型判別が必要な場合:

- 仮想メソッドで振舞いを解決 (第一選択)
- `std::variant` + `std::visit` (閉じた集合)
- 基底クラスに `enum class Kind` を持たせ `static_cast` (open-closed 違反にならない範囲で)

### 22.3 決定済みの技術選定 (旧未決事項)

| 項目 | 決定 | 参照 ADR |
|---|---|---|
| 内部文字型 | `char16_t` / `std::u16string` | ADR-006 (要作成) |
| HTTP クライアント | **WinHTTP** (依存最小、要件「外部ライブラリ最小限」に合致) | ADR-004 |
| ビルドシステム | CMake + MSVC v143 + Ninja | ADR-001 |
| 正規表現 | RE2 | ADR-002 |
| シンタックス | TextMate 互換 (tree-sitter は将来検討) | ADR-003 |
| 最低 VS | VS 17.13+ | ADR-005 |
| 設定ファイル | JSON5 | (basic §6.1) |
| マクロ言語同梱 | Lua + JS(QuickJS) + Python(標準プラグイン) + キー記録 | (§15) |

### 22.4 残る未決事項

- libgit2 のライセンス運用 (GPLv2 with GCC linking exception) → Phase 11 前に法務確認
- LSP クライアント自作 vs 既存 → Phase 11 で比較評価
- tree-sitter 導入時期 → Phase 7 完了後に評価

---

## 23. Definition of Done (詳細設計)

- [x] 主要データ構造 (Piece Table / LineIndex / UndoStack) のインターフェースと計算量が明記
- [x] レンダリングパイプラインのフレーム予算が具体化
- [x] スレッド安全性の方針がモジュール単位で明記
- [x] プラグイン C ABI の骨子が定義
- [x] エラーハンドリング方針が統一
- [x] テスト・ベンチマークの目標値が数値で提示
- [x] 未決事項が Issue/ADR 起票対象として明示
