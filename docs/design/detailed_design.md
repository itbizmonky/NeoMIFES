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

**Phase 4a (2026-07-16) で実装確定:** 名前空間は Phase 0 時点の `neomifes::application` ではなく `neomifes::core` (CLAUDE.md §5 の `neomifes::<layer>` 命名規則、Editor Core レイヤーに対応)。`ExecutionContext` は `Document&`+`SelectionModel&` を保持する新規グルークラスとして `src/core/include/neomifes/core/command.h` に実装 (詳細は §6.1 参照)。

**Phase 4b1 (2026-07-17) で `cursorPositionAfterExecute()`/`cursorPositionAfterUndo()` を追加、Phase 4b5a (同日) で `cursorsAfterExecute()`/`cursorsAfterUndo()` に一般化:** `ExecutionContext` が保持していた `SelectionModel&` は Phase 4a 時点では未使用だった (実装後レビューで指摘済み)。Phase 4b1 で単一 `TextPos` を返す2メソッドを追加し `SelectionModel::moveAllTo()` で復元する形にしたが、これは全カーソルを1点に強制収束させることしかできず複数カーソル編集(Phase 4b5)を表現できないと判明。`std::vector<Cursor>` を返す形に置き換え、`CommandDispatcher::dispatch()`/`UndoStack::undo()`/`redo()` は `SelectionModel::setCursors()` を呼ぶ (詳細は §6.1/§6.2)。
```cpp
namespace neomifes::core {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute(class ExecutionContext&) = 0;
    virtual void undo(class ExecutionContext&) = 0;
    [[nodiscard]] virtual std::size_t weight() const noexcept = 0; // Undo圧縮用
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    // Phase 4b5a: 実行後/取り消し後にSelectionModelが持つべきカーソル集合全体。
    // 単一カーソル系コマンドは要素数1のvectorを返す。
    [[nodiscard]] virtual std::vector<Cursor> cursorsAfterExecute() const = 0;
    [[nodiscard]] virtual std::vector<Cursor> cursorsAfterUndo() const = 0;
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

> **実装確定 (ADR-007、Phase 2b2/2b3 で実装済み):** 当初検討していた Path-Copying Persistent RB-Tree ([ADR-006](../decisions/ADR-006-piece-tree-implementation.md)、**Superseded**) は実装コストと性能リスクの観点から採用を見送り、**Mutable Red-Black Tree + 都度コピーの Piece-Vector Snapshot** ([ADR-007](../decisions/ADR-007-piece-tree-mutable-rb.md)) を採用した。以下は実測値を伴う実装済みアーキテクチャを反映している (旧 RCU/persistent tree 案のコード例ではない)。

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

// Piece の並びは Mutable Red-Black Tree (順序統計木、CLRS 13.3/13.4 準拠) で保持。
// ノードは std::unique_ptr で親から所有 (PieceTable が排他)。詳細は piece_tree.h。
class PieceTree { /* insert/erase O(log n)。subtreeLength/subtreeNewlines/subtreeCount を集約保持 */ };

class PieceTable {
public:
    void insert(TextPos pos, std::u16string_view text);
    void erase(TextRange range);
    [[nodiscard]] std::u16string extract(TextRange range) const;

    // スナップショット: tree を in-order 走査して std::vector<Piece> にコピーし
    // BufferSnapshot でラップして返す。O(n pieces) — O(1) ではない (下記参照)
    [[nodiscard]] std::shared_ptr<const BufferSnapshot> snapshot() const;

private:
    std::shared_ptr<const OriginalBuffer> m_original;  // メモリマップ元 (読み取り専用)
    std::shared_ptr<AddBuffer>            m_add;       // append-only チャンク列 (snapshot と共有)
    PieceTree                             m_tree;      // mutable、PieceTable が排他所有
};

} // namespace
```

**性能要件 (実測値、2026-07-15 時点。詳細は [`piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md))**
- 挿入: O(log n)、CI実測 243〜276ns (Release) — 目標 <500ns を達成
- 削除: O(log n + k)、k = 削除対象ピース数
- 位置 → 行番号 / 行番号 → 位置: **O(N) 再構築 + O(log n) 二分探索**。tree 集約からの O(log n) 直接算出は原理的に不可能と判明 (理由は §3.2 参照)
- スナップショット取得: **O(n pieces)**、tree の in-order 走査 + vector コピー。目標 ≤1ms @ 100K piece に対し実測 1.2〜1.5ms (約20〜48%超過、低優先度の残タスクとして受容)。**snapshot は毎フレーム/毎キー入力で呼ぶ想定ではなく**、LineIndex 再構築・検索・自動保存等の低頻度呼び出しでの利用を前提とする設計 — **§4 Rendering Engine はフレームごとに snapshot() を呼ばず、Document からの変更通知を受けたときだけ再取得してキャッシュすること** (この前提を崩すと 100K piece 規模のドキュメントで 1 フレームあたり ~1.2ms をコピーだけで消費し、16.6ms 予算の約7%を奪う)

**巨大ファイル対応**
- 原本は `CreateFileW` + `CreateFileMappingW` + `MapViewOfFile` で **ファイル全体を単一ビューとしてマップ** (x64 の仮想アドレス空間は10GB級ファイルでも十分足りるため、1GBずつの LRU 分割マップは過剰設計と判断し不採用)
- Add Buffer は **128KiB チャンク** の `deque`。編集を append しかしないので断片化なし、pointer stability も保証 (snapshot 後も既存の view が無効化されない)

**Lazy Decode (原本の非UTF-16保持)**

- 10GB ファイルを起動時に UTF-16 全変換するとメモリが 2 倍膨張し 20MB 目標に反する。原本は**生バイトのまま**保持し、UTF-16 化は「表示/検索/編集」の対象になった範囲が実際に要求された時点で行う。
```cpp
class OriginalBuffer {
public:
    // [offset, offset+length) の UTF-16 ビューを返す。MemoryMapped モードでは
    // 初回アクセス時にデコードしてキャッシュし (以降は同一範囲の再デコードなし)、
    // 追い出しは行わない (理由は lazy_decode_mmap.md 参照 — u16string_view を返す
    // 現行APIでは、追い出すと既存 view が dangling になるため)
    [[nodiscard]] std::u16string_view view(std::uint64_t offset, std::uint64_t length) const;

    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] std::uint32_t newlineCount() const noexcept;  // 初回スキャンで事前計算済み、O(1)

private:
    platform::FileMapping   m_mapping;      // ファイル全体を単一ビューでマップ
    std::vector<Checkpoint> m_checkpoints;  // 64KiBごとの (バイトオフセット, CUオフセット) 対
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::unique_ptr<std::u16string>>
                             m_decodeCache; // (offset,length) キー、追い出しなし
};
```
- **Piece.offset は Add / Original どちらのソースでも UTF-16 CU オフセット** で統一する。Original 側は `OriginalBuffer` の内部でバイトオフセット ↔ CU オフセットのマッピング (チェックポイント索引) を保持し、外から見える offset は必ず CU。これにより PieceTable / BufferSnapshot / LineIndex は source を意識せず一様なアドレス空間で動作する
- **チェックポイントは常に完全なコードポイント境界にのみ記録される** — マルチバイト UTF-8 文字がチャンク境界で分断されることは、単なる注意ではなくアルゴリズムの不変条件として構造的に起こり得ない
- **改行数は初回のバイトレベルストリームスキャンで事前計算**され `newlineCount()` として O(1) で取得可能。`PieceTable` のコンストラクタはこれを使うため、ファイルを開いただけでは UTF-16 デコードが一切走らない (laziness の核)
- **現状 UTF-8 (BOM可) 専用**。UTF-16LE/BE・Shift-JIS 等への対応は Phase 6 (Encoding Engine) で拡張予定 — それまでは `encoding::Encoding` パラメータは存在しない
- ネットワークドライブ切断等による `EXCEPTION_IN_PAGE_ERROR` は SEH (`__try`/`__except`) で捕捉し `IoFailure` に変換する (Phase 2b3 Step 2 で実装済み。MSVC の「`__try` を含む関数はオブジェクトアンワインドを持てない」制約のため、リスクのある呼び出しはプリミティブ型ローカルのみを持つ小さなトランポリン関数に隔離している)

### 3.2 LineIndex

```cpp
class LineIndex {
public:
    [[nodiscard]] std::uint64_t lineToOffset(std::uint64_t line) const;
    [[nodiscard]] std::uint64_t offsetToLine(std::uint64_t offset) const;
    void rebuild(const BufferSnapshot&);  // O(N) 全再構築、次回クエリ時に遅延実行
private:
    std::vector<std::uint64_t> m_lineStartOffsets;  // 二分探索対象
};
```

> **設計変更 (Phase 2b2 Step 2 で判明、撤回済み):** 当初は「Piece Tree の順序統計集約と一体化し O(log n) 化する」計画だったが、**原理的に不可能**と判明した。集約が保持するのは piece 内の改行**総数**のみで、任意オフセット以前の改行数を答えるには piece 内の改行の**実際の位置**が必要 — これは tree が持たないテキスト内容 (buffer) を見なければ分からない。詳細と将来案は [`line_index_o_log_n.md`](../issues/line_index_o_log_n.md)。
>
> 代わりに Phase 2a 以来の設計 (Document 変更後、次回クエリ時に遅延で O(N) 全再構築 → 以後は O(log n) 二分探索) を維持する。10GB 級ファイルでは全行の offset 一覧 (8byte/行) を保持するため、行数が極端に多いファイルではメモリ量に注意 — 将来ボトルネックが実測されたら per-piece newline-offset 配列方式等を再評価する

### 3.3 FileLoader

```cpp
enum class LoadError { NotFound, PermissionDenied, IoFailure, InvalidUtf8, TooLarge, Unknown };

struct LoadResult {
    std::unique_ptr<Document> document;
    bool                      hadBom     = false;
    std::uint64_t             byteLength = 0;
};

// UTF-8 (BOM可) ファイルを読み込み Document を構築する。同期 API。
// maxBytes はデフォルト 512MiB (誤ってバイナリを開いてもメモリを食い潰さないための上限)
[[nodiscard]] std::variant<LoadResult, LoadError>
    loadUtf8File(const std::filesystem::path& path, std::uint64_t maxBytes = 512ULL * 1024 * 1024);
```

- ファイル本体は `OriginalBuffer::openMemoryMapped` (mmap) 経由で扱う。`FileLoader` 自身は BOM 検出のための先頭3バイトだけを個別に `_wfopen_s`/`fread` で読む (mmap 全体を作ってから3バイトだけ見るより単純)
- 非同期化 (Worker 経由) は将来検討。現状は同期 API のみ

---

## 4. Rendering Engine 詳細

### 4.1 クラス構成 (Phase 3c 実装済み)

> **現状 (Phase 3c 完了時):** D3D11/D2D/DXGI デバイスグラフの RAII 所有 + DirectWrite でアタッチした `Document` の可視行を実描画 + 行キャッシュ (`TextLayoutCache`) + 粗粒度フレームスキップ。独自グリフアトラス (GlyphCache) と細粒度 DamageTracker は明示的に延期 (ADR-011)。
> **設計判断:** [ADR-008](../decisions/ADR-008-com-raii-comptr.md) COM RAII に `Microsoft::WRL::ComPtr` を採用 / [ADR-009](../decisions/ADR-009-deferred-device-init.md) デバイス生成は同期・UIスレッド・自己ポストメッセージ (`WM_APP`) 方式 / [ADR-010](../decisions/ADR-010-render-depends-on-document.md) Rendering Engine は Document Engine に直接依存する / [ADR-011](../decisions/ADR-011-phase3c-render-cache-scope.md) Phase 3c は TextLayoutCache のみを実装し GlyphCache・細粒度 DamageTracker を延期する

```cpp
namespace neomifes::render {

// プロセス単位シングルトン (d2d_factories.h) — magic-static で遅延初期化
RenderExpected<Microsoft::WRL::ComPtr<ID2D1Factory7>>   sharedD2DFactory() noexcept;
RenderExpected<Microsoft::WRL::ComPtr<IDWriteFactory7>> sharedDWriteFactory() noexcept;

// 1 HWND 分の D3D11+D2D+DXGI デバイスグラフの RAII 所有者
class RenderDevice {
public:
    [[nodiscard]] static RenderExpected<RenderDevice> create(HWND, std::uint32_t w, std::uint32_t h) noexcept;
    [[nodiscard]] static RenderExpected<RenderDevice> createHeadless() noexcept; // テスト用
    [[nodiscard]] RenderExpected<void> resize(std::uint32_t w, std::uint32_t h) noexcept;
    void setDpi(float dpiScale) noexcept; // SetDpi は失敗しないため戻り値なし
    // beginFrame()/endFrame() ペアで DC を貸し出す (Phase 3a の clearAndPresent()
    // を置き換え)。返るポインタは対応する endFrame() までのみ有効
    [[nodiscard]] RenderExpected<ID2D1DeviceContext6*> beginFrame() noexcept;
    [[nodiscard]] RenderExpected<void> endFrame() noexcept;
private:
    Microsoft::WRL::ComPtr<ID3D11Device>        m_d3dDevice;   // HARDWARE→WARP フォールバック
    Microsoft::WRL::ComPtr<IDXGISwapChain2>     m_swapChain;   // FLIP_DISCARD, 2 buffers
    Microsoft::WRL::ComPtr<ID2D1Device6>        m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext6> m_dc;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1>        m_targetBitmap;
    bool                                         m_frameOpen = false; // beginFrame/endFrame 誤用ガード
};

// 行番号キーの IDWriteTextLayout キャッシュ (Phase 3c、ADR-011)。
// デバイスロストとは無関係 (recreateDeviceではクリアしない)。
// 無効化は Document::version() 変化時の wholesale clear() のみ
struct TextLayoutCacheStats { std::uint64_t hits = 0, misses = 0; };
class TextLayoutCache {
public:
    [[nodiscard]] RenderExpected<IDWriteTextLayout*> getOrCreate(
        document::LineNumber line, std::u16string_view lineText,
        IDWriteFactory7& dwriteFactory, IDWriteTextFormat& textFormat,
        float maxWidthDips, float maxHeightDips) noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] TextLayoutCacheStats stats() const noexcept;
    void resetStats() noexcept;
private:
    std::unordered_map<document::LineNumber, Microsoft::WRL::ComPtr<IDWriteTextLayout>> m_entries;
    TextLayoutCacheStats m_stats;
};

// MainWindow / app が実際に触るファサード
class RenderPipeline {
public:
    [[nodiscard]] RenderExpected<void> attach(HWND hwnd) noexcept;
    [[nodiscard]] RenderExpected<void> resize(std::uint32_t w, std::uint32_t h, float dpiScale) noexcept;
    [[nodiscard]] RenderExpected<void> render() noexcept;
    [[nodiscard]] bool isAttached() const noexcept;

    // 非所有。呼び出し側が Document の生存期間を保証する (ADR-010)
    void setDocument(const document::Document* doc) noexcept;
    // Phase 4 で Viewport に置換されるまでの暫定フック (対話的スクロール未実装、
    // --measure-frame ハーネスが唯一の実呼び出し元)
    void setTopLine(document::LineNumber line) noexcept;
    [[nodiscard]] document::LineNumber topLine() const noexcept;
    // --measure-frame と統合テストが観測するキャッシュ統計 (Phase 3c)
    [[nodiscard]] TextLayoutCacheStats layoutCacheStats() const noexcept;
private:
    // 粗粒度フレームスキップ (Phase 3c の DamageTracker 代替、ADR-011)。
    // 前回成功フレームと完全一致なら beginFrame/Clear/draw/endFrame を丸ごとスキップ
    struct FrameState {
        bool hasDocument = false;
        std::uint64_t documentVersion = 0;
        document::LineNumber topLine = 0;
        std::uint32_t width = 0, height = 0;
        float dpiScale = 0.0F;
        friend bool operator==(const FrameState&, const FrameState&) = default;
    };
    [[nodiscard]] FrameState captureFrameState() const noexcept;

    [[nodiscard]] RenderExpected<void> recreateDevice() noexcept; // デバイスロスト時
    [[nodiscard]] RenderExpected<void> refreshDocumentCacheIfStale() noexcept; // §4.3 ガードレールの実装本体、layoutCache も clear
    [[nodiscard]] RenderExpected<void> ensureTextFormat() noexcept;   // IDWriteTextFormat 遅延生成 + 行高さ計測
    [[nodiscard]] RenderExpected<void> ensureTextBrush(ID2D1DeviceContext6&) noexcept;
    [[nodiscard]] RenderExpected<void> renderOnce() noexcept;
    void drawVisibleLines(ID2D1DeviceContext6&) noexcept; // layoutCache.getOrCreate() + DrawTextLayout

    HWND                        m_hwnd = nullptr;
    std::uint32_t               m_width = 0, m_height = 0;
    float                       m_dpiScale = 1.0F;
    std::optional<RenderDevice> m_device; // 有効 or 空の二択

    const document::Document*                       m_document = nullptr;
    bool                                             m_hasCachedSnapshot = false;
    std::uint64_t                                    m_cachedDocumentVersion = 0;
    std::shared_ptr<const document::BufferSnapshot>  m_cachedSnapshot;
    document::LineNumber                             m_topLine = 0;

    Microsoft::WRL::ComPtr<IDWriteFactory7>      m_dwriteFactory; // DPI非依存、デバイスロストを跨いで生存
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBrush;    // デバイスコンテキスト依存、recreateDeviceでReset
    float                                         m_lineHeightDips = 0.0F;

    TextLayoutCache            m_layoutCache;             // recreateDeviceではクリアしない
    std::optional<FrameState>  m_lastRenderedFrameState;  // nullopt = 次のrender()は必ず描画
};

// エラー型 — プロジェクト初の std::expected 実用箇所
struct RenderError { RenderStage stage; HRESULT hr; [[nodiscard]] bool isDeviceLost() const noexcept; };
template <typename T> using RenderExpected = std::expected<T, RenderError>;

} // namespace
```

**デバイスロスト処理:** `RenderPipeline::render()` が `isDeviceLost()` を検知したら `RenderDevice` を丸ごと破棄・再生成 (1 回だけリトライ)。MS の推奨通り「スワップチェーンだけ」ではなく「デバイスグラフ全体」を作り直す。`recreateDevice()` は再生成後に `setDpi()` を再適用し、`m_textBrush` (デバイスコンテキスト依存) を破棄する。

**MainWindow との統合:** `neomifes_ui` は `neomifes_render` をリンクしない。合成は `src/app/main.cpp` (両方をリンクする構成ルート) で行い、レイヤ分離を保つ。`MainWindowConfig::onDeferredInit` で `RenderPipeline::attach()` → 成功時に `setDocument()` と `setPaintHandler()` で D2D 描画を差し込む。`--measure-startup`/`--measure-memory` モードでは `RenderPipeline` を一切配線しない (計測契約を構造的に保護)。

**Rendering Engine → Document Engine の依存 (ADR-010):** `neomifes_render` は `neomifes_document` に `PUBLIC` 依存する。CLAUDE.md §3 のレイヤ図は上位→下位の依存を示しており、Rendering Engine は Document Engine より上位に描かれているため、この依存方向はレイヤ規約上正しい (document 側に render への参照は無く、循環は発生しない)。

### 4.2 レンダリング戦略

**Phase 3a (実装済み):** クリア色で全画面を塗り潰して `Present1(1, 0, ...)` (v-sync interval 1)。

**Phase 3b (実装済み):**
1. **可視行決定:** `topLine` + `computeVisibleLineCount()` (`viewport_math.h`、クライアント高さ/DPI/行高さから算出) で表示行範囲を決定
2. **DirectWrite 基盤:** `IDWriteTextFormat` を遅延生成 (`Consolas` 14pt、`DWRITE_WORD_WRAPPING_NO_WRAP` — 固定行送りレイアウトが折り返しで崩れないよう明示指定)
3. **描画:** 可視範囲全体を1回の `BufferSnapshot::extract()` で取得し (行ごとの `extract()` 呼び出しによる O(pieces) 累積コストを回避)、`\n` で分割して行ごとに描画

**Phase 3c (実装済み):**
1. **DirectWrite Layout キャッシュ:** `TextLayoutCache` が行番号キーで `IDWriteTextLayout` をキャッシュ。`drawVisibleLines()` は `getOrCreate()` → `dc.DrawTextLayout()` (Phase 3b の `dc.DrawText()` 直呼びを置換)。無効化は `Document::version()` 変化時の wholesale `clear()` のみ (§4.4 参照)
2. **粗粒度フレームスキップ:** `RenderPipeline::render()` が `FrameState` (Document version/topLine/width/height/dpiScale) を前回成功フレームと比較し、完全一致なら `beginFrame`/`Clear`/`drawVisibleLines`/`endFrame` を丸ごとスキップ
3. **Present:** 引き続き全画面 `Present1(1, 0, ...)`。部分更新・`DXGI_PRESENT_DO_NOT_SEQUENCE` は細粒度 DamageTracker (延期、ADR-011) と共に将来検討

**Phase 3c でスコープ外とした残タスク (ADR-011、再評価トリガー付き):**
- **GlyphCache:** 独自グリフラン/アトラスラスタライズ。TextLayoutCache 単体の実測 (§4.3) が目標を大幅にクリアしているため現時点では不要と判断。再評価トリガー: ベンチ/`--measure-frame` 実測でフレーム予算割れが判明した場合
- **細粒度 DamageTracker:** 部分矩形 dirty-rect 追跡。対話的編集 (Phase 4) が無いため実際のユースケースが存在しない。再評価トリガー: Phase 4 で対話的な1行単位編集が実現した場合

### 4.3 パフォーマンス目安・実測値

- 1 行のレイアウト生成 (TextLayoutCache miss): 目標 < 50µs、**実測 532ns** (`tests/bench/render_text_layout_cache_bench.cpp` `BM_TextLayoutCache_Miss`, Release, CI, 約94倍のマージン)
- キャッシュヒット時の 1 行描画準備 (TextLayoutCache hit): 目標 < 5µs、**実測 4.34ns** (`BM_TextLayoutCache_Hit`, Release, CI, 約1152倍のマージン)
- フルフレーム描画予算: 16.6ms / 60fps。**実測 (`--measure-frame`, 50,000行合成ドキュメント、300フレーム連続スクロール, Release):** avg 5.52ms / p50 5.56ms / p95 5.66ms / max 8.11ms / min 0.25ms — 全フレームが予算内 (vsync 同期のため avg は概ね1フレーム分の描画+Presentコストを反映、CPU側のキャッシュ効果はマイクロベンチ側で分離測定)
- **`PieceTable::snapshot()` はフレームごとに呼ばない。** 実測 O(n pieces) (100K piece規模で1.2〜1.5ms、§3.1参照) のため、毎フレーム呼ぶと大きめのドキュメントで frame budget の約7%をコピーだけで消費する。**実装済み (Phase 3b):** `RenderPipeline::refreshDocumentCacheIfStale()` が `Document::version()` を前回キャッシュ時の値と比較し、変化時のみ `snapshot()` を呼ぶ (ADR-010)。これがこの層で `snapshot()` を呼ぶ唯一の箇所

### 4.4 Phase 3b/3c 設計課題 (解決済み)

Phase 3b 着手前に洗い出した4件のアーキテクチャ課題、および Phase 3c で追加検討した2件は全て解決済み (未着手ではなく「解決 = 実装 or 明示的延期」):

1. **RenderDevice の DC アクセスパターン:** `beginFrame()`/`endFrame()` ペアに分解して解決 (§4.1 参照、`clearAndPresent()` は廃止)
2. **Document → Render の変更通知チャネル:** `Document::version()` + `RenderPipeline::refreshDocumentCacheIfStale()` で解決 (ADR-010、§4.3 参照)。Phase 3c でこの同じ通知を `TextLayoutCache::clear()` のトリガーとしても再利用
3. **スクロール位置管理:** `RenderPipeline::m_topLine` (Phase 4 で `Viewport` に置換予定、`--measure-frame` ハーネスが現状唯一の実呼び出し元) + `computeVisibleLineCount()` (`viewport_math.h`) で解決
4. **DPI 対応テキストレイアウト:** `RenderPipeline::m_dpiScale` を保持し `RenderDevice::setDpi()` に転送。フォントサイズは DIP 固定 (`SetDpi` が自動スケーリング) のため DPI 依存のフォントサイズ計算は不要と判明
5. **(Phase 3c) TextLayoutCache のキャッシュキー・無効化粒度:** 行番号キー + `Document::version()` 変化時の wholesale `clear()` で解決 (ADR-011)。内容ハッシュキーや細粒度無効化は、対応する変更範囲情報のソースが存在しないため見送り
6. **(Phase 3c) フレームスキップの安全性:** `RenderDevice` の `DXGI_SWAP_EFFECT_FLIP_DISCARD` + DWM 合成下では前回 Present 内容がコンポジタ側に保持されるため、`WM_PAINT` の都度描画は必須でない。`MainWindow::handlePaint()` が無条件に `::ValidateRect()` を呼ぶため、描画スキップが `WM_PAINT` 再発行ループを招くこともない (ADR-011)

---

## 5. Editor Core 詳細

**Phase 4a (2026-07-16) で実装確定 (ADR-012)。** 本節は Phase 0 時点のスケッチから、`src/core/` の実装内容に同期済み。§5.1.1 (縦編集)・§5.2 の `FoldingMap` は ADR-012 により Phase 4b 以降へ明示的に延期 (削除はしない)。**Phase 4b1 (2026-07-17) でキーボード入力配線 (`src/app/editor_input.h/.cpp`)・キャレット描画・マウスホイールスクロールを実装、Phase 4b2 (同日) でマウスクリック位置特定・選択範囲ハイライト描画、Phase 4b3 (同日) でドラッグ選択、Phase 4b4 (同日) でダブルクリック単語選択・トリプルクリック行選択、Phase 4b5a/4b5b (同日) で複数カーソル編集コマンド基盤 + Alt+クリック複数カーソル追加、Phase 4b6a〜4b6d (同日) で PageUp/PageDown・Ctrl+矢印単語移動・クリップボードコピー・Alt+Shift+クリック/Alt+ドラッグ選択拡張を実装**(詳細は本節末尾および §6 参照)。

### 5.1 Cursor / Selection

`src/core/include/neomifes/core/cursor.h` / `selection_model.h` / `selection_model.cpp` に実装。位置表現は Document Engine に line/column 型が存在しないため `document::TextPos` (フラット UTF-16 オフセット) のまま — Phase 0 スケッチの想定通り、新しい位置型は導入していない。

```cpp
namespace neomifes::core {

struct Cursor {
    document::TextPos position  = 0;
    document::TextPos anchor    = 0;  // == position なら選択なし
    bool               isPrimary = false;
    [[nodiscard]] constexpr bool hasSelection() const noexcept { return position != anchor; }
};

enum class MovementKind : std::uint8_t {
    Left, Right, Up, Down, LineStart, LineEnd, DocumentStart, DocumentEnd,
    PageUp, PageDown,    // Phase 4b6a
    WordLeft, WordRight, // Phase 4b6b
};

class SelectionModel {
public:
    explicit SelectionModel(document::TextPos initialPosition = 0);
    void addCursor(document::TextPos position);
    // Phase 4b6a: pageSize (デフォルト0) は PageUp/PageDown でのみ参照される。
    void moveAll(MovementKind kind, const document::Document& doc, bool extendSelection,
                document::LineNumber pageSize = 0);
    void collapseToPrimary();
    // Phase 4b1: 編集/Undo/Redo後の絶対位置ジャンプ用。Phase 4b2で extendSelection
    // (デフォルトfalse) を追加、Shift+クリック時のanchor保持に対応。
    void moveAllTo(document::TextPos position, bool extendSelection = false);
    // Phase 4b4: 簡易文字種ベースの単語境界/行全体を選択 (詳細は §5.3)。
    void selectWordAt(document::TextPos pos, const document::Document& doc);
    void selectLineAt(document::TextPos pos, const document::Document& doc);
    // Phase 4b5a: カーソル集合全体を差し替え (ICommand::cursorsAfterExecute()/
    // AfterUndo() から CommandDispatcher/UndoStack が呼ぶ、§6.1参照)。
    void setCursors(std::vector<Cursor> cursors);
    // Phase 4b6d: anchorがidentifyingAnchorと一致する1個のカーソルだけを
    // newPosへ拡張する (Alt+Shift+クリック/Alt+ドラッグ、詳細は §5.3)。
    void moveCursorMatching(document::TextPos identifyingAnchor, document::TextPos newPos);
    [[nodiscard]] std::span<const Cursor> cursors() const noexcept;
    [[nodiscard]] const Cursor& primaryCursor() const noexcept;
private:
    void mergeOverlapping();
    std::vector<Cursor> m_cursors;  // 常にソート & マージ済み
};

}  // namespace neomifes::core
```

- **複数カーソル操作**: `moveAll`/`moveAllTo`/`setCursors` が全カーソルに適用され、適用後に `mergeOverlapping()` で範囲重複/同一位置のカーソルを1つにマージする(`isPrimary` は OR で伝播)。`moveCursorMatching` (Phase 4b6d) だけは例外で、1個のカーソルのみを対象とする。
- **上下移動の列保持**: `LineIndex` の契約 (`line_index.h`: 行区切りは `\n` のみ) から `lineToOffset(line+1) - 1` で行内容の終端オフセットを直接算出し、`BufferSnapshot::extract` を使わずに列クランプを行う。`\r` は他コードパス (`RenderPipeline::drawVisibleLines`) と同様ストリップしない (CRLF 対応は Phase 6 Encoding Engine のスコープ)。
- **`MovementUnit`(単語単位移動)は Phase 4b6b で `MovementKind::WordLeft`/`WordRight` として実装済み**(Ctrl+矢印、現在行内に限定、詳細は §5.3)。**段落単位移動は Phase 4b6d 時点でも未実装** — 段落境界の定義(空行区切り等)が未検討。ADR-012 参照。
- **矩形選択 (`setRectangular`) は Phase 4b6d 時点でも未実装** — §5.1.1 参照。

### 5.1.1 縦編集 (列編集 / MIFES 由来) — Phase 4b 以降に延期 (ADR-012)

矩形選択で選んだ範囲の**各行同一列位置に対して同時に**挿入/削除/上書きを行う操作。以下の Command を提供する構想 (未実装)。Rendering Engine に矩形選択のハイライト描画が無く、実装しても視覚的に確認できないため Phase 4a では延期した。

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

`src/core/include/neomifes/core/viewport.h` / `viewport.cpp` に実装。`FoldingMap`/`setFoldingRanges` は Phase 7 (折り畳みエンジン) 未着手のため Phase 4a では未実装 (ADR-012)。**`RenderPipeline::setTopLine()` への配線は Phase 4b1 で `src/app/main.cpp` が実装済み** (`neomifes::core` 自体は `neomifes::render` に依存しない設計を維持 — CLAUDE.md §3「並行実行可能な独立エンジン」の原則、ADR-010 の依存許容とは別の判断、ブリッジはアプリ層が担う)。

`ensureVisible()` は Phase 4a 時点では `noexcept` 宣言だったが、内部で呼ぶ `Document::offsetToLine()` が (`LineIndex` 再構築で `snapshot()` 経由の allocate が起きうるため) `noexcept` ではないことが Phase 4a 完了後のコードレビューで判明し、Phase 4b1着手前に `noexcept` を削除して修正済み。

```cpp
namespace neomifes::core {

struct LineRange {
    document::LineNumber start = 0;
    document::LineNumber end   = 0;  // exclusive
};

class Viewport {
public:
    void scrollTo(document::LineNumber topLine) noexcept;
    void ensureVisible(document::TextPos pos, const document::Document& doc);  // not noexcept
    void setVisibleLineCount(std::uint32_t count) noexcept;
    [[nodiscard]] document::LineNumber topLine() const noexcept;
    [[nodiscard]] LineRange visibleLines() const noexcept;
private:
    document::LineNumber m_topLine          = 0;
    std::uint32_t          m_visibleLineCount = 0;
    // FoldingMap m_folds; -- Phase 7 まで未実装 (ADR-012)
};

}  // namespace neomifes::core
```

- `scrollTo`/`visibleLines` は実際のドキュメント行数へのクランプを行わない — `RenderPipeline::drawVisibleLines()` が既に render 時にクランプしているため重複させない設計。
- `ensureVisible` のみ `Document` を参照し (`offsetToLine`)、カーソル位置が可視window外なら `m_topLine` を最小限調整する。

### 5.3 入力配線・キャレット・選択・ドラッグ・単語/行選択・複数カーソル・ページ移動・単語移動・クリップボード (Phase 4b1〜4b6d、2026-07-17実装)

`MainWindow::wndProc` (`src/ui/`) が `WM_KEYDOWN`/`WM_CHAR`/`WM_MOUSEWHEEL` を新設のフック (`onKeyDown`/`onChar`/`onMouseWheel`) 経由で `src/app/main.cpp` に配送する。実際の Win32-to-Editor-Core 変換ロジックは Win32 に一切依存しない新規ライブラリ `neomifes::app_input` (`src/app/include/neomifes/app/editor_input.h` + `editor_input.cpp`) に分離し、`tests/unit/app_editor_input_test.cpp` でヘッドレスにテストする (Win32メッセージシミュレーションハーネスがこのコードベースに存在しないための設計判断、`src/core/` の Phase 4a テストと同じ思想)。

```cpp
namespace neomifes::app {
bool handleKeyDown(UINT vkCode, bool shiftDown, bool ctrlDown,
                   core::CommandDispatcher&, core::SelectionModel&, core::Viewport&,
                   const document::Document&);
bool handleChar(wchar_t ch, core::CommandDispatcher&, core::SelectionModel&, core::Viewport&,
               const document::Document&);
[[nodiscard]] document::LineNumber applyMouseWheelScroll(short wheelDelta,
                                                          document::LineNumber currentTopLine);
}  // namespace neomifes::app
```

- `handleKeyDown`: 矢印(+Shift拡張)・Home/End(+Ctrlでドキュメント端)・Backspace/Delete(選択があれば選択範囲を削除、無ければ1文字、境界ではno-op)・Ctrl+Z/Ctrl+Y(undo/redo)。
- `handleChar`: 印字可能文字(選択があれば`ReplaceRangeCommand`、無ければ`InsertTextCommand`)・Enter(`\r`→`\n`)・Tab。他の制御文字は無視(Backspace/Escape等は`WM_KEYDOWN`側で処理済みのため二重処理を回避)。
- `applyMouseWheelScroll`: 純粋関数。WM_MOUSEWHEELの符号規約(正=手前に回す=前方スクロール=topLine減少)に従う。
- キャレット描画: `RenderPipeline::setCaretPosition(document::TextPos)` を新設(`neomifes::core::Cursor` 型は使わず素の `TextPos` を受け取り、render層がcoreに依存しない設計を維持)。`drawVisibleLines()` のループ内でキャレット行のみ `IDWriteTextLayout::HitTestTextPosition()` を呼び `FillRectangle` で細いバーを描く(既存の `m_textBrush` を再利用、新規ブラシは作らない)。
- `RenderPipeline::FrameState` に `caretPosition` を追加(Phase 3c の粗粒度フレームスキップがキャレット単独の移動を再描画対象外にしてしまう不整合を修正)。

**Phase 4b2 (2026-07-17実装) でマウスクリック位置特定と選択範囲ハイライト描画を追加:**

```cpp
// SelectionModel::moveAllTo に extendSelection を追加 (デフォルト引数、既存呼び出しは変更不要):
void moveAllTo(document::TextPos position, bool extendSelection = false);

namespace neomifes::render {
class RenderPipeline {
public:
    // このコードベース初の HitTestPoint 使用 (座標→位置)。HitTestTextPosition
    // (位置→座標、caret描画で使用済み) の逆方向にあたる。
    [[nodiscard]] std::optional<document::TextPos> hitTest(std::int32_t xPx, std::int32_t yPx) noexcept;
    void setSelectionRange(document::TextRange range) noexcept;
};
}

namespace neomifes::app {
bool handleMouseDown(document::TextPos pos, bool shiftDown, core::SelectionModel&,
                     core::Viewport&, const document::Document&);
}
```

- `RenderPipeline::hitTest()`: クリック座標 (デバイスピクセル) を `m_dpiScale` でDIPに変換し、`m_topLine`+`m_lineHeightDips` から対象行を特定、その行のテキストを1行分 `extract()` し `TextLayoutCache::getOrCreate()` でレイアウトを取得 (可視行なら描画時に作成済みでキャッシュヒット) してから `HitTestPoint()` を呼ぶ。列位置は `isTrailingHit ? (textPosition + length) : textPosition` の定番イディオムで算出
- 選択範囲ハイライト: `setSelectionRange(TextRange)` を新設、`FrameState` に `selectionRange` を追加(caretPosition追加と同じ理由)。`drawVisibleLines()` のループ内、`DrawTextLayout` を呼ぶ**前**に該当行を半透明の新規ブラシ (`m_selectionBrush`) で塗る (`drawSelectionOnLine()`、`HitTestTextPosition`を選択開始/終了列それぞれに呼ぶ2回構成)
- `handleMouseDown()`: 座標→`TextPos` のヒットテストは `RenderPipeline` (レイアウト情報を持つレンダー層) でしか行えないため、`editor_input` はヒットテスト済みの `TextPos` を受け取るだけに留め、Win32/render非依存の既存制約を維持。`selection.moveAllTo(pos, shiftDown)` を呼ぶだけの薄い実装
- `MainWindow` に `onMouseDown` フック新設、`WM_LBUTTONDOWN` を追加(`<windowsx.h>` の `GET_X_LPARAM`/`GET_Y_LPARAM`、Shift状態はマウスメッセージの慣例通り `wParam & MK_SHIFT` から取得)

**Phase 4b3 (2026-07-17実装) でドラッグ選択を追加:**

調査の結果、Phase 4b2 の `handleMouseDown(pos, shiftDown=true, ...)` が既に「anchorを保持しpositionだけ動かす」という、ドラッグの継続移動に必要な挙動と完全に一致することが判明し、**新規の core/app ロジックは一切不要だった** — `MainWindow` 側の Win32 状態管理 (`SetCapture`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`) を追加するだけで実現した。

```cpp
// MainWindowConfig に追加 (shiftDownパラメータなし - ドラッグは常にanchor保持での拡張):
std::function<void(HWND, std::int32_t x, std::int32_t y)> onMouseDrag;
```

- `WM_LBUTTONDOWN` (`handleMouseDown`) の先頭で `::SetCapture(m_hwnd)` を呼び `m_isDragging = true` にする(既存の `onMouseDown` 呼び出しはそのまま)。プレーンなクリックでドラッグに発展しない場合も直後の `WM_LBUTTONUP` で無害に capture が解放される
- `WM_MOUSEMOVE` (`handleMouseMove`): `m_isDragging` の間だけ `onMouseDrag` を発火
- `WM_LBUTTONUP` (`handleMouseUp`): `::ReleaseCapture()` + `m_isDragging = false`
- `main.cpp` の `onMouseDrag` は `RenderPipeline::hitTest()` でヒットテストした後、`handleMouseDown(*hit, /*shiftDown=*/true, ...)` を呼ぶだけ。ドラッグ開始点(`WM_LBUTTONDOWN` の `onMouseDown` が確定させたanchor)からの拡張が、Shift+ドラッグ・通常ドラッグの両方で自然に成立する
- `SetCapture` を使う理由: ドラッグ中にカーソルがクライアント領域外に出ても `WM_MOUSEMOVE`/`WM_LBUTTONUP` が自ウィンドウへ確実に配送される、Win32 標準のドラッグ実装パターン

**Phase 4b4 (2026-07-17実装) でダブルクリック単語選択・トリプルクリック行選択を追加:**

単語境界判定の方式についてユーザーに確認し、「簡易文字種ベース」(ASCII英数字+`_`の連続・CJK文字の連続をそれぞれ1単語、それ以外の記号は1文字ずつ)を採用。Unicode UAX #29 準拠の本格実装は外部ライブラリ導入とADR起票を要するため見送った。

```cpp
// 新規 src/ui/include/neomifes/ui/click_tracking.h (ヘッダオンリー、純粋関数。
// resize_math.h/viewport_math.h と同じ「Windows SDK非依存でユニットテスト
// 可能」パターンを src/ui/ に初適用):
namespace neomifes::ui {
struct ClickPoint { std::int32_t x = 0, y = 0; };
struct ClickTrackerState { ClickPoint lastPos{}; std::uint32_t lastTimeMs = 0; int count = 0; };
[[nodiscard]] constexpr ClickTrackerState nextClickState(
    const ClickTrackerState& previous, ClickPoint pos, std::uint32_t nowMs,
    std::uint32_t thresholdMs, std::int32_t maxDx, std::int32_t maxDy) noexcept;
}

namespace neomifes::core {
class SelectionModel {
public:
    void selectWordAt(document::TextPos pos, const document::Document& doc);
    void selectLineAt(document::TextPos pos, const document::Document& doc);
};
}

namespace neomifes::app {
bool handleDoubleClick(document::TextPos pos, core::SelectionModel&, core::Viewport&, const document::Document&);
bool handleTripleClick(document::TextPos pos, core::SelectionModel&, core::Viewport&, const document::Document&);
}
```

- クリック回数判定: `WM_LBUTTONDBLCLK`(`CS_DBLCLKS`要)には「3回目」の概念が無いため、`WM_LBUTTONDOWN` 単体で `nextClickState()` を呼んで手動判定する(`CS_DBLCLKS` は追加しない)。`MainWindow::handleMouseDown()` が `::GetMessageTime()`/`::GetDoubleClickTime()`/`::GetSystemMetrics(SM_CXDOUBLECLK/SM_CYDOUBLECLK)` を渡す。`onMouseDown` フックのシグネチャに `int clickCount`(1/2/3、3で頭打ち)を追加
- `SelectionModel::selectWordAt()`: 行を1行分 `doc.snapshot()->extract()` で取得(`RenderPipeline::hitTest()` と同じ「1行分だけ抽出」パターン)し、クリック列から同じ文字種クラスの連続範囲を前後にスキャン。文字種分類は無名名前空間の `classify(char16_t)` — ASCII英数字+`_`/CJK範囲(ひらがな・カタカナ・CJK統合漢字・半角全角形式)→Word、空白→Whitespace、それ以外→Other(1文字ずつ)
- `SelectionModel::selectLineAt()`: 既存の `lineContentEnd()` を再利用。最終行以外は次行の開始オフセット(`\n`を含む)を選択終端とし、選択状態でのBackspace/Deleteで行がきれいに消えるようにする
- `handleDoubleClick`/`handleTripleClick`: `handleMouseDown` の既存契約(単純配置/Shift拡張)を変更せず新規の兄弟関数として追加。ドラッグ経路(`onMouseDrag`→`handleMouseDown(shiftDown=true)`)には影響しない
- `main.cpp` の `onMouseDown` は `clickCount>=3`→`handleTripleClick`、`==2`→`handleDoubleClick`、それ以外→既存の `handleMouseDown` に分岐

**Phase 4b5a (2026-07-17実装) で複数カーソル編集コマンド基盤 (core層、ヘッドレス) を追加:**

Alt+クリックでカーソルを追加できても、そのカーソル全てに編集が反映されなければ機能として不完全という Phase 4b4 完了時の指摘を受けて調査した結果、`ICommand::cursorPositionAfterExecute()`/`AfterUndo()` (単一 `TextPos` を返し `SelectionModel::moveAllTo()` で全カーソルを1点に強制収束させる) という Phase 4b1 由来のインターフェースが複数カーソルを原理的に表現できないことが判明。既存3コマンドも含めてインターフェースを一般化した:

```cpp
namespace neomifes::core {
class ICommand {
public:
    // Phase 4b1 の cursorPositionAfterExecute()/AfterUndo() (単一TextPos) を置き換え。
    // 単一カーソル系コマンドは要素数1のvectorを返すだけで、パラレルな2つ目の
    // インターフェースは増やさない。
    virtual std::vector<Cursor> cursorsAfterExecute() const = 0;
    virtual std::vector<Cursor> cursorsAfterUndo() const    = 0;
};

// N個のカーソルへの同時編集を1回のundoステップとして適用する。累積オフセット法
// (VSCode等で使われる標準的手法): SelectionModel::cursors() が保証する昇順・非
// 重複の順序のまま edits を1パス処理し、直前までの編集による純増減
// (cumulativeShift) を足し込んで各編集の実適用位置を求める。undo は降順で
// m_currentStartAtExecute (execute時に捕捉) を使って戻すため、シフト量の再計算が
// 不要。カーソル復元は execute前の SelectionModel::cursors() のスナップショット
// (選択範囲込み) をそのまま返す。
struct PerCursorEdit {
    document::TextRange range;         // 空range = 純粋挿入
    std::u16string        insertedText; // 空文字列 = 純粋削除
};
class MultiCursorEditCommand final : public ICommand {
public:
    MultiCursorEditCommand(std::vector<PerCursorEdit> edits, std::vector<Cursor> cursorsBefore);
    // ...
};
}  // namespace neomifes::core
```

`SelectionModel::setCursors(std::vector<Cursor>)` を新設 (`mergeOverlapping()` 込み、`moveAllTo()`と同系統の置き換え先)。`CommandDispatcher::dispatch()`/`UndoStack::undo()`/`redo()` は `moveAllTo(pos)` 呼び出しを `selection.setCursors(command->cursorsAfterExecute()/AfterUndo())` に置き換え。既存の `InsertTextCommand`/`DeleteRangeCommand`/`ReplaceRangeCommand` はクラスとして残すが、Phase 4b5b で `editor_input.cpp` の呼び出し経路が `MultiCursorEditCommand` に一本化されたため、実アプリからは呼ばれなくなった (単体テストのみでの被覆、削除はしていない)。

**Phase 4b5b (2026-07-17実装) で Alt+クリック複数カーソル追加の入力配線を追加:**

```cpp
namespace neomifes::app {
bool handleAltClick(document::TextPos pos, core::SelectionModel&, core::Viewport&, const document::Document&);
}
```

- `handleAltClick()`: 既存 (Phase 4a) の `SelectionModel::addCursor()` を呼ぶだけの薄い実装。新規 core メソッドは不要だった
- `editor_input.cpp` の `handleChar`/`applyDeleteKey` を全カーソル対応に書き換え: `selection.cursors()` の各カーソルから `PerCursorEdit` を1個ずつ(1:1・同順序で)組み立て、`MultiCursorEditCommand` を1回ディスパッチする形に統一 (カーソルが1個の場合も同じ経路、単一/複数で分岐しない)。境界(文書先頭でのBackspace等)でそのカーソルの編集が発生しない場合も空range/空文字列の"no-op edit"として1エントリを必ず作る (`MultiCursorEditCommand` は1カーソル1エントリの1:1対応を前提とするため) — 全カーソルがno-opならディスパッチ自体を行わない(単一カーソル時の「何も起きない」動作を維持)
- Win32側: `WM_LBUTTONDOWN` の wParam には `MK_ALT` が存在しない (Shift/Ctrlの `MK_SHIFT`/`MK_CONTROL` とは非対称)。`MainWindow::handleMouseDown()` で `::GetKeyState(VK_MENU) & 0x8000` を都度読み取り、`onMouseDown` フックのシグネチャに `bool altDown` を追加
- `main.cpp`: `onMouseDown` ラムダの分岐ロジックをcognitive complexity低減のため `dispatchMouseDown()` (新規フリー関数) に切り出し。`altDown` が最優先分岐で `handleAltClick` へ、それ以外は既存の `clickCount` 分岐。Alt+ダブル/トリプルクリックの組み合わせ意味は定義しない(altDownが立っていれば常にhandleAltClick)

**Phase 4b6a (2026-07-17実装) で PageUp/PageDown を追加:**

```cpp
enum class MovementKind : std::uint8_t {
    Left, Right, Up, Down, LineStart, LineEnd, DocumentStart, DocumentEnd,
    PageUp, PageDown,  // moveAll()のpageSize引数がジャンプ量を供給
};
void SelectionModel::moveAll(MovementKind, const document::Document&, bool extendSelection,
                             document::LineNumber pageSize = 0);
```

垂直移動の列保持ロジック (`moveVertically(doc, current, bool up)`) を「1行分の上下」から「任意行数の上下」(`moveVertically(doc, current, int64_t lineDelta)`) に一般化し、既存の `Up`/`Down` (delta=±1) と新規 `PageUp`/`PageDown` (delta=±pageSize) が同じ実装を共有する。`editor_input.cpp` の `handleKeyDown()` が `viewport.visibleLines()` の行数を `pageSize` として渡す。ページ送り後のスクロールは既存の `ensureVisible()` がそのまま自然に「1ページ分スクロール」を実現するため、新規スクロールロジックは不要だった。

**Phase 4b6b (2026-07-17実装) で Ctrl+矢印(単語移動)を追加:**

```cpp
enum class MovementKind : std::uint8_t { /* ... */ WordLeft, WordRight };
```

`selectWordAt()` (Phase 4b4) が既に持つ `classify(char16_t)`/`CharKind` を(このコマンドベースでのみ使う内部詳細から)`moveByWord()` と共有する小さなヘルパー群に格上げし、単語境界の定義を1箇所に保つ。**単語移動は現在行の中で完結** — 行頭/行末で止まり、隣接行への越境はしない(`selectWordAt()`と同じ単一行スコープを踏襲、複数行走査という新たな設計判断を避けるため意図的な簡略化)。`editor_input.cpp` の `applyMovementKey()` で既存の `VK_LEFT`/`VK_RIGHT` ケースに `ctrlDown` 分岐を追加(`VK_HOME`/`VK_END` の既存パターンと同型)。

**Phase 4b6c (2026-07-17実装) で選択範囲のクリップボードコピー (Ctrl+C/X/V) を追加:**

```cpp
namespace neomifes::platform {
[[nodiscard]] bool setClipboardText(HWND owner, std::u16string_view text) noexcept;
[[nodiscard]] std::optional<std::u16string> getClipboardText(HWND owner);
}
namespace neomifes::app {
[[nodiscard]] std::optional<std::u16string> textToCopy(const core::SelectionModel&, const document::Document&);
bool handlePaste(std::u16string_view, core::CommandDispatcher&, core::SelectionModel&, core::Viewport&, const document::Document&);
}
```

**スコープはプライマリカーソルの選択範囲のみ**(複数カーソルを跨いだコピー/ペーストの分配は次点課題)。クリップボードは Win32 API を要するため、新規 `src/platform/clipboard.h/.cpp` に切り出し(`editor_input.cpp` はWin32 API呼び出しゼロという既存制約を維持)。`GlobalAlloc`/`GlobalLock`/`SetClipboardData` の定番手順(`SetClipboardData`成功後は所有権がシステムに移るため`GlobalFree`しない)。Cut はクリップボード書き込みが失敗した場合、選択範囲を削除しない(コピーできなかったテキストの消失を防ぐ)。`main.cpp` の `onKeyDown` は `neomifes::app::handleKeyDown()` を呼ぶ前に Ctrl+C/X/V を判定・処理する新規 `handleClipboardKey()`。この関数もclang-tidyのcognitive complexity対策で、`onKeyDown`ラムダの本体全体を`handleKeyDownEvent()`という独立関数に切り出す形になった(ラムダがwireNormalMode内にインライン定義されていると、その本体の複雑度が外側関数に積算されるため、分岐ロジックだけでなくラムダ本体そのものを外に出す必要があった)。

**Phase 4b6d (2026-07-17実装) で Alt+Shift+クリック / Alt+ドラッグ(追加カーソルの選択拡張)を追加:**

```cpp
namespace neomifes::core {
class SelectionModel {
public:
    // anchorがidentifyingAnchorと一致する1個のカーソルだけをnewPosへ拡張する。
    // moveAll()/moveAllTo()は常に全カーソルへ一様に適用されるため、
    // 「特定の1カーソルだけを動かす」ための新規プリミティブとして追加。
    void moveCursorMatching(document::TextPos identifyingAnchor, document::TextPos newPos);
};
}
```

カーソルは `mergeOverlapping()` で毎回ソート・マージされ配列添字が不安定なため、「識別に使える安定したキー」としてカーソルの `anchor`(拡張中は不変)を採用。`main.cpp` の `wWinMain` に `std::optional<TextPos> altCursorAnchor` を新設(`selectionModel`等と同じ寿命が必要なため `wireNormalMode` の外、ローカル変数として宣言し参照で渡す — `MainWindow::m_isDragging` がメンバ変数である理由と同じ)。プレーンAlt+クリックで設定、Alt+Shift+クリックとAlt+ドラッグで消費、Alt無しのクリックでリセット。

**既知の制限:** `RenderPipeline` はキャレット位置・選択範囲を1個(プライマリカーソルの分)しか保持しないため、Alt+クリックで追加した非プライマリカーソルのキャレット/選択ハイライトは描画されない(Phase 4b5a以降ずっと存在する制限で、4b6d固有の問題ではない)。`SelectionModel` レベルの正しさ(複数カーソルへの編集反映・選択範囲拡張)は単体テストで検証済みだが、視覚的にはプライマリカーソルの状態しか確認できない。複数カーソルの視覚描画対応は今回のスコープ外の、より大きな Rendering Engine 側の変更。

---

## 6. Command / Undo 詳細

**Phase 4a (2026-07-16) で実装確定 (ADR-012)。** `edit.insert`/`edit.delete`/`edit.replace` の3種のみ実装。**Phase 4b5a (2026-07-17) で4種目の `edit.multiCursor` (`MultiCursorEditCommand`) を追加**、詳細は §5.3 参照。`tryMerge`(連続入力パッキング)・§6.1.1 の残り約20種の標準コマンド・§6.1.2(バックアップ/Recent Files)は Phase 4b 以降へ明示的に延期 (削除はしない)。

### 6.1 Command 例

実装は `src/core/include/neomifes/core/command.h`(`ICommand`/`ExecutionContext`)・`edit_commands.h`/`.cpp`(`InsertTextCommand`/`DeleteRangeCommand`/`ReplaceRangeCommand`/`MultiCursorEditCommand`)。`ExecutionContext` は Phase 0 スケッチに名前は無いが、`ICommand::execute(ExecutionContext&)` と `UndoStack::push()`(execute前提)の両シグネチャから要求される最小限のグルーとして新設: `Document&` + `SelectionModel&` を保持する。**Phase 4b1 でこの `SelectionModel&` を実際に使い始め、Phase 4b5a で複数カーソルに対応する形へ一般化した** — `CommandDispatcher::dispatch()`/`UndoStack::undo()`/`redo()` は当初 各コマンドの `cursorPositionAfterExecute()`/`AfterUndo()`(単一 `TextPos`)を `ctx.selection().moveAllTo()` に渡していたが、これは全カーソルを1点に強制収束させることしかできず複数カーソル編集を表現できないため、`cursorsAfterExecute()`/`AfterUndo()`(`std::vector<Cursor>`)を `ctx.selection().setCursors()` に渡す形へ置き換えた(単一カーソル系コマンドは要素数1のvectorを返すだけの機械的な変更)。

```cpp
namespace neomifes::core {

class ExecutionContext {
public:
    ExecutionContext(document::Document& document, SelectionModel& selection) noexcept;
    [[nodiscard]] document::Document& document() noexcept;
    [[nodiscard]] SelectionModel&     selection() noexcept;
};

class InsertTextCommand final : public ICommand {
public:
    InsertTextCommand(document::TextPos pos, std::u16string text) noexcept;
    void execute(ExecutionContext&) override;   // ctx.document().insertText(m_pos, m_text)
    void undo(ExecutionContext&) override;      // ctx.document().eraseRange({m_pos, m_pos+m_text.size()})
    std::size_t weight() const noexcept override { return (m_text.size() * 2) + 32; }
    std::string_view id() const noexcept override { return "edit.insert"; }
    // Phase 4b1で新設、Phase 4b5aでvector<Cursor>を返す形に一般化
    // (CommandDispatcher/UndoStack が呼ぶ):
    std::vector<Cursor> cursorsAfterExecute() const override;  // {{m_pos+m_text.size(), 同値, true}}
    std::vector<Cursor> cursorsAfterUndo() const override;     // {{m_pos, 同値, true}}
    // tryMerge(連続入力パッキング) は Phase 4b5b時点でも未実装のまま (ADR-012):
    // マージ閾値の決定は次のPhase 4bサブフェーズの課題として残る
    // (時間経過/文字種によるマージ境界は未合意)
private:
    document::TextPos m_pos;
    std::u16string     m_text;
};

// DeleteRangeCommand / ReplaceRangeCommand も同型: execute() 内で
// BufferSnapshot::extract() により削除/置換前のテキストを捕捉し、undo() で復元する
// (RenderPipeline::drawVisibleLines() が既に使っている extract パターンを再利用)。
// Phase 4b5b以降、editor_input.cpp の呼び出し経路は MultiCursorEditCommand に
// 一本化されたため、この3クラスは実アプリからは呼ばれず単体テストのみで
// 被覆されている (削除はしていない - §5.3 Phase 4b5a参照)。

// N個のカーソルへの同時編集を1回のundoステップとして適用する (Phase 4b5a、
// 累積オフセット法。詳細は §5.3 参照):
struct PerCursorEdit {
    document::TextRange range;         // 空range = 純粋挿入
    std::u16string        insertedText; // 空文字列 = 純粋削除
};
class MultiCursorEditCommand final : public ICommand {
public:
    MultiCursorEditCommand(std::vector<PerCursorEdit> edits, std::vector<Cursor> cursorsBefore);
    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    std::string_view id() const noexcept override { return "edit.multiCursor"; }
    std::vector<Cursor> cursorsAfterExecute() const override;  // execute()で計算
    std::vector<Cursor> cursorsAfterUndo() const override;     // cursorsBeforeをそのまま返す(選択範囲込み)
};

// execute() が「実行してから push する」呼び出し順を要求するため (UndoStack::push
// はexecute前提)、Phase 0 スケッチに無かった CommandDispatcher を新設した:
class CommandDispatcher {
public:
    CommandDispatcher(document::Document& document, SelectionModel& selection) noexcept;
    // execute() → selection.setCursors(cmd->cursorsAfterExecute()) → UndoStack::push()
    void dispatch(std::unique_ptr<ICommand> command);
    bool undo();  // UndoStack::undo() が同様に setCursors(cursorsAfterUndo()) を呼ぶ
    bool redo();  // UndoStack::redo() が同様に setCursors(cursorsAfterExecute()) を呼ぶ
private:
    ExecutionContext m_context;
    UndoStack        m_undoStack;
};

}  // namespace neomifes::core
```

### 6.1.1 標準 Command 一覧 (要件対応)

**Phase 4a で実装済みなのは `edit.insert`/`edit.delete`/`edit.replace` の3種のみ。** 残りは以下の理由で Phase 4b 以降へ延期 (ADR-012):
- `edit.autoIndent`/`edit.formatDocument`/`edit.tabsToSpaces`/`edit.spacesToTabs`: 仕様(インデント幅設定・LSP連携方針)が未確定
- `column.*`(縦編集): §5.1.1 参照、矩形選択ハイライト描画が Rendering Engine に無い
- `file.changeEncoding`/`file.changeLineEnding`/`file.toggleBom`: Encoding Engine (Phase 6) 未着手
- `file.autoSave`/`file.backup`/`file.recent.open`: §6.1.2 参照
- `search.*`: Search Engine (Phase 5) 未着手
- `nav.bookmark.*`/`view.fold.toggle`/`view.outline.jump`: 折り畳み/アウトライン (Phase 7) 未着手
- `ai.invoke`: Plugin Engine (Phase 8) / AI Plugin (Phase 9) 未着手

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

### 6.1.2 バックアップ / Recent Files — Phase 4b 以降に延期 (ADR-012)

- **バックアップ (`file.backup`)**
  - 上書き保存時、直前バージョンを同一ディレクトリの `<name>.bak` に退避 (既存 `.bak` は上書き)
  - 世代数は設定で 1〜10 の範囲。世代管理時は `<name>.bak.<n>` サフィックス
  - 大容量 (>100MB) ファイルは設定でスキップ可
- **Recent Files**
  - `SessionManager` が管理、保存先 `%APPDATA%\NeoMIFES\recent.json5`
  - 最大 100 件 (LRU)、ピン留め機能あり
  - パスは正規化 (`std::filesystem::weakly_canonical`) して重複排除

### 6.2 Undo Stack

**Phase 4a (2026-07-16) で実装確定 (ADR-012)。** 1000件バケット化・zstd圧縮・ディスクスワップ (下記の元スケッチ) は延期し、`std::vector<std::unique_ptr<ICommand>>` 2本 (undo/redo) のシンプルな実装にした。100万件到達の DoD 主張はベンチマーク実測値を根拠とする (CLAUDE.md ルール10)。

```cpp
// 実装 (src/core/include/neomifes/core/undo_stack.h):
namespace neomifes::core {

class UndoStack {
public:
    void push(std::unique_ptr<ICommand> command);
    bool undo(ExecutionContext&);  // false = undoスタックが空
    bool redo(ExecutionContext&);  // false = redoスタックが空
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
private:
    std::vector<std::unique_ptr<ICommand>> m_undo;
    std::vector<std::unique_ptr<ICommand>> m_redo;
};

}  // namespace neomifes::core
```

**実測値 (`tests/bench/core_undo_stack_bench.cpp`, Release, 1,000,000コマンド, `--benchmark_min_time=0.01s`):**

| ベンチマーク | 実測値 |
|---|---|
| `BM_UndoStack_PushOneMillion`(100万件push) | 352ms |
| `BM_UndoStack_UndoOneMillion`(100万件undo) | 174ms |

DoD「100万Undo達成」はこの実測で満たされたと判断する。メモリ使用量は未計測 — [`docs/issues/undo_stack_unbounded_memory.md`](../issues/undo_stack_unbounded_memory.md) の tripwire として記録済み。**Phase 4b1 (2026-07-17)** で実アプリでの対話的編集セッション(約1,350文字入力)による最初の実測を追記(WorkingSet増分 約3MB)したが、100万件規模には遠く及ばない小規模サンプルのため、issue自体は引き続きOpenのまま維持している(詳細は issue doc 参照)。`undo(ExecutionContext&)`/`redo(ExecutionContext&)` は Phase 4b1 で `ctx.selection().moveAllTo(command->cursorPositionAfterUndo()/AfterExecute())` を呼ぶよう拡張済み(§6.1参照)。

**元スケッチ (Phase 0、未実装。Phase 4b 以降で実メモリ計測に基づき再検討 — ADR-012 参照):**
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
| PieceTable::insert (small edit) | ≤ 500ns | 🟢 CI実測 243〜276ns |
| PieceTable::snapshot (100K pieces) | ≤ 1ms | 🟡 実測 1.2〜1.5ms (低優先度残タスク) |
| Render frame (100 行) | ≤ 3ms | 未計測 (Phase 3b+) |
| Search (1GB, plain) | ≥ 500MB/s | 未計測 (Phase 5) |
| Undo/Redo (100k ops) | ≤ 50ms | 未計測 (Phase 4) |

---

## 19. ビルド & CI 詳細

### 19.1 ビルド

> 確定済み: [ADR-001](../decisions/ADR-001-build-system.md) / [ADR-005](../decisions/ADR-005-min-msvc-version.md)

```
CMake >= 3.28, Ninja ジェネレータ
MSVC v143 (VS 17.13+, ADR-005)  — ローカル開発機は VS 2026 (MSVC 19.50)
/std:c++latest /W4 /permissive- /Zc:__cplusplus /EHsc /GR-
Debug:   /fsanitize=address /Zi /Od (ASan プリセット)
Release: /O2 /Ob3 /GL /LTCG /GS-
UBSan:   clang-cl + /MT (静的 CRT) + -fno-sanitize=alignment (ubsan プリセット)
```
- `/GR-` は RTTI 無効。プラグイン境界は C ABI なので影響なし。`dynamic_cast` 禁止 (CLAUDE.md §4)
- `src/.clang-tidy` で本番コードのみ `WarningsAsErrors: '*'`。`tests/` はルートの `WarningsAsErrors: ''` が適用

### 19.2 CI (GitHub Actions)
- ジョブ: `build-and-test` (Debug/Release マトリクス)、`static-analysis` (clang-tidy)、`ubsan` (clang-cl UBSan)
- ベンチマークは smoke 実行のみ (CI 上の退化ガードは Phase 3c の `--measure-frame` と併せて導入予定)

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
