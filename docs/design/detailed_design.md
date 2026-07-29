# NeoMIFES 詳細設計書 v1.0

> 上位: [`basic_design.md`](basic_design.md) / 要件: [`../../NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md)
> 未着手フェーズの実装詳細: [`master_roadmap.md`](master_roadmap.md) (Plan-of-Record、Phase 5c以降はこちらが正。Phase 4b8は2026-07-20に全サブフェーズ完了、確定内容は本書§5.1.1/§5.3へ吸収済み)

本書は各モジュールの内部データ構造・クラス設計・アルゴリズム・API 仕様を規定する「How」レベルのドキュメント。**本書は実装済み機能のリファレンス。未着手フェーズの計画は `master_roadmap.md` を参照。フェーズ完了時に該当章がここへ吸収される。**

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

**Phase 4a (2026-07-16) で実装確定 (ADR-012)。** 本節は Phase 0 時点のスケッチから、`src/core/` の実装内容に同期済み。§5.1.1 (縦編集)・§5.2 の `FoldingMap` は ADR-012 により Phase 4b 以降へ明示的に延期 (削除はしない)。**Phase 4b1 (2026-07-17) でキーボード入力配線 (`src/app/editor_input.h/.cpp`)・キャレット描画・マウスホイールスクロールを実装、Phase 4b2 (同日) でマウスクリック位置特定・選択範囲ハイライト描画、Phase 4b3 (同日) でドラッグ選択、Phase 4b4 (同日) でダブルクリック単語選択・トリプルクリック行選択、Phase 4b5a/4b5b (同日) で複数カーソル編集コマンド基盤 + Alt+クリック複数カーソル追加、Phase 4b6a〜4b6d (同日) で PageUp/PageDown・Ctrl+矢印単語移動・クリップボードコピー・Alt+Shift+クリック/Alt+ドラッグ選択拡張、Phase 4b7a〜4b7c (同日) で複数カーソルの視覚描画・行を跨ぐ単語移動・複数カーソルクリップボードを実装**(詳細は本節末尾および §6 参照)。

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
- **`MovementUnit`(単語単位移動)は Phase 4b6b で `MovementKind::WordLeft`/`WordRight` として実装、Phase 4b7b で複数行対応に拡張済み**(Ctrl+矢印、詳細は §5.3)。**段落単位移動は Phase 4b7c 時点でも未実装** — 段落境界の定義(空行区切り等)が未検討。ADR-012 参照。
- **矩形選択 (`setRectangular`) は Phase 4b6d 時点でも未実装** — §5.1.1 参照。

### 5.1.1 縦編集 (列編集 / MIFES 由来)

**Phase 4b8a (2026-07-19) で矩形選択そのものは実装済み** — `SelectionModel::setRectangularSelection(TextPos anchor, TextPos active, const Document& doc)`(下記コード例)。「矩形範囲の各行同一列位置に対して同時に挿入/削除/上書き」という**専用コマンド**(`ColumnInsertCommand`等、下記)は依然未実装のまま。矩形選択後の実際の編集(タイプ入力・Ctrl+V貼り付け等)は、既存の`MultiCursorEditCommand`(Nカーソルへ一様適用、Phase 4b5a)がそのまま処理する — 「短い行はパディングしない・行末で停止」という下記の`ColumnOverwriteCommand`構想の挙動を、専用コマンドを新設せずに`setRectangularSelection()`自身のクランプ処理(各行の列を実際の行長でクランプ)だけで実質的に代替している。

Rendering Engine自体は元々矩形選択専用の描画コードを持たない(§5.3のCursorVisual/drawSelectionsOnLineが「各行1カーソル」を透過的に描画するため新規描画コード不要と判明、Phase 4b8a実装時に確認済み)。

```cpp
// src/core/include/neomifes/core/selection_model.h に追加 (Phase 4b8a)
class SelectionModel {
public:
    // ...
    void setRectangularSelection(document::TextPos anchor, document::TextPos active,
                                 const document::Document& doc);
};
```

`SelectionMode`列挙体は採用しなかった(roadmapのスケッチから乖離) — 既存の`moveAll()`がカーソル集合へ一様適用される設計のおかげで、矩形選択後に矢印キーを押すとVSCode同様「N個の独立カーソルへ降格」する挙動が新規コード無しで自然に得られるため、今回のスコープでは「モード」概念自体が不要だった。

**Phase 4b8g (2026-07-20) でキーボードによる矩形選択拡張を追加。** `moveOne()`は`moveTextPos()`という公開自由関数へ格上げされ(シグネチャ・ロジックは不変)、`main.cpp`の`Shift+Alt+矢印`ハンドラ(`MainWindow::onSysKeyDown`経由)が`rectangularAnchor`を再利用して`setRectangularSelection()`を呼ぶ。新規`SelectionModel::convertToLineEndCursors()`が`Shift+Alt+I`(選択範囲→各行末尾の1カーソルへ変換)を実装。詳細は§5.3のPhase 4b8b〜4b8g追記を参照。

**依然未実装の専用コマンド構想 (将来の縦編集フェーズ向け):**

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

- 全行に一意な `TextPos` を持たせて `SelectionModel::setRectangularSelection` の展開結果と同期する。
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

### 5.3 入力配線・キャレット・選択・ドラッグ・単語/行選択・複数カーソル・ページ移動・単語移動・クリップボード (Phase 4b1〜4b7c、2026-07-17実装)

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

`selectWordAt()` (Phase 4b4) が既に持つ `classify(char16_t)`/`CharKind` を(このコマンドベースでのみ使う内部詳細から)`moveByWord()` と共有する小さなヘルパー群に格上げし、単語境界の定義を1箇所に保つ。単語移動は当初(本Phase時点)は現在行の中で完結する簡略版として実装 — 行頭/行末で止まり隣接行への越境はしない設計だったが、**この単一行スコープは Phase 4b7b で複数行対応に拡張済み**(下記参照)。`editor_input.cpp` の `applyMovementKey()` で既存の `VK_LEFT`/`VK_RIGHT` ケースに `ctrlDown` 分岐を追加(`VK_HOME`/`VK_END` の既存パターンと同型)。

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

**(解消済み、Phase 4b7a参照):** 上記の「非プライマリカーソルは描画されない」という制限は Phase 4b7a で解消した。

**Phase 4b7a (2026-07-17実装) で複数カーソルの視覚描画を追加:**

`RenderPipeline` はそれまでキャレット位置・選択範囲を1個(プライマリカーソルの分)しか保持しておらず、Alt+クリックで追加した非プライマリカーソルは`SelectionModel`レベルでは正しく動作していても画面には一切描画されなかった(Phase 4b5a以降ずっと存在していた制限)。

```cpp
namespace neomifes::render {
// document::型のみに依存 (core::Cursorには依存しない、既存制約を維持)
struct CursorVisual {
    document::TextPos   position;
    document::TextRange selectionRange;  // start==end: このカーソルは無選択
};

class RenderPipeline {
public:
    // setCaretPosition()/setSelectionRange() (単一値、Phase 4b1/4b2) を置き換え。
    void setCursorVisuals(std::vector<CursorVisual> cursors) noexcept;
};
}
```

`drawVisibleLines()` を `computeCaretDraws()`/`drawCaretsOnLine()`/`drawSelectionsOnLine()` の3関数に分割(単一キャレット/選択が全カーソル分のループになったことでcognitive complexityが閾値を超えたため、`main.cpp`の`dispatchMouseDown()`/`handleClipboardKey()`と同じ抽出パターン)。`main.cpp`の`syncRenderStateAndInvalidate()`は`selection.cursors()`全件から`CursorVisual`を組み立てて1回で渡す。

**Phase 4b7b (2026-07-17実装) で単語移動を複数行対応に拡張:**

Phase 4b6bが単一行に限定していた`moveByWord()`を、`moveByWordForward()`/`moveByWordBackward()`(+`skipWhitespaceForward()`/`Backward()`ヘルパー)に一般化。`classify()`が1行内で`'\n'`を空白として扱う性質を、行と行の**境界**(`classify()`が直接見ることのない場所)にまで拡張する形で実装。空行は「改行1個分の空白」として扱い通過する(段落区切りとしての明示的停止は別の未実装の関心事)。単語間に実際の空白文字が無い行境界(例: `"foo\nbar"`)は、1行内の単一スペースを1回のCtrl+Rightで飛び越える既存の挙動と一貫して、1回の操作で直接次の単語頭へ着地する。

**Phase 4b7c (2026-07-17実装) でクリップボードを複数カーソル対応に拡張:**

Phase 4b6cがプライマリカーソルの選択範囲のみを対象としていた`textToCopy()`/`handlePaste()`を全カーソル対応に一般化。VSCode等が行う「コピー時のカーソル数とペースト時のカーソル数が一致すれば1対1で分配する」という高度な対応はクリップボードへのメタデータ付与を要するため対象外とし、代わりにシンプルな規則を採用: コピー/カットは選択を持つ全カーソルのテキストを`\n`区切りで連結、ペーストは連結済みテキストを全カーソルへ同一内容として適用(`handleChar()`と同じ規則)。

```cpp
namespace neomifes::app {
// 新規、handleChar()と共有:
bool insertTextAtEveryCursor(std::u16string_view text, core::CommandDispatcher&,
                             core::SelectionModel&, core::Viewport&, const document::Document&);
// 全カーソルの選択を削除(Ctrl+X用、main.cppが直接DeleteRangeCommandを組み立てていたのを置換)
bool deleteAllSelections(core::CommandDispatcher&, core::SelectionModel&, core::Viewport&,
                         const document::Document&);
}
```

**Phase 4b8a (2026-07-19実装) で矩形選択のマウス配線を追加:**

`master_roadmap.md` §3.2はキーバインドを`Alt+LMouseドラッグ`と定めていたが、これは既存Phase 4b6dの「Alt+ドラッグ=直前のAlt+クリックで追加したカーソルを拡張する」という同一ジェスチャーと衝突することが判明(実装前にユーザーへAskUserQuestionで確認)。**`Shift+Alt+ドラッグ`に変更**(VSCodeの実際の慣習に整合、既存のAlt+ドラッグ/Alt+Shift+クリックは無変更のまま維持)。

Plan agentへの2ラウンドのレビューで、この方針転換自体が引き起こす2件の設計不備を検出・修正した:
1. `SelectionModel::setRectangularSelection()`の各行列計算で`min(anchorCol,activeCol)`/`max(...)`により`position`/`anchor`を振り分けると、本コードベースの「ドラッグは`position`のみを動かす」規約に反しキャレットが視覚的に後退するバグになる → 各行で`anchorCol`は常に`anchor`側、`activeCol`は常に`position`側へ独立に(行の実長でクランプしつつ)書き込むよう修正
2. 既存`altCursorAnchor`(Phase 4b6d、セッション中残り続ける)が無関係な過去のAlt+クリックにより新規`rectangularAnchor`のジェスチャーを乗っ取ってしまう不備、および矩形選択ドラッグ後に`altCursorAnchor`が古いカーソルを指したまま残留し次のShift+Alt+クリックが空振りする不備、の2点

```cpp
// src/app/main.cpp: wWinMainスコープの新規状態(altCursorAnchorと同じ寿命)
std::optional<document::TextPos> rectangularAnchor;
```

`dispatchMouseDown()`のAlt+Shift+クリック分岐は`rectangularAnchor = hit`を(既存の拡張/追加ロジックを変更せず)副次的に記録するだけに留め、実際の矩形選択構築は`onMouseDrag`の最優先分岐(`rectangularAnchor`が真なら`setRectangularSelection()`を呼び、直後に`altCursorAnchor.reset()`)が担う。`setRectangularSelection()`は常に`setCursors()`でカーソル集合を丸ごと置き換えるため、クリック単体(ドラッグに発展しない場合)の既存副作用は無害 — 詳細な検証トレースは`docs/history/TIMELINE.md`のPhase 4b8aセッション参照。

**Phase 4b8b〜4b8g (2026-07-20実装) で Phase 4b8 の残り全機能を完了:**

- **4b8b (桁位置ジャンプ):** 新規`ui::GotoLineBar`(`goto_line_bar.{h,cpp}`) — FindBar/CommandPaletteより単純な単一`WC_EDITW`のみのオーバーレイ(デバウンス・リストボックス不要)。新規`ui::parseGotoLineInput()`(`goto_line_parser.h`、ヘッダオンリー純粋関数)が`"123"`/`"123:45"`(共に1始まり)をパース。`Ctrl+G`で表示、`jumpToGotoTarget()`が0始まりへ変換しクランプして`selectionModel.moveAllTo()`+`viewport.ensureVisible()`。
- **4b8c (マーカー):** 新規`core::BookmarkManager`(`bookmark_manager.{h,cpp}`) — ソート済み`vector<LineNumber>`、`toggle()`/`next()`/`previous()`(ラップアラウンド)。**ドキュメント編集への追従は実装しない既知の制約**(本コードベースにEditEvent購読機構が存在しないため、`Document`は`version()`ポーリングのみ、ADR-010)。`RenderPipeline`に最小限のブックマーク専用ガター(●印のみ、`kGutterWidthDips=24dip`、行番号・折りたたみは含まない)を新設。設計検証で`HitTestTextPosition()`がレイアウトローカル座標を返す(`DrawTextLayout()`の描画原点と独立)ことをPlan agentレビューで検出し、`drawCaretOnLine`/`drawSelectionOnLine`/`drawMatchOnLine`の3メソッド全てに`kGutterWidthDips`の明示的加算を実装前に追加。`Ctrl+F2`でトグル、`F2`/`Shift+F2`で次/前ジャンプ。
- **4b8d (タブ⇔スペース変換):** 新規`core::computeIndentationConversionEdits()`(`indentation_conversion.{h,cpp}`) — 各行先頭の連続空白のみを対象にしたヘッドレス純粋関数。専用コマンドクラスは新設せず、結果を既存`core::ReplaceAllCommand`(§7.1'''置換)へそのまま渡す。コマンドパレットに"Convert Tabs to Spaces"/"Convert Spaces to Tabs"を追加(`tabWidth=4`固定、設定システムが無いためハードコード)。
- **4b8e (フリーカーソル、簡略版):** `document::TextPos`は拡張せず(176箇所・28ファイルでの使用実績からユーザーにAskUserQuestionで確認済み)、`main.cpp`のセッション状態(`std::optional<std::uint32_t> freeCursorVirtualColumns`)のみで実装。コマンドパレットの"Toggle Free Cursor Mode"で有効化。単一プライマリカーソル・無選択時のRight矢印が行の実行末に達すると仮想列をインクリメント、文字入力時に仮想列数分のスペース+入力文字を`core::ReplaceRangeCommand`で一括実体化。`render::CursorVisual::virtualColumnOffset`が等幅フォント(Consolas)の1文字幅(`m_charWidthDips`、既存の"Ag"プローブレイアウトを流用して計測)分だけキャレット描画を右にずらす。マウスでの行末より右クリック・複数カーソル同時のフリーカーソル・仮想空間の視覚的パディングは対象外。
- **4b8f (N対N分配クリップボード):** `handlePaste()`(`editor_input.cpp`)を変更 — ペーストするテキストの行数がカーソル数と一致する場合のみ各カーソルへ対応する1行ずつを分配(VSCode等の既定動作と同じ基準)、不一致時(単一カーソルへの複数行貼り付けを含む)は従来通り全カーソルへ同一テキストを挿入。`insertTextAtEveryCursor()`の内部ロジックを`insertPerCursorTexts()`(カーソルごとに独立したテキストを1つの`MultiCursorEditCommand`として適用)へ抽出し両方から共有。カスタムクリップボードフォーマットや「サイクル貼り付け」等の高度な分配ルール設定は実装しない(設定システムが存在しないため)。
- **4b8g (キーボード矩形選択拡張 + Shift+Alt+I):** `MainWindow`に`onSysKeyDown`フック(`WM_SYSKEYDOWN`)を新設 — 未消費時は必ず`DefWindowProcW`へフォールスルーし、Alt+F4等のシステムキー既定動作を保持する設計を徹底。`SelectionModel`のprivate`moveOne()`を公開自由関数`document::TextPos moveTextPos(MovementKind, const Document&, TextPos, LineNumber pageSize=0)`へ格上げし、`moveAll()`もこれを呼ぶよう変更。`Shift+Alt+矢印`ハンドラは`moveTextPos()`で新active位置を計算し、Phase 4b8aの`rectangularAnchor`状態を再利用して`setRectangularSelection()`を呼ぶ — マウスとキーボードの矩形選択拡張が同じ状態変数を共有。新規`SelectionModel::convertToLineEndCursors()`が`Shift+Alt+I`で現在のカーソル/選択範囲(`position`と`anchor`の両方を考慮)が跨る各行の実行末に1カーソルずつ配置。**既知の制約:** キーボードでの矩形拡大は「短い行を経由した後の元の意図列」を記憶しない(通常の垂直移動が持つ列保持とは異なる簡略実装)。

これでPhase 4b8はroadmap上の保留項目を残さず完全に完了した(6サブフェーズ、`docs/history/TIMELINE.md`のセッション記録参照)。

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

**Phase 5a (2026-07-18実装) で本節冒頭のスケッチのうち`SearchService::findAll`(同期・単一行スコープ)を実装、Phase 5b1 (2026-07-19実装) で複数行にまたがるマッチに対応、Phase 5b2 (2026-07-19実装) で置換(`core::ReplaceAllCommand` + `search::expandReplacementTemplate`)を実装、Phase 5b3a (2026-07-19実装) でFind bar UI(`ui::FindBar`、WC_EDIT子コントロール)を実装しここで初めて`search::`が実アプリ本体へリンクされた。** 元のスケッチは非同期(`std::future`)・`grep()`・`IncrementalFindService`・`ReplaceInFilesCommand`まで見込んでいたが、これらはPhase 5b3b以降のスコープとして未着手のまま残す(実装済み範囲との対比を明確にするため、実際に動く§7.1'/§7.1'''/§7.1''''を先に示し、元のスケッチは§7.1''として残す)。実装時に、元スケッチの`ReplaceAllCommand : public application::ICommand`という表記が実在しない名前空間だったことが判明した(実際は`neomifes::core::ICommand`、command.h:40)ほか、`master_roadmap.md` §4.3のPhase 5b2スケッチも実在しない`document::EditResult`/`SelectionModel::Snapshot`という型を前提にしていたことが判明した — いずれも実装確定前の高レベルスケッチに過ぎず、実際のシグネチャは下記§7.1'''/§7.1''''参照。

### 7.1' SearchService (Phase 5a 実装、Phase 5b1 で複数行対応)
```cpp
// src/search/include/neomifes/search/search_service.h
namespace neomifes::search {

struct Query {
    std::u16string pattern;
    bool caseSensitive = true;
    bool wholeWord     = false;
    bool regex         = false;
};

struct Match {
    document::TextRange range;
    // Phase 5b2: capture groups 1..9 (RE2 1-indexed, capped at 9 - see
    // search_service.h), empty for a literal query. Consumed by
    // expandReplacementTemplate() below.
    std::vector<document::TextRange> groups;
};

class SearchService {
public:
    // 同期・ヘッドレス。マッチは行をまたいでよい (Phase 5b1)。
    // static:現時点でインスタンス状態を持たない。
    [[nodiscard]] static std::vector<Match> findAll(const document::Document& doc,
                                                     const Query&              query);
};

}  // namespace neomifes::search
```
- リテラル検索・正規表現検索いずれも**RE2の1本のコードパス**で実装(リテラルは`RE2::QuoteMeta()`でエスケープ)。`wholeWord`はRE2の`\b`(ASCII単語境界のみ、既存の`selectWordAt()`のCJK対応`classify()`とは非連携 — 既知の制限として明記)
- Document内部はUTF-16(`std::u16string`)だがRE2はUTF-8バイト列を対象とするため、新規`neomifes::util::toUtf8WithOffsets()`(`src/util/include/neomifes/util/utf8_convert.h`)でUTF-16→UTF-8変換とバイトオフセット→UTF-16オフセットの対応表を構築してからRE2へ渡す
- **Phase 5b1: `scanDocument()`が`pieceView()`で文書全体を1つの`std::u16string`バッファへ連結し、1回だけ検索するよう変更**(Phase 5aは1行ごとに`findAllInLine()`を呼んでいた)。これにより`\n`を含むリテラルクエリや`[\s\S]`等の文字クラスを使ったパターンが行をまたいでマッチできるようになった。`.`は`dot_nl`オプションを既定`false`のままにしているため引き続き改行をまたがない(明示的な指定が必要、一般的なエディタの慣習に合わせた意図的な選択)
- **`^`/`$`のセマンティクス維持:** RE2は`posix_syntax=false`(本プロジェクトのモード)では`^`/`$`が既定でテキスト全体の先頭/末尾にのみアンカーする。文書全体を1バッファ化したことでこの既定動作のまま使うと`^`/`$`が「行の先頭/末尾」ではなく「文書全体の先頭/末尾」を意味するように変わってしまうため、`buildPattern()`が生成する最終パターンの先頭に`"(?m)"`を付与し、Phase 5a時点の暗黙動作(`^`/`$`=行の先頭/末尾)を維持している。文書全体の先頭/末尾を明示したい場合はRE2の`\A`/`\z`を使う
- **既知のメモリスケーリング制約(Phase 5b1で許容):** 文書全体を1つのUTF-16バッファ+UTF-8変換+オフセット表へ連結するため、検索1回あたりのメモリ使用量が文書サイズに比例する(Phase 5aは最長1行分だけで済んでいた)。要件定義書の「10GB」目標とは緊張関係にあるが、下記§7.3のチャンク並列走査は依然未実装であり、この制約は実測が必要になった時点で改めてIssue化する方針
- 空バッファ(文書全体が空、または個々のマッチがゼロ幅で位置0)はRE2の空入力に対する`submatch[i].data()==NULL`という仕様上、オフセット計算を特別扱いする(`findAllInBuffer()`)

### 7.1'' 元スケッチ (Phase 5b3 以降のスコープ、未実装)
```cpp
class SearchService {
public:
    // 非同期 (Search Worker Pool) - Phase 5aでは同期のみ実装、UI配線で
    // 実際にブロッキング回避が必要になってから導入する
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

// 複数ファイル置換 Command (ファイル単位でトランザクション化) - ReplaceAllCommand
// 自体は Phase 5b2 で実装済み (§7.1''' 参照)、こちらはまだ未実装
class ReplaceInFilesCommand final : public core::ICommand {
public:
    ReplaceInFilesCommand(std::vector<std::filesystem::path>,
                          SearchService::Query, std::u16string replacement);
    // 各ファイルに ReplaceAllCommand を適用し、履歴を統合
};
```

### 7.1''' 置換 (Phase 5b2 実装)

`core::ReplaceAllCommand`(`src/core/include/neomifes/core/replace_all_command.h`)は`search::`を一切知らない疎結合設計 — `master_roadmap.md` §4.3のスケッチ(`ReplaceAllCommand`が`search::Match`を直接受け取る想定)から意図的に乖離した。理由: `search::`モジュールは`NEOMIFES_BUILD_TESTS`限定でしかビルドされておらず(実アプリ本体`NeoMIFES.exe`は未リンク、Phase 5aレビューのFix#4参照)、`core::`(常時ビルド対象)が`search::`へ依存すると、このガードを外しRE2/Abseilの取得を全ビルドで必須化する必要が生じる。Phase 5b3でFind bar UIが実際に`search::`を本体へリンクするまで、この結合は先送りする方針をユーザーに確認済み。

```cpp
// src/core/include/neomifes/core/replace_all_command.h
namespace neomifes::core {

// N個の独立したrange-replace編集をアトミックに1つのUndoステップとして適用。
// MultiCursorEditCommand(edit数=カーソル数を前提)は転用不可 - 置換のマッチ数は
// カーソル数と無関係。cursorsAfterExecute()/cursorsAfterUndo()はカーソルを
// 一切動かさず、construction時のスナップショットをそのまま返す。
class ReplaceAllCommand final : public ICommand {
public:
    ReplaceAllCommand(std::vector<PerCursorEdit> edits, std::vector<Cursor> cursorsBefore);
    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return "edit.replaceAll"; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterExecute() const override { return m_cursorsBefore; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterUndo() const override    { return m_cursorsBefore; }
    // ...
};

}  // namespace neomifes::core

// src/search/include/neomifes/search/replacement.h
namespace neomifes::search {

// $0/$&(全体マッチ)・$1-$9(キャプチャグループ、未参加なら空文字列)・$$(リテラル$)
// を展開。範囲外の$N・未知のエスケープ・末尾の$はリテラルのまま残す(エラーにしない
// - SearchService::findAll()の「不完全な正規表現は空結果」という既存方針と同じ)。
[[nodiscard]] std::u16string expandReplacementTemplate(std::u16string_view replacementTemplate,
                                                        const document::Document& doc,
                                                        const Match&               match);

}  // namespace neomifes::search
```

- `execute()`/`undo()`の累積オフセット適用アルゴリズムは新規`src/core/include/neomifes/core/cumulative_shift_edit.h`(`applyEditsWithCumulativeShift()`/`undoEditsDescending()`)に切り出し、`MultiCursorEditCommand`と`ReplaceAllCommand`の両方が共有する(既存`MultiCursorEditCommand`の挙動は無変更、既存6テストが無変更のままpassすることで確認済み)
- `search::Match`に`groups`フィールド(キャプチャグループ1-9、RE2の`NumberOfCapturingGroups()`を`std::min(9, ...)`でキャップ — `expandReplacementTemplate()`が`$1`-`$9`しか消費しないため)を追加。非参加の任意グループ(例: `(a)|(b)`が"b"にマッチした場合のグループ1)はマッチ開始位置での空レンジとして表現
- **本PRのスコープ外(意図的に延期):** Preview API(`master_roadmap.md` §4.3の`preview()`静的メソッド)・ベンチマーク・チャンク圧縮Undoは、UIの消費者がまだ無い状態(Phase 5b3のFind bar UI配線待ち)で作るのはCLAUDE.mdルール3の推測実装にあたるため見送り。`search::`と`core::`を実際に繋ぐグルーコード(`search::Match` → `core::PerCursorEdit`変換)もPhase 5b3まで書かない(現時点ではテストのみでパイプライン全体の合成可能性を証明、`tests/unit/core_replace_all_command_test.cpp`の`IntegrationFindAllExpandTemplateThenReplaceAllProducesExpectedDocument`参照)
- **既知の未解決コスト:** `BufferSnapshot::extract()`は毎回ピースリストを先頭から再走査するため、`ReplaceAllCommand`が数十万件規模のマッチを処理する場合はO(matches×pieces)になりうる(`docs/issues/replace_all_buffer_snapshot_extract_scaling.md`に記録、Phase 5b3で実際に大量マッチ経路ができてから再評価)

### 7.1'''' Find bar UI + マッチハイライト (Phase 5b3a 実装)

`ui::FindBar`(`src/ui/include/neomifes/ui/find_bar.h`)は本プロジェクト初の子HWND。`ui::MainWindow`と全く同じ分離方針(`neomifes::search`/`document`/`core`を一切知らない、Win32機構のみのクラス)を踏襲 — roadmap `master_roadmap.md` §5.3の`FindBarState`スケッチ(検索状態をFind bar自身の構造体に持たせる想定)から意図的に乖離し、`currentQuery`/`currentMatches`/`currentMatchIndex`は`src/app/main.cpp`の`wWinMain`スコープにローカル状態として置いた(`altCursorAnchor`と同じ寿命上の理由)。

```cpp
// src/ui/include/neomifes/ui/find_bar.h
namespace neomifes::ui {

struct FindBarConfig {
    // デバウンス済み(内部150ms)。呼び出し側が実際の検索を行いsetMatchCount()で返す。
    std::function<void(std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex)>
        onQueryChanged;
    std::function<void()> onFindNext;      // Enter/F3 (Find edit フォーカス時)
    std::function<void()> onFindPrevious;  // Shift+Enter/Shift+F3 (同上)
    std::function<void()> onClosed;        // Escape (同上)
};

class FindBar {
public:
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const FindBarConfig& config);
    void show() noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;
    void setMatchCount(std::size_t currentIndex, std::size_t count) noexcept;
    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;  // WM_COMMAND (EN_CHANGE)
    // ...
};

}  // namespace neomifes::ui
```

**設計上の要点:**
- **子HWND生成 + サブクラス化。** `WC_EDITW`を`CreateWindowExW`で生成し、`SetWindowSubclass`/`DefSubclassProc`(`<commctrl.h>`)でEnter/Escape/F3/Shift+F3/Ctrl+Fを横取りする。Win32はキーボード入力をフォーカスを持つHWNDへ直接ルーティングするため、Find editにフォーカスがある間は`MainWindow::wndProc`の`WM_KEYDOWN`ケースは発火しない — この横取りが唯一の手段
- **IME安全性(必須設計判断)。** `WM_IME_STARTCOMPOSITION`/`WM_IME_ENDCOMPOSITION`で変換状態を追跡し、日本語/中国語/韓国語の変換中はEnter/Escape/F3をFind barショートカットとして解釈せず`DefSubclassProc`(IME自身)へ委譲する。設計時のPlan agentレビューで指摘された必須修正 — 見落とすと日本語入力時にFind barが誤操作される
- **Alt+C/W/RはWM_SYSKEYDOWNで届く。** Altはシステムキー修飾子のため通常の`WM_KEYDOWN`では届かず、専用の`WM_SYSKEYDOWN`ハンドラで処理し、処理した3キーは既定の(存在しないシステムメニューを開こうとする)処理へフォールスルーさせないよう`return 0`する
- **デバウンスタイマーは発火後に`KillTimer`。** `EN_CHANGE`受信毎に`KillTimer`→`SetTimer(150ms)`で再武装し、タイマー発火時(`WM_TIMER`、対象HWNDがFind edit自身のためサブクラスプロシージャに届く)は`KillTimer`してから1回だけ`onQueryChanged`を呼ぶ(単純な`SetTimer`だけでは入力停止後も無限に再発火する)
- **DPI追従フォント。** `WC_EDIT`は既定では素のシステムフォントを使うため、`onParentResized(parentWidth, dpiScale)`が`CreateFontW`でDPIスケール済み`HFONT`を生成し`WM_SETFONT`で送る(`platform::GdiObjectHandle`で所有)
- `platform::WindowHandle`/`platform::GdiObjectHandle`(`handle_guard.h`、既存だが未使用のまま存在していた)をHWND/HFONT所有に採用
- **`ui::MainWindow`に`onCommand`フック追加。** Win32は子コントロールの通知(`EN_CHANGE`等)を常に親HWNDへ`WM_COMMAND`で送るため、既存の`onKeyDown`等と同じ`MainWindowConfig`フックパターンで追加(`main_window.h`)

**マッチハイライト描画:**
```cpp
// src/render/include/neomifes/render/render_pipeline.h (CursorVisualと同じファイル、
// roadmapが示唆した別ファイルmatch_visual.hからは意図的に乖離 - 既存CursorVisualの配置と一貫性を取るため)
struct MatchVisual {
    document::TextRange range;
    bool                isCurrent = false;  // F3で移動した「現在」のマッチ、別色で描画
};

class RenderPipeline {
public:
    void setMatchVisuals(std::vector<MatchVisual> matches) noexcept;
};
```
`drawMatchesOnLine()`/`drawMatchOnLine()`は既存`drawSelectionsOnLine()`/`drawSelectionOnLine()`と全く同じ構造(overlap計算+`HitTestTextPosition()`2回+`FillRectangle()`)。`drawVisibleLines()`内で`drawSelectionsOnLine()`の直前に呼ぶ(マッチが最背面、選択がその上、グリフが最前面)。`FrameState`に`matchVisuals`を追加(damage-tracking対象、`cursorVisuals`と同じ扱い)。

**CMakeガード解除。** `search::`を`NeoMIFES.exe`へ実際にリンクするため、`cmake/Dependencies.cmake`をRE2/Abseil専用に整理して無条件`include()`化、GoogleTest/benchmarkは新規`cmake/TestDependencies.cmake`へ分離し`NEOMIFES_BUILD_TESTS`ガード内に残した(単純に1ファイルを丸ごとガード解除すると、テスト専用の依存まで無条件フェッチされてしまうため)。`NEOMIFES_BUILD_TESTS=OFF`でも`NeoMIFES.exe`単独ビルドが成立することを確認済み。

**本PRのスコープ外(意図的に延期、Phase 5b3b で置換行を実装、残りは 5b3c 以降):** 置換行(Ctrl+H)配線 → **Phase 5b3b で実装済み(下記 §7.1''''' 参照)**。コマンドパレット(Ctrl+Shift+P)、Case/Word/Regexのクリック可能なトグルボタン(Alt+C/W/Rキーバインドのみ実装)、Grep(Phase 5c)は引き続き未実装。

**既知の未解決コスト:** `drawMatchesOnLine()`は可視行ごとに`m_matchVisuals`全件を線形走査するため、マッチ件数が数千〜数万件規模になると60fps目標に抵触しうる(`docs/issues/match_highlight_linear_scan_scaling.md`に記録、Phase 5c等で大量マッチ経路ができてから再評価)。

### 7.1''''' 置換行UI配線 (Phase 5b3b 実装)

Phase 5b2で実装済みの`core::ReplaceAllCommand`/`search::expandReplacementTemplate`(§7.1'''参照)は、`search::Match` → `core::PerCursorEdit`変換のグルーコードを意図的に書かず、`search::`が実際に`NeoMIFES.exe`へリンクされるタイミング(Phase 5b3a)まで延期していた。本フェーズがそのグルーコードを書く最初の実装。

```cpp
// src/ui/include/neomifes/ui/find_bar.h (Phase 5b3b 追加分)
struct FindBarConfig {
    // ... (onQueryChanged/onFindNext/onFindPrevious/onClosedは5b3aのまま)
    std::function<void(std::u16string_view replacementText)> onReplaceCurrent;  // Enter (Replace edit)
    std::function<void(std::u16string_view replacementText)> onReplaceAll;      // Ctrl+Enter (Replace edit)
};

class FindBar {
public:
    // ...
    void showWithReplace() noexcept;  // Ctrl+H: Find edit + Replace edit を表示、Find editへフォーカス
};
```

```cpp
// src/app/main.cpp (Phase 5b3b 追加分)
// currentQuery/currentMatches/currentMatchIndexを1つにまとめ、wireNormalMode以下の
// 呼び出し連鎖への引数を削減(wireNormalModeが12引数に達していたため)。
struct FindReplaceState {
    Query               currentQuery;
    std::vector<Match>  currentMatches;
    std::size_t          currentMatchIndex = 0;
};

void replaceCurrentMatch(std::u16string_view replacementTemplate, HWND hwnd, Document& document,
                         CommandDispatcher& dispatcher, FindReplaceState& state, /* ... */);
void replaceAllMatches(std::u16string_view replacementTemplate, HWND hwnd, Document& document,
                       CommandDispatcher& dispatcher, const SelectionModel& selectionModel,
                       FindReplaceState& state, /* ... */);
```

**設計上の要点:**
- **Find edit / Replace edit は同一サブクラスプロシージャを共有。** `FindBar::create()`が2つ目の`WC_EDITW`を生成し、同じ`&FindBar::subclassProc`/`dwRefData=this`で`SetWindowSubclass`する。`handleSubclassMessage`/`handleSubclassKeyDown`が既に受け取っている`HWND hwnd`引数だけで両エディットを区別する(サブクラス登録・メッセージルーティング機構を複製しない)
- **Tabキーによるフォーカス巡回は自前実装。** 本アプリのメッセージループ(`runMessageLoop()`)は`IsDialogMessageW`を使わない素の`GetMessageW`/`TranslateMessage`/`DispatchMessageW`ループのため、ダイアログなら無料で手に入るTabキー巡回が自動では効かない。`FindBar::cycleFocus(HWND)`が2要素間のトグルとして実装 — 2要素の巡回はA→B/B→Aが同一操作のため、Shift+Tabは意図的に未特別扱い
- **`replaceCurrentMatch()`のインデックス再取得。** 現在マッチを`core::ReplaceRangeCommand`で置換した後、`state.currentQuery`で再検索(`refreshMatches()`、`runFindQuery()`から検索実行+状態更新部分のみ抽出したヘルパー)し、置換前のインデックスを`std::min(replacedIndex, count-1)`でクランプして次に近いマッチへジャンプ。置換は1件ずつしかマッチ数を減らさないため、クランプで範囲外アクセスは起きない
- **`replaceAllMatches()`は再検索しない。** `core::ReplaceAllCommand`で全マッチを1回のUndoステップとして一括置換した後、ハイライトを単純にクリアする(`closeFindBar()`と同じ扱い)。置換後のテキストが同じクエリに再マッチして見えると「置換できていない」ように誤解されるため
- **マッチ順序の安全性。** `search::SearchService::findAll()`は「document order、非重複」を保証し(`search_service.h`)、`core`側の`applyEditsWithCumulativeShift()`(`cumulative_shift_edit.h`)は「ascending, non-overlapping」順を前提とするため、`state.currentMatches`をソートせずそのまま`PerCursorEdit`列に変換して`ReplaceAllCommand`へ渡せる
- **キャプチャグループ展開は編集前に実施。** `search::expandReplacementTemplate()`はドキュメントへの累積オフセット計算を持たない(§7.1'''の契約どおり)ため、`replaceCurrentMatch()`/`replaceAllMatches()`ともに編集適用前の(まだ変更されていない)ドキュメント状態に対して呼ぶ

**本PRのスコープ外(意図的に延期):** クリックできる「Replace」/「All」ボタン(Case/Word/Regexトグルと同じ簡略化方針、キーバインドのみ)。コマンドパレット(Ctrl+Shift+P、Phase 5b3c) → **Phase 5b3c で実装済み(下記 §7.1'''''' 参照)**。

### 7.1'''''' コマンドパレット (Phase 5b3c 実装)

`ui::CommandPalette`(`src/ui/include/neomifes/ui/command_palette.{h,cpp}`)は`ui::FindBar`を直接踏襲した設計だが、**異なる2種類のコントロール型**(`WC_EDITW` + `WC_LISTBOXW`)を同一サブクラス機構で扱う初めてのケースという点でFindBarのFind/Replace edit(同一型2つ)から一段複雑になる。

```cpp
// src/ui/include/neomifes/ui/command_descriptor.h
struct CommandDescriptor {
    std::u16string id;
    std::u16string title;
    std::u16string keybindingLabel;  // 表示専用
    std::function<void()> action;
};

// src/ui/include/neomifes/ui/command_palette_filter.h (ヘッダオンリー、find_navigation.hと同系統)
[[nodiscard]] std::vector<std::size_t> filterAndRankCommands(std::u16string_view query,
                                                              std::span<const CommandDescriptor> commands);

// src/util/include/neomifes/util/fuzzy_matcher.h
[[nodiscard]] std::optional<int> fuzzyMatchScore(std::u16string_view query,
                                                  std::u16string_view target) noexcept;

// src/ui/include/neomifes/ui/command_palette.h
class CommandPalette {
public:
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const CommandPaletteConfig& config,
                              std::vector<CommandDescriptor> commands);
    void show() noexcept;
    void hide() noexcept;
    // ...
};
```

**設計上の要点:**
- **フォーカスはクエリEditに固定し続け、リストボックスへは移さない。** VSCode実際のUXに合わせ、Up/Down/Enterはすべてクエリedit側のサブクラスで横取りし`LB_SETCURSEL`でハイライトのみ動かす。デバウンス無し、`EN_CHANGE`毎に同期的に`filterAndRankCommands()`を再実行(対象は最大数十件程度でroadmapの性能目標「500件で20ms」に対し十分余裕があるため、FindBarの150msデバウンスは不要と判断)
- **標準`WC_LISTBOX`は自身の`WM_LBUTTONDOWN`処理内で自分自身に`SetFocus`する。** これを放置すると結果行を1回クリックしただけでクエリeditからフォーカスが奪われ、以降Up/Down/Enter/Escapeが素のリストボックスの`DefWindowProc`に届いて無反応になる(設計時のPlan agentレビューで検出)。**対策としてリストボックスも同一パターンでサブクラス化し、`WM_LBUTTONDOWN`/`WM_LBUTTONDBLCLK`を`DefSubclassProc`に処理させた直後に`::SetFocus(m_hwndEdit)`でフォーカスを奪い返す。**
- **ダブルクリックでフォーカス奪回とコマンド実行が競合する落とし穴。** `WM_LBUTTONDBLCLK`の`DefSubclassProc`処理はネストした`SendMessage`で`LBN_DBLCLK`を親へ同期的に送出し、親の`handleCommand()`がその場で`action()`を実行し`hide()`する場合がある(例: `findBar.show()`でフォーカスが別の子HWNDへ移る)。この`DefSubclassProc`呼び出しの直後に無条件で`::SetFocus(m_hwndEdit)`すると、コマンドが直前に開いたばかりのUIからフォーカスを奪い返してしまう。**`isVisible()`を確認してから`SetFocus`する**ことで、コマンド実行によって既に閉じられていた場合はフォーカス奪回をスキップする(このセッション自身がPlan agentのレビュー後、実装トレース中に発見・修正した設計不備)
- **`LOWORD(wParam)`/`HIWORD(wParam)`によるコントロール判別はFindBarの`EN_CHANGE`判定と同じ規約。** 追加でマウス操作による選択変更は`m_selectedIndex`を経由しないため、`LBN_SELCHANGE`/`LBN_DBLCLK`受信時は`LB_GETCURSEL`で実際の選択位置を都度取得し同期する(キーボード操作は`m_selectedIndex`→`LB_SETCURSEL`、マウス操作は`LB_GETCURSEL`→`m_selectedIndex`の双方向設計)
- **ファジーマッチはASCII範囲のみの大文字小文字無視、貪欲最左マッチ。** VSCode等のDP最適スコアラーより意図的に簡略化(コマンド候補が最大数十件の定型英語文字列であるため、実用上の精度差は問題にならない判断)
- **登録6コマンドはすべて既存実装済みキーバインドの再露出。** Find/Find+Replace/Find Next/Find Previous/Undo/Redo — File Open/Save等の未実装機能はコマンドパレット用に新規実装しない(CLAUDE.mdルール3の推測実装回避、`buildCommandRegistry()`のコメント参照)

**本PRのスコープ外(意図的に延期):** サブメニュー、絵文字アイコン、最近使用ボーナス、検索履歴共有、Quick Open(Ctrl+P)・行ジャンプ(Ctrl+G) — roadmap v2.0の拡張項目でありUIの消費者/要件確定が別途必要なため。

### 7.1''''''' GrepService (Phase 5c1 実装)

`search::GrepService`(`src/search/include/neomifes/search/grep_service.h`)は複数ルート・複数ファイルを横断する同期検索。**既存`search::SearchService::findAll()`/`document::loadUtf8File()`を無改変のまま再利用するだけで実装でき、`search_service.{h,cpp}`への変更は一切不要だった。**

```cpp
// src/search/include/neomifes/search/grep_service.h
struct GrepQuery {
    std::vector<std::filesystem::path> roots;
    std::vector<std::u16string>        includeGlobs;  // ファイル名のみに util::globMatch() でマッチ
    std::vector<std::u16string>        excludeGlobs;  // includeGlobsと独立に常に適用、競合時はexclude優先
    Query                               query;          // 既存 search::Query をそのまま再利用
};

struct GrepMatch {
    std::filesystem::path path;
    document::LineNumber   line = 0;  // 0-based
    document::TextRange    columnRange;  // lineText先頭からの相対位置(絶対TextPosではない)
    std::u16string          lineText;    // 末尾の \n / \r は除去済み
};

class GrepService {
public:
    [[nodiscard]] static std::vector<GrepMatch> findAll(const GrepQuery& query);
};

// src/util/include/neomifes/util/glob_match.h (ヘッダオンリー宣言 + 小さい .cpp、fuzzy_matcher.hと同じ分割)
[[nodiscard]] bool globMatch(std::u16string_view pattern, std::u16string_view text) noexcept;
```

**設計上の要点:**
- **同期実装、`std::vector`を直接返す。** roadmap §5.5のスケッチ(Search Worker Pool、`std::function<void(GrepMatch)>`ストリーミングコールバック)から意図的に乖離 — 本コードベースには`std::thread`/`std::async`等の並行処理が一切存在せず、`search_service.h`が既に「UIが必要とするまで非同期化はしない」と明記していた方針をそのまま踏襲した。Phase 5c1にはまだUIが無いため、同じ理由で同期のままとした
- **ファイル読み込みは`document::loadUtf8File()`を1ファイルにつき1回呼ぶだけ。** BOM処理・サイズ上限・UTF-8検証は全て既存実装に委譲。読み込みに失敗したファイル(バイナリ含む、`LoadError::InvalidUtf8`)はそのファイルをスキップするだけで全体を失敗させない(grep/ripgrepが不読/バイナリファイルをスキップする一般的な挙動と同じ)
- **`GrepMatch::columnRange`は`lineText`先頭からの相対位置。** `GrepService`が読み込む`Document`は検索後に破棄される一時オブジェクトのため、`document::TextPos`の絶対オフセットは後続の利用者にとって無意味 — `lineText`に対して自己完結する相対レンジの方が有用という判断
- **ディレクトリ走査は`std::filesystem::recursive_directory_iterator`を非throwの`it.increment(ec)`で回す。** range-based forは内部でthrowする`operator++`を使うため使わない。`skip_permission_denied`を設定、`follow_directory_symlink`は既定OFFのままでシンボリックリンクループ対策も不要
- **存在しないルート・走査エラーはそのルート/ファイルをスキップするのみ。** ルートは実質的にユーザー入力(Grepダイアログの入力欄)というシステム境界だが、`findAll()`の戻り値は単純な`vector`でエラーチャネルを持たないため、1つの不正なルートが他のルートの結果まで消してしまう事態を避ける設計とした(ripgrep/grep -rと同じ考え方)
- **`globMatch()`はファイル名1コンポーネントのみを対象、`*`/`?`のみサポート。** ASCII範囲のみの大文字小文字無視(NTFS自体が大文字小文字を区別しないこと、`util::fuzzyMatchScore`の既存方針を踏襲)。アンカー付き全文マッチ(部分一致ではない)

**意図的にスコープ外とした項目(Phase 5cの後続サブフェーズへ):** `contextLines`(周辺行表示)、`GrepMatch`へのキャプチャグループ、`Mode::GrepResult`・結果ペインUI・`render_pipeline`へのマッチビジュアル配線・`main.cpp`のキーバインド配線、タグジャンプパーサ、検索履歴永続化。詳細は`master_roadmap.md` §5.5参照。

### 7.1'''''''' openDocumentAt (Phase 5c2 実装)

`neomifes::app::openDocumentAt()`(`src/app/include/neomifes/app/document_open.h`)は、実行中に任意のファイルを開いて現在の`Document`を差し替えるヘッドレス関数。Grep結果ペイン(5c3)・タグジャンプ(5c4)の共通前提として、両者に先行して独立サブフェーズで実装した(§5.5参照 — roadmapスケッチには無かった発見)。

```cpp
// src/app/include/neomifes/app/document_open.h
[[nodiscard]] std::optional<document::LoadError> openDocumentAt(
    const std::filesystem::path& path, std::optional<document::LineNumber> targetLine,
    std::optional<std::uint64_t> targetColumn, document::Document& document,
    core::CommandDispatcher& dispatcher, core::SelectionModel& selectionModel,
    core::Viewport& viewport, core::BookmarkManager& bookmarks,
    std::optional<document::TextPos>& altCursorAnchor,
    std::optional<document::TextPos>& rectangularAnchor,
    std::optional<std::uint32_t>&      freeCursorVirtualColumns);
```

**設計上の要点:**
- **`document::loadUtf8File()`でロードした結果を`document = std::move(*result->document)`でその場move-assignする。** `Document::operator=(Document&&) noexcept = default`は`document.h`に既存だったが、これが初めての実利用。`Document`はローカル変数として値で保持され(`wWinMain`)、`ExecutionContext`/`RenderPipeline`は`Document*`をポインタで保持するのみのため、move代入後もアドレスが不変であればダングリングにならない — この安全性は実装前にPlan agentによるレビューで3点(move代入operatorの存在・ポインタ保持であること・`RenderPipeline::refreshDocumentCacheIfStale()`が`version()`の等価比較であること)を個別にソース確認した上で確定させた
- **`neomifes_app_input`ライブラリ(`editor_input.h`と同じWin32/RenderPipeline非依存の層)に配置。** `main.cpp`の無名namespace関数として置くと、Phase 5c2時点ではまだ呼び出し元(UIトリガー)が無いためMSVC `/WX`+C4505(未参照ローカル関数)でビルド不能になることが設計段階で判明したため
- **ファイル切替に伴い旧ドキュメントに対してのみ意味を持つ状態を一括リセットする:** `core::CommandDispatcher::resetUndoHistory()`(新設、内部で`core::UndoStack::clear()`を呼ぶ — `UndoStack&`を直接公開せず`canUndo()`/`canRedo()`と同じ「狭い動詞を公開する」設計)、`core::BookmarkManager::clear()`(新設)、Alt-クリック/矩形選択アンカー、フリーカーソル仮想列(いずれも呼び出し元の`std::optional`参照引数を`reset()`)
- **`targetLine`/`targetColumn`は0始まり(`document::LineNumber`の既存慣習)。** `search::GrepMatch::line`/`columnRange`と同じ規約で、`Ctrl+G`の1始まり`ui::GotoTarget`規約とは意図的に異なる — この関数の実際の呼び出し元(5c3のGrep結果、5c4のタグジャンプ)は既に0始まりの位置を渡すため
- **範囲外の`targetLine`/`targetColumn`はクランプする(失敗にしない)。** `main.cpp`の`jumpToGotoTarget()`と同じ防御的規約
- **失敗時(`LoadError`)は`document`を含む一切の状態を変更しない。** `document::loadUtf8File()`の既存エラー分類をそのまま返す(将来のエラートースト等の消費者のために情報を保持)

**意図的にスコープ外とした項目(5c3/5c4側でmain.cppに追加):** `RenderPipeline::setBookmarkedLines({})`/`setMatchVisuals({})`・`FindBar::setMatchCount(0,0)`・`FindReplaceState::currentMatches.clear()`等のキャッシュ済みビジュアル状態のリセット。これらは`openDocumentAt()`から到達できない`main.cpp`側の状態であり、5c3/5c4が実際のUIトリガー(キーバインド)を配線する同一コミットでまとめて後始末する。

### 7.1''''''''' GrepBar / Grep結果ペインUI (Phase 5c3 実装)

`neomifes::ui::GrepBar`(`src/ui/include/neomifes/ui/grep_bar.h`)は`Ctrl+Shift+F`で開くGrep結果ペイン。既存`ui::CommandPalette`(WC_LISTBOX管理・フォーカス奪取対策)と`ui::FindBar`(2つのWC_EDITが1つのサブクラスを共有)の設計をそのまま組み合わせた構造で、新規のWin32サブクラス機構は不要だった。

```cpp
// src/ui/include/neomifes/ui/grep_bar.h
struct GrepBarConfig {
    std::function<void(std::u16string_view queryText, std::u16string_view folderText)> onRunQuery;
    std::function<void(std::size_t resultIndex)> onResultActivated;
    std::function<void()> onClosed;
};

class GrepBar {
public:
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const GrepBarConfig& config);
    void show() noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;
    void setResults(const std::vector<std::u16string>& rows) noexcept;
    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;
    // ...
};
```

**橋渡し用のヘッドレス純粋関数2つ**(`goto_line_parser.h`と同じパターン、`src/app/include/neomifes/app/`配下 — `ui::`層は`search::`/`document::`/`core::`を一切知らない既存原則を維持するため、`GrepBar`自身は`search::GrepMatch`を直接扱わない):

```cpp
// grep_query_builder.h
[[nodiscard]] std::optional<search::GrepQuery> buildGrepQueryFromInput(
    std::u16string_view queryText, std::u16string_view folderText);

// grep_result_formatting.h
[[nodiscard]] std::u16string formatGrepResultRow(const search::GrepMatch& match);
```

**設計上の要点:**
- **検索実行はEnterキーによる明示トリガーのみ。** キー入力ごとの自動再実行(Find bar式デバウンス)は不採用 — `GrepService::findAll()`はディレクトリ全体を舐める同期処理であり、単一ドキュメント内のインクリメンタル検索と異なりキー入力のたびに実行するとUIが固まるリスクがある(ユーザー確認済み)。`GrepBar`に`WM_TIMER`/`EN_CHANGE`処理は一切無い
- **クリック=選択のみ、ダブルクリック=ジャンプ+`hide()`。** `CommandPalette::runSelectedCommand()`と同型。単一クリックで即座に閉じる設計ではない(結果を眺めながら選ぶ操作性を優先)
- **`buildGrepQueryFromInput()`はファイルI/Oを一切行わない。** 存在しないフォルダは`GrepService::findAll()`側が既に静かにスキップする設計(Phase 5c1)のため、この関数は純粋計算のまま保てる。単一rootのみ構築(複数フォルダ入力は非対応)、include/exclude glob・Case/Whole word/Regexトグルは`GrepQuery`/`Query`のデフォルト値のまま(入力UIが無いため)
- **`formatGrepResultRow()`は`"{path}({line+1}): {lineText}"`形式、1始まり行番号。** `GrepMatch::line`自体は0始まりだが、`ui::parseGotoLineInput()`のCtrl+G表示慣習と揃えた
- **ジャンプ時、`main.cpp`の`jumpToGrepResult()`が`openDocumentAt()`(Phase 5c2)を呼んだ後、その関数自身が行わない後始末を実施する:** `RenderPipeline::setMatchVisuals({})`/`setBookmarkedLines({})`(`BookmarkManager::clear()`は`openDocumentAt()`内部で既に呼ばれるが、`RenderPipeline`側のキャッシュされたコピーは別物のため明示リセットが必要)、`FindBar::setMatchCount(0,0)`、`FindReplaceState::currentMatches.clear()`。5c2で意図的に据え置いていた後始末(§7.1''''''''参照)がこれで実装された
- **`Mode::GrepResult`のような集中モード管理enumは新設しなかった。** 本コードベースに`Mode`enumは元々存在せず、`FindBar`/`CommandPalette`/`GotoLineBar`と同じ「個々のオーバーレイが独立して`isVisible()`を持つ」規約(相互排他制御なし)をそのまま踏襲

**意図的にスコープ外とした項目:** フォルダピッカーダイアログ、include/exclude globの入力UI、Case/Whole word/Regexトグル、複数フォルダ入力、Grepヒットの`MatchVisual`エディタ本体ハイライト、`GrepMatch`へのキャプチャグループ・「結果内で置換」、ジャンプ失敗時のエラートーストUI、検索履歴永続化、タグジャンプパーサ(5c4)。詳細は`master_roadmap.md` §5.5参照。

### 7.1'''''''''' タグジャンプ (Phase 5c4 実装)

`neomifes::util::parseTagJumpReference()`(`src/util/include/neomifes/util/tag_jump_parser.h`)は、`F12`キー押下時にカーソル行のテキストから MSVC コンパイラ診断出力の位置表記(`path(line)` / `path(line,column)`)を探索するヘッドレス純粋関数。`ui::goto_line_parser.h`とは異なり任意の大きな文字列に埋め込まれたパターンを探索する処理のため、`util::globMatch()`/`util::fuzzyMatchScore()`と同じ`neomifes::util`名前空間に配置した。

```cpp
// src/util/include/neomifes/util/tag_jump_parser.h
struct TagJumpReference {
    std::u16string                path;    // 生のパス文字列(未解決)
    std::uint64_t                 line = 0;  // 1始まり
    std::optional<std::uint64_t>  column;    // 1始まり、nullopt=行頭のみ
};

[[nodiscard]] std::optional<TagJumpReference> parseTagJumpReference(
    std::u16string_view lineText) noexcept;
```

```cpp
// src/app/include/neomifes/app/tag_jump.h
[[nodiscard]] std::filesystem::path resolveTagJumpPath(
    std::u16string_view rawPath, const std::filesystem::path& baseDir);
```

**設計上の要点:**
- **括弧形式(MSVC流)のみサポート、コロン形式(GCC/Clang流 `path:line:column`)は非対応。** Windows絶対パス自体がドライブレター直後にコロンを含む(`C:\...`)ため曖昧性解消の複雑さに見合う需要が無いという判断
- **パーサはファイルI/Oを一切行わない。** 見つけたパス文字列の存在確認はせず、後続の`openDocumentAt()`の静かな失敗に委ねる(誤検出しても実害が無い設計)
- **「ファイルパスらしさ」ヒューリスティック(既知拡張子のホワイトリストではない):** 最後の`\`/`/`より後ろを「ファイル名部分」とし、そこに`.`があり後ろが1〜8文字のASCII英数字であることを要求。`if (x)`・`Foo(bar)`のような無関係な括弧式を安価に除外しつつ、既知拡張子の維持コストを避ける
- **`resolveTagJumpPath()`は`std::filesystem::current_path()`を内部で呼ばず、呼び出し元(`main.cpp`)が渡す`baseDir`を使う純粋関数。** ヘッドレステスト可能にするための設計。相対パスの解決基準を「現在開いているファイルのディレクトリ」ではなく`current_path()`にしたのは、本コードベースがそれを追跡していないという制約ゆえではなく、MSVC/MSBuildのビルドエラー出力が常にビルド起動ディレクトリからの相対パスであり、エディタで偶然開いているファイルのディレクトリとは無関係という意味論的な正しさに基づく判断
- **`main.cpp`の`handleTagJumpKey()`(F12)は`jumpToGrepResult()`(5c3)と同じ後始末シーケンスを実施。** カーソル行を`document.offsetToLine()`→`lineToOffset()`→`snapshot()->extract()`の既存3段イディオムで取得し、パーサ→`resolveTagJumpPath()`→`openDocumentAt()`(5c2)→成功時は`RenderPipeline::setMatchVisuals({})`/`setBookmarkedLines({})`・`FindBar::setMatchCount(0,0)`・`FindReplaceState::currentMatches.clear()`・`syncRenderStateAndInvalidate()`
- **`handleKeyDownEvent()`の`document`引数を`const Document&`から`Document&`へ拡張し、`altCursorAnchor`/`rectangularAnchor`を新規引数として追加。** `openDocumentAt()`が必要とするため。`wireNormalMode()`自体の引数は変化していない(両方とも既存の引数、`cfg.onKeyDown`ラムダのキャプチャに追加するのみ)

**意図的にスコープ外とした項目:** コロン形式の位置表記、コマンドパレットへの登録(F12キーのみ)、マッチ無し時のユーザーフィードバック、複数ルート/ワークスペース対応のパス解決、ジャンプ先の`MatchVisual`ハイライト、パスに空白を含むケースの正確な解析、既知拡張子のホワイトリスト維持。詳細は`master_roadmap.md` §5.5参照。

### 7.1''''''''''' 検索履歴永続化 (Phase 5c5 実装)

`neomifes::core::SearchHistory`(`src/core/include/neomifes/core/search_history.h`)は、Find bar(Ctrl+F)とGrepダイアログ(Ctrl+Shift+F)で共有する検索パターン履歴(直近50件、MRU順)を`%APPDATA%\NeoMIFES\search_history.json`へ永続化するヘッドレスクラス。コマンドパレットは対象外(§5.5参照 — コマンド名とテキスト検索パターンは意味的に別種のデータ)。

```cpp
// src/core/include/neomifes/core/search_history.h
class SearchHistory {
public:
    [[nodiscard]] static SearchHistory loadFrom(const std::filesystem::path& path);
    void record(std::u16string_view query);
    [[nodiscard]] const std::vector<std::u16string>& entries() const noexcept;
    [[nodiscard]] std::optional<std::u16string_view> older(std::u16string_view currentText) const noexcept;
    [[nodiscard]] std::optional<std::u16string_view> newer(std::u16string_view currentText) const noexcept;
    void saveTo(const std::filesystem::path& path) const;
private:
    static constexpr std::size_t kMaxEntries = 50;
    std::vector<std::u16string> m_entries;
};
```

```cpp
// src/platform/include/neomifes/platform/app_data_dir.h
[[nodiscard]] std::optional<std::filesystem::path> resolveAppDataDir();
```

**設計上の要点:**
- **`older()`/`newer()`はステートレス。** 呼び出し側(FindBar/GrepBar)が「今どのインデックスを辿っているか」を保持する必要が無い — 現在edit欄に表示されているテキストを引数に渡すだけで、`m_entries`内でそのテキストを値検索し隣接エントリを返す自己修正的な設計。`currentText`が既存エントリと一致すればその1つ古い/新しいエントリを、一致しなければ(未入力・ユーザーが新規入力中)`older()`は最新エントリ(`entries()[0]`)から開始する。最古/最新エントリでの呼び出しはクランプ(ラップアラウンドしない、`GrepBar::moveSelection()`と同じ規約)
- **履歴を辿るキーはCtrl+Up/Ctrl+Down。** `GrepBar`(いずれの入力欄でも)と`CommandPalette`が素のUp/Downを既に`moveSelection(±1)`(リスト選択)に割り当て済みのため、衝突しないCtrl修飾版を採用(本コードベースでどこにも未使用であることをgrep確認済み)
- **記録タイミングは`FindBar`の`onFindNext`/`onFindPrevious`、`GrepBar`の`onRunQuery`のみ。** `navigateToMatch()`の他の呼び出し経路(document-focused F3、コマンドパレット経由の「Find Next」)では記録しない — `record()`自身が既存エントリをMRU先頭へ移動する(重複させない)ため、後から同じクエリが再記録されても無害なno-opになることを利用し、`navigateToMatch()`の3箇所の呼び出し元全てへ`SearchHistory&`を通すシグネチャ変更を避けた
- **保存はプロセス終了時(`runMessageLoop()`復帰後)の1回のみ。** 検索のたびに毎回ディスクへ書かない設計。クラッシュ時は当該セッションの新規追加のみが失われる許容可能なデータロス
- **新規外部依存`nlohmann/json`(v3.11.3、ADR-013)をJSON入出力に採用。** UTF-16⇔UTF-8境界変換は新規実装せず既存`neomifes::encoding::encode()`/`decode()`(Phase 6a〜6d)を再利用した。JSON形状は`{"version": 1, "entries": ["query1", ...]}`(MRU順、index 0が最新)。`loadFrom()`は存在しないパス・壊れたJSON・バージョン不一致のいずれでも空の`SearchHistory`を返す非throw設計(`nlohmann::json::parse(str, nullptr, false)`の`is_discarded()`を利用)、`saveTo()`は書き込み失敗をベストエフォートで無視する(いずれも通知UIが本コードベースに存在しないため)
- **新規`platform::resolveAppDataDir()`は`SHGetKnownFolderPath(FOLDERID_RoamingAppData, ...)`の薄いラッパー(`clipboard.h`と同じパターン)。** ディレクトリが無ければ`create_directories()`で作成し、`%APPDATA%\NeoMIFES`を返す。解決失敗時はnullopt(呼び出し側はメモリのみで動作する既定へフォールバック — `wWinMain`は`searchHistoryPath`がnulloptなら`saveTo()`自体を呼ばない)
- **`FindBar::setQueryText()`/`GrepBar::setQueryText()`はプログラムからのテキスト設定用に新設。** `FindBar`側は`SetWindowTextW`→`EN_CHANGE`→既存デバウンス機構が自然に発火し、150ms後に実際に検索が再実行される(「履歴から呼び出したら検索も走る」という意図した挙動に追加配線不要)。`GrepBar`側は検索を自動実行しない(5c3の「Enter明示実行」方針を維持、クエリeditのみが対象でフォルダeditは対象外)

**意図的にスコープ外とした項目:** コマンドパレットでの履歴共有、履歴のクリア/削除UI、複数エントリの同時削除、`search_history.json5`(JSON5固有機能はこの用途では不要と判断しプレーンJSONを採用)。詳細は`master_roadmap.md` §5.5参照。

### 7.2 アルゴリズム
| 種別 | アルゴリズム |
|---|---|
| 通常 (case-sensitive, ASCII) | Phase 5aでは未実装。RE2経由の1本のコードパスで代替 (下記) — Boyer-Moore-Horspool + AVX2 pcmpeqbは計測してから要否判断する将来の最適化候補として温存 |
| 通常 (Unicode) | 同上 |
| 正規表現 | **RE2 採用・実装済み** (ADR-002、Phase 5a) |
| Grep (複数ファイル) | 未実装 (Phase 5b以降)。Worker Pool、ファイル単位 memory-map |

### 7.3 巨大ファイル検索
- **未実装 (Phase 5b2以降)。** Phase 5b1で`pieceView()`ベースのO(文書長)走査(§7.1'参照)に改善したが、Piece Tableのチャンク単位**並列**走査は依然未実装。加えてPhase 5b1は文書全体を1バッファへ連結する設計のため、検索1回あたりのメモリ使用量が文書サイズに比例するようになった(§7.1'の既知の制約参照)
- 実測 (Phase 5a時点): 20万行(約10MB相当)の合成ログ風ドキュメントに対する`findAll()`をRelease構成でgoogle-benchmark実測した結果、約60〜66ms(スパースマッチ/無マッチいずれも同程度)。単純換算で約150MB/s相当
- 実測 (Phase 5b1、同一ベンチマークで再測定): 約33〜39ms(スパースマッチ/無マッチいずれも同程度)。単純換算で約260〜300MB/s相当 — 1行ごとのUTF-8変換・RE2呼び出しの繰り返しオーバーヘッドが無くなったことで、単一ピースの合成ドキュメントに対しては改善した。ただし既存ベンチマークは依然として単一ピース文書のみが対象であり、実際の編集で発生する多ピース文書での挙動(Phase 5aで修正したO(pieces)問題が再発しないこと)は本ベンチマークでは検証できていない — 要件定義書§5「検索: 数GBファイルでも高速」の達成には、この同期・単一スレッドかつメモリ比例の実装のままでは数GB規模で数十秒+相応のメモリを要する計算になり、非同期化・チャンク並列化(本節の未実装項目)が実際に必要になることを示すデータであることに変わりはない

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

> **凍結された歴史的記録:** 本節はPhase 6着手前(basic_design.md起草時)に書かれた速記スケッチであり、§9.3で確定した実装とは判定アルゴリズム(§9.1、Phase 6aでは未着手)・不正シーケンス処理方針(§9.2、「U+FFFD置換」ではなく「拒否」を採用)の両方で内容が食い違う。最新情報は§9.3を参照すること。

### 9.1 判定アルゴリズム (未実装、Phase 6c予定)
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
BOM判定部分(1.)は`encoding::detectBom()`としてPhase 6aで既に実装済み — §9.3参照。2.以降(ASCII/UTF-8妥当性/ISO-2022-JP/Shift-JIS/EUC-JP判定)はPhase 6c(自動判定)で実装予定、未着手。

### 9.2 変換 (Phase 6a実装で確定した内容は§9.3参照)
- 内部表現は **UTF-16LE (`char16_t`)** — 確定、変更なし
- 変換テーブルはコンパイル時定数配列 (constexpr) — BOMバイト列は`constexpr std::array`で確定、コードポイント変換テーブルはUnicodeファミリーには不要(ビット演算のみ)だったため未使用。Shift-JIS/EUC-JP(Phase 6b)では変換テーブルが必要になる見込み
- **不正シーケンスの処理方針は「`U+FFFD`に置換」から「拒否(エラーを返す)」に変更した。** §9.3参照

### 9.3 実装後の確定事項/変更点 (2026-07-20、Phase 6a完了)

`neomifes::encoding`(`src/encoding/include/neomifes/encoding/encoding.h`)にUnicodeファミリー(UTF-8/UTF-8 BOM/UTF-16 LE/BE/UTF-32 LE/BE、BOM有無で計10種)の`decode()`/`encode()`/`detectBom()`を実装。

```cpp
enum class Encoding {
    Utf8, Utf8Bom, Utf16Le, Utf16LeBom, Utf16Be, Utf16BeBom,
    Utf32Le, Utf32LeBom, Utf32Be, Utf32BeBom,
    // ShiftJis / EucJp / Iso2022Jp は Phase 6b で追加。
};

enum class DecodeError { InvalidSequence, TruncatedSequence };

[[nodiscard]] std::variant<std::u16string, DecodeError> decode(
    std::span<const std::byte> bytes, Encoding encoding);
[[nodiscard]] std::vector<std::byte> encode(std::u16string_view text, Encoding encoding);

struct BomDetection { Encoding encoding; std::size_t bomLength; };
[[nodiscard]] std::optional<BomDetection> detectBom(std::span<const std::byte> bytes) noexcept;
```

**設計上の要点(roadmap/§9.1-9.2のスケッチからの乖離とその理由):**
- **不正シーケンスは`U+FFFD`置換ではなく拒否する。** §9.2のスケッチは寛容な「replace and continue」方針だったが、`ui::parseGotoLineInput()`/`util::parseTagJumpReference()`が既に確立していた「曖昧な入力は拒否する」規約に揃えた。寛容モードは将来必要になった時点で追加できる(既存の`decode()`契約を壊さない追加的な変更のため)
- **クラスベースの`Encoder`/`EncodingDetector`は採用せず、`neomifes::encoding`名前空間直下の自由関数にした。** 状態を持たない純粋関数群にクラスの皮を被せる理由が無い(`util::globMatch()`/`util::parseTagJumpReference()`と同じ判断)
- **`decode()`は`hint`引数を取らない。** `detectBom()`(BOM検出のみ)と`decode()`(指定Encodingでのデコードのみ)を分離し、`detectBom()`の戻り値をそのまま`decode()`に渡せる設計にした — Phase 6c(自動判定)が追加する非BOM判定手段も同じ`decode()`の入口を共有できる
- **`util::toUtf8WithOffsets()`(Phase 5a)を再利用せず独立実装。** ENCODE方向のみ・RE2用オフセット表構築必須という設計はコーデック用途に合わない。`document::OriginalBuffer`の内部UTF-8検証ロジックと合わせ、本コードベースには用途ごとに独立したUTF-8実装が複数存在する(意図的)
- **`document::loadUtf8File()`/`OriginalBuffer`(mmap+遅延デコード、Phase 2b3)への統合は行っていない。** UTF-8専用に深く結合した既存機構の一般化は独立した大きなサブフェーズ(6d以降)になる見込み

**意図的にスコープ外とした項目(Phase 6の後続サブフェーズへ):** Shift-JIS/EUC-JP/ISO-2022-JP(6b)、3段階自動判定の2.以降(6c、BOM判定のみ6aで完成済み)、行末コード判定・`DecodeResult`統合戻り値、Document/OriginalBufferへの統合、10GB mmap遅延デコードの一般化、メニューバー・ステータスバーUI、Direct Storage API検討。詳細は`master_roadmap.md` §6参照。

### 9.4 実装後の確定事項/変更点 (2026-07-20、Phase 6b1完了)

`Encoding`enumへ`ShiftJis`(CP932)/`EucJp`(CP20932)を追加。新規`neomifes::platform::codepage_convert`(`src/platform/include/neomifes/platform/codepage_convert.h`)がWin32のネイティブコードページ変換を薄くラップし、`neomifes::encoding`はこれを呼び出すのみで自前のJIS X 0208対応表を持たない。

```cpp
// src/platform/include/neomifes/platform/codepage_convert.h
namespace neomifes::platform {

enum class CodepageConvertError { InvalidSequence, UnmappableCharacter };

[[nodiscard]] std::variant<std::u16string, CodepageConvertError>
convertToUtf16(std::span<const std::byte> bytes, unsigned codepage);

[[nodiscard]] std::variant<std::vector<std::byte>, CodepageConvertError>
convertFromUtf16(std::u16string_view text, unsigned codepage);

}  // namespace neomifes::platform

// src/encoding/include/neomifes/encoding/encoding.h (拡張分のみ)
enum class Encoding { /* ...既存10種... */ ShiftJis, EucJp /* Iso2022Jp は6b2で追加 */ };
enum class EncodeError { UnmappableCharacter };
[[nodiscard]] std::variant<std::vector<std::byte>, EncodeError> encode(
    std::u16string_view text, Encoding encoding);  // 6aから戻り値型変更
```

**設計上の要点:**
- **JIS X 0208対応表の自前実装は行わなかった。** 数千文字規模のUnicode⇔JIS対応表を記憶から手打ちで生成するのはCLAUDE.mdルール3(推測実装をしない)に反すると判断し、Win32の`MultiByteToWideChar`/`WideCharToMultiByte`(Microsoft保守の権威ある変換表)をラップする設計にした。既存`src/platform/clipboard.h`(Win32機能の薄いラッパー)と同じパターン
- **`WC_ERR_INVALID_CHARS`はCP932/20932で使用できないことを実機検証で確認した。** `GetLastError()`が`ERROR_INVALID_FLAGS`を返す(decode方向の`MB_ERR_INVALID_CHARS`は問題なく機能した、非対称な制約)。代わりに`WC_NO_BEST_FIT_CHARS`+`lpUsedDefaultChar`出力引数の組み合わせで、既定文字への曖昧な置換が発生した場合をエラー扱いにする設計にした(`codepage_convert.cpp`参照)
- **`encode()`の戻り値を`std::vector<std::byte>`から`std::variant<std::vector<std::byte>, EncodeError>`へ変更(6a APIの破壊的変更)。** Shift-JIS/EUC-JPは非全域関数(JIS X 0208に無い文字、例えば絵文字は表現不可能)なため。6a完了時点で呼び出し元がテストのみだったため低リスクと判断
- **ISO-2022-JPは6b2へ分離。** `WC_ERR_INVALID_CHARS`のISO-2022系コードページ(50220/50221/50222)対応状況が未検証であることと、エスケープシーケンスによるモード切替という別種の構造を持つことが理由

**テスト:** `tests/unit/platform_codepage_convert_test.cpp`(新規) — 既知バイト列(「あ」「亜」)による外部真実性テストを中心に構成、自己ラウンドトリップのみでは検出できない対称的な誤りを防ぐ。`tests/unit/encoding_encoding_test.cpp`にShiftJis/EucJpを追加したラウンドトリップ・エラー系テストを拡張。

**意図的にスコープ外とした項目:** ISO-2022-JP(6b2)、3段階自動判定(6c)、Document/OriginalBufferへの統合(6d以降)。詳細は`master_roadmap.md` §6参照。

### 9.5 実装後の確定事項/変更点 (2026-07-21、Phase 6c1完了)

新規`detectEncoding()`を`neomifes::encoding`に追加。`detectBom()`/`decode()`の合成のみで構成する。

```cpp
[[nodiscard]] std::optional<Encoding> detectEncoding(std::span<const std::byte> head) noexcept;
```

**アルゴリズム:** ①`detectBom()`が検出できればそのEncoding、②空またはUTF-8として`decode()`が成功すればUtf8、③`decode(head, ShiftJis)`/`decode(head, EucJp)`のうち片方だけが成功すればそのEncoding、④両方成功する場合は0x81-0x9F範囲のバイト有無で判定(Shift-JIS優先マーカ、両方失敗する場合と合わせて`nullopt`)。

**設計上の要点:**
- **新規の低レベルバイト走査コードはほぼ書いていない。** 既存`decode()`の成功/失敗をそのまま判定オラクルとして再利用し、roadmapが記述する「Shift-JIS第1バイト範囲0x81-0x9F...を優先マーカとして使用」は「両方成功する場合のタイブレーカー」としてのみ最小限残った
- **Phase 6b1で見落としていたバグを本フェーズのテスト作成中に発見・修正した: Windows CP932/CP20932は一部の未割当バイトをC1制御コード(U+0080-U+009F)へ黙って直接マッピングし、`MB_ERR_INVALID_CHARS`指定下でも拒否しない(未文書化)。** 具体的にはShift-JISの単独`0x80`、EUC-JPの`0x80-0x9F`のほぼ全域(SS2シフトバイト`0x8E`単体を除く)。`decodeLegacyCodepageBody()`にデコード結果のC1範囲出力を拒否する後処理を追加して修正した。`platform::convertToUtf16()`自体(汎用Win32ラッパー)は変更せず、JIS固有のこの業務ルールは`encoding.cpp`側に置いた
- **Shift-JIS/EUC-JPの2バイト表現域(0xA1-0xFE×0xA1-0xFE)はほぼ全域が両コーデックで同時に有効になりうることを実機検証で確認した。** EUC-JP第2バイトが0xFD/0xFEの場合のみ確定的にEUC-JP判別可能(Shift-JISのDBCS第2バイト有効範囲は最大0xFC)。それ以外の大半のケース(「あ」`A4 A2`等)は真に曖昧で、roadmapのN-gramモデル(Stage 3、本フェーズはスコープ外)が本来解決すべき領域のため`nullopt`を返す
- **ISO-2022-JP検出は実装しなかった。** 着手前の実機検証で、ISO-2022系コードページ(50220/50221/50222)がWin32レベルで厳格な入力検証を一切サポートしないことが判明(`MB_ERR_INVALID_CHARS`/`WC_ERR_INVALID_CHARS`/`WC_NO_BEST_FIT_CHARS`いずれも`ERROR_INVALID_FLAGS`、有効な`dwFlags=0`はエスケープシーケンス異常をPUA文字へ静かに置換)。Phase 6b2として独立させたまま保留

**テスト:** `tests/unit/encoding_encoding_test.cpp`に`DetectEncodingTest`スイート(9件)・C1制御コード拒否の回帰テスト(`DecodeErrorTest`2件)を追加。

**意図的にスコープ外とした項目:** ISO-2022-JP検出(6b2)、N-gramモデルによる曖昧ケースの確信度算出、行末コード判定(6c2以降)、Document/OriginalBufferへの統合(6d以降)。詳細は`master_roadmap.md` §6参照。

### 9.6 実装後の確定事項/変更点 (2026-07-21、Phase 6c2完了)

新規`detectLineEnding()`を`neomifes::encoding`に追加。

```cpp
enum class LineEnding { Crlf, Lf, Cr, Mixed };
[[nodiscard]] std::optional<LineEnding> detectLineEnding(std::u16string_view text) noexcept;
```

**設計上の要点:**
- **生バイト列ではなく`decode()`済みのUTF-16文字列を走査する。** roadmapスケッチは生バイト列走査であるかのように読めるが、UTF-16では`\n`(U+000A)が2バイト表現になるため、生バイト単位の走査ではUTF-16入力に対して誤検出/検出漏れが起こる。呼び出し順序は`detectEncoding()`→`decode()`→`detectLineEnding(decodedText)`という合成
- **「混在」は1件でも異なる規約が混じればMixed。** 少数派を多数派へ黙って丸めると、roadmap §6.3が意図する「UIで警告」目的を果たせないため
- 64KBサンプリング上限は内部で強制しない(`detectEncoding(head)`と同じ「呼び出し側が渡す範囲を全て走査する」設計)

**テスト:** `tests/unit/encoding_encoding_test.cpp`に`DetectLineEndingTest`スイート(9件)を追加。

**意図的にスコープ外とした項目:** Document/OriginalBufferへの統合(6d以降)、実ファイル読込時の呼び出し配線。詳細は`master_roadmap.md` §6参照。

### 9.7 実装後の確定事項/変更点 (2026-07-21、Phase 6b2完了)

`Encoding`enumへ`Iso2022Jp`(CP50220のみ)を追加。CP50220は`dwFlags=0`以外を一切受け付けないため、他コードページで確立した厳格変換パターンが使えず、専用の寛容変換レイヤーと2段構えの検証ロジックが必要になった。

```cpp
// src/platform/include/neomifes/platform/codepage_convert.h (拡張分)
namespace neomifes::platform {

// CP50220はdwFlags=0のみ有効(MB_ERR_INVALID_CHARS等の厳格フラグ、
// lpDefaultChar/lpUsedDefaultCharの個別指定すら ERROR_INVALID_FLAGS /
// ERROR_INVALID_PARAMETER になる)。decode方向は不正シーケンスを
// Private Use Areaへ、encode方向は不可能文字を'?'へ黙って置換する
// 寛容モードしか存在しない - neomifes::encoding側が両方向とも
// 自前で妥当性を検証する。
[[nodiscard]] std::variant<std::u16string, CodepageConvertError>
convertToUtf16Lenient(std::span<const std::byte> bytes, unsigned codepage);

[[nodiscard]] std::variant<std::vector<std::byte>, CodepageConvertError>
convertFromUtf16Lenient(std::u16string_view text, unsigned codepage);

}  // namespace neomifes::platform

// src/encoding/src/encoding.cpp (無名namespace、拡張分のみ)
[[nodiscard]] std::variant<std::u16string, DecodeError> decodeIso2022JpBody(
    std::span<const std::byte> bytes, unsigned codepage);  // PUA範囲(U+E000-U+F8FF)を拒否
[[nodiscard]] std::variant<std::vector<std::byte>, EncodeError> encodeIso2022JpBody(
    std::u16string_view text, unsigned codepage);  // EUC-JP厳格encodeを可否オラクルとして先に確認
```

**設計上の要点:**
- **decode方向の不正入力検知は、デコード結果にUnicode私用領域(U+E000-U+F8FF)が含まれるかどうかで行う。** `dwFlags=0`の寛容モードは不正なエスケープシーケンス/不正なku-tenペアをエラーにせずPUAへ黙って置換する(実機観測: `U+F8F0`/`U+F8F3`)。正当なISO-2022-JPコンテンツがPUAへデコードされることは無いため安全な判定条件になる。6c1で確立したC1制御コード拒否と同じ「後処理での範囲チェック」パターンを踏襲
- **encode方向は「置換の検知不能」問題をEUC-JP(CP20932)の厳格encodeを代理可否オラクルとして使うことで回避した。** WindowsがCP50220とCP20932を共に同一の文字集合(JIS X 0208-1990 & 0212-1990)として文書化していることを根拠に、`platform::convertFromUtf16(text, 20932)`が失敗すれば`EncodeError::UnmappableCharacter`を即座に返し、実際のCP50220寛容encodeは呼ばない。**既知の制約:** CP20932とCP50220の文字集合が理論上完全一致しない可能性(JIS X 0212のどちらかにのみ実装されている稀な文字)は未対処 — 発生しても安全側(誤って`UnmappableCharacter`)に倒れるため許容
- **`lpDefaultChar`/`lpUsedDefaultChar`を個別に(片方だけ)指定しても`ERROR_INVALID_PARAMETER`になることを実機検証で確認した。** 6b1/6c1で判明していた「厳格フラグ全滅」に加えて、独自センチネル値注入による置換検知という代替戦略も塞がれていることが確定した
- ISO-2022-JP検出(`detectEncoding()`がエスケープシーケンスを認識すること)は本フェーズでも未実装のまま

**テスト:** `tests/unit/platform_codepage_convert_test.cpp`に既知バイト列(「あ」`1B 24 42 24 22 1B 28 42`・「亜」`1B 24 42 30 21 1B 28 42`)による外部真実性テスト・ASCII単体/混在ラウンドトリップ・不正ku-tenペアのPUA変換確認・絵文字の'?'置換確認を追加(8件)。`tests/unit/encoding_encoding_test.cpp`の`kAllEncodingsWithLegacy`にIso2022Jpを追加、`DecodeErrorTest`/`EncodeErrorTest`にIso2022Jp向けケースを追加。

**意図的にスコープ外とした項目:** CP50221/50222(半角カタカナ拡張)、ISO-2022-JP検出(`detectEncoding()`)、Document/OriginalBufferへの統合(6d以降)。詳細は`master_roadmap.md` §6参照。

### 9.8 実装後の確定事項/変更点 (2026-07-21、Phase 6d完了)

`OriginalBuffer::openMemoryMapped()`をEncoding引数対応に汎化し、新規`document::loadFile()`で自動判定込みの多エンコーディング読込を実現。roadmap §6全体が完了。

```cpp
// src/document/include/neomifes/document/original_buffer.h (拡張分)
namespace neomifes::document {

// Utf8/Utf16Le/Utf16Be/Utf32Le/Utf32Beのみ有効(バイト単位で文字境界が
// 構造的に分かるエンコーディング)。ShiftJis/EucJp/Iso2022Jpを渡すのは
// 呼び出し側の契約違反 - loadFile()がfromU16String()側へルーティングする。
[[nodiscard]] static std::variant<std::shared_ptr<const OriginalBuffer>, OriginalBufferError>
    openMemoryMapped(const std::filesystem::path& path, std::uint64_t byteOffset,
                      encoding::Encoding encoding);

}  // namespace neomifes::document

// src/document/include/neomifes/document/file_loader.h (新設)
namespace neomifes::document {

// detectBom()→detectEncoding()→Utf8フォールバックの順で自動判定し、
// mmap遅延デコード(Group A)または一括デコード(Group B)へ振り分ける。
// loadUtf8File()自体は無変更(GrepServiceの既存契約を壊さないため)。
[[nodiscard]] std::variant<LoadResult, LoadError>
loadFile(const std::filesystem::path& path,
         std::uint64_t                maxBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL);

}  // namespace neomifes::document
```

**設計上の要点:**
- **mmap+遅延デコードの一般化対象(Group A)はUTF-8・UTF-16 LE/BE・UTF-32 LE/BEのみ。Shift-JIS/EUC-JP/ISO-2022-JP(Group B)は既存の`OriginalBuffer::fromU16String()`による一括デコード経路を使う。** ISO-2022-JPのエスケープシーケンスによるモード切替という状態を持つ性質上、チェックポイントからの再開時に「そのバイト位置がどのモードか」を別途保持する必要があり、mmap+遅延デコードへの一般化は独立した設計課題になる。加えて対象ペルソナがShift-JIS/EUC-JP/ISO-2022-JPで開く想定のファイルは実務上MB級であり10GB級の想定が無い
- **UTF-16はチェックポイント機構を使わない。** バイトオフセット/2が常に正確なCUオフセットになる(サロゲートペアも2個の独立したCUとして扱われるため)ため、`viewMemoryMappedUtf16()`は要求範囲のバイト位置を直接計算するのみ。UTF-32は非BMP文字が1ユニットから2 CUを生成しCUオフセットが乖離しうるため、UTF-8と同型のチェックポイント方式を維持(ただし固定4バイトユニットで可変長先頭バイト判定が無い分UTF-8より単純)
- **`loadFile()`の`maxBytes`デフォルトを16GiB(10GB目標+ヘッドルーム)に設定した。** `loadUtf8File()`の512MiBデフォルトのまま`main.cpp`/`app::openDocumentAt()`が上限指定なしで呼んでいたため、実際のアプリの入口からは「10GB」目標にそもそも到達できていなかった。この変更で初めて到達可能になった
- **`loadUtf8File()`自体は一切変更しない。** `search::GrepService`の「バイナリ/非UTF-8ファイルは静かにスキップ」という既存の意図的スコープ(Phase 5c1)を壊さないため、内部実装だけ汎化した`openMemoryMapped(path, byteOffset, Encoding::Utf8)`を呼ぶようリファクタしたが外部挙動は完全に同一
- ISO-2022-JP自動判定は引き続き未実装(平文ISO-2022-JPは全バイトが0x80未満のためUTF-8判定に成功してしまい、`loadFile()`は文字化けした状態でUTF-8として"成功"扱いする既知の制約)

**テスト:** `tests/unit/document_file_loader_test.cpp`に`LoadFileTest`スイート(19件)を追加 — UTF-16/UTF-32 BOM往復、非BMPサロゲートペア(UTF-16源/UTF-32源)、不正な奇数バイト数/4の倍数でないバイト数の拒否、Shift-JIS/EUC-JP自動判定、UTF-8フォールバック、大規模範囲(10万CU)でのO(1)バイト↔CU算出とPieceTable分割検証。

**意図的にスコープ外とした項目:** ISO-2022-JP自動判定、N-gramモデルによる曖昧ケース確信度算出、「エンコーディング指定して開く」UI(メニュー/ステータスバー基盤が本コードベースに無い)、`GrepService`の多エンコーディング対応。詳細は`master_roadmap.md` §6参照。

---

## 10. Syntax Engine 詳細

> ⚠️ **本節は当初TextMate互換文法(ADR-003)を前提としたスケッチだったが、Phase 7a着手前レビューでADR-014によりtree-sitterへ切替済み。** §10.1/10.2は将来(非同期増分解析・アウトライン/折り畳み統合サブフェーズ)着手時の構想として残すが、tree-sitterでは10.2の「TextMate文法→独自IRコンパイル」は発生しない(グラマーはCソースとして静的リンクするのみ)。実装済みの内容は§10.3参照。

### 10.1 増分解析 (未実装、将来構想)
- 変更範囲を含む Region を最小単位で再解析
- 結果はカラー ID の run-length で保持し、Rendering に渡す
- 折り畳み範囲は解析結果から生成

### 10.2 文法定義 (旧スケッチ、ADR-014により対象外)
- ~~TextMate 互換 (JSON/XML) → 独自 IR にコンパイル~~
- ~~ホットリロード可能~~

### 10.3 neomifes::syntax (Phase 7a 実装、シグネチャは§10.6 Phase 7dで多言語対応)

`neomifes::syntax::parseCpp()`(`src/syntax/include/neomifes/syntax/syntax.h`)は、tree-sitter(ADR-014)でC++ソースを解析し、フラットなToken列を返すヘッドレス関数。Document/RenderPipeline統合・非同期増分解析・他言語対応はいずれも後続サブフェーズへ意図的に据え置き(§7.12参照)。**本節のコード例はPhase 7a完了時点のC++単一言語版 - Phase 7dでLanguage enum/parsePython()/parse()が追加された現在の完全なシグネチャは§10.6参照。**

```cpp
// src/syntax/include/neomifes/syntax/syntax.h (Phase 7a時点、C++単一言語)
enum class TokenKind {
    Text, Keyword, Type, Variable, Number, String, Comment, Punctuation, Preprocessor,
};

struct Token {
    document::TextRange range;  // UTF-16コードユニットオフセット
    TokenKind           kind = TokenKind::Text;
};

[[nodiscard]] std::vector<Token> parseCpp(std::u16string_view text);
```

**設計上の要点:**
- **`ts_parser_parse_string_encoding(..., TSInputEncodingUTF16LE)`で`std::u16string`を直接パースし、UTF-8への往復変換を挟まない。** バイトオフセット÷2が常に正確なUTF-16コードユニットオフセットになることをスタンドアロンprobeで実機確認済み(`document::TextPos`の既存規約と自然に一致)
- **公開ヘッダにtree-sitterの型(`TSNode`/`TSTree`等)を一切露出しない。** `nlohmann::json`を隠蔽したADR-013の設計判断を踏襲、実装は`syntax.cpp`内の無名namespaceに完全に閉じる
- **`TokenKind`はroadmap §7.3のフルスケッチ(Function/Operator/TypeParameter/Enum/Namespace/Interface/Attribute/Error + modifiersビットフィールド)から縮小し9値のみ実装。** Functionは呼び出し/宣言の文脈判定(親ノード参照)が必要で単一leafの種別だけでは決定できず、Operatorはtree-sitter-cppの匿名トークン集合(約200種)にPunctuationとの明確な境界が無いため、いずれも後続サブフェーズへ据え置き(Phase 6aの`Encoding`enum同様「未実装のenumeratorを公開APIに置かない」規約を踏襲)
- **ノード種別→TokenKind対応表は`tree-sitter-cpp` v0.23.4の`node-types.json`(230件の名前付きノード型)を実機参照し、実際のパーサ出力(既知C++スニペット)と交差検証して構築した。** 名前付きleafノード(`identifier`/`primitive_type`/`comment`/`string_content`等)は個別テーブル、匿名leafノード(キーワード・演算子・記号)は「英字のみならKeyword、`#`始まりならPreprocessor、引用符(`"`/`'`)ならString、それ以外はPunctuation」という構造的ルールで分類 — C++の文法上、演算子/記号トークンに純英字のものが存在しない性質を利用した一般化
- **`walkTree()`は`TSTreeCursor`を使ったイテレーティブなpre-order走査。** C++呼び出しスタックの深さに依存しない(tree-sitterのカーソルAPI自体が内部スタックを持つ標準的な技法)
- **tree-sitter-cppのCMakeLists.txtを直接`add_subdirectory()`しない。** `find_program(TREE_SITTER_CLI tree-sitter)`ベースの`parser.c`再生成が未インストール環境でビルド失敗することをスタンドアロンprobeで確認したため、`SOURCE_SUBDIR "does-not-exist"`(populateのみ、add_subdirectory()はしない公式イディオム)+フェッチ済みソースを直接参照する自前`add_library`ターゲットで回避(`cmake/Dependencies.cmake`参照、詳細はADR-014)

**意図的にスコープ外とした項目:** Document/RenderPipeline統合(実際の色付け描画)、非同期増分解析(Syntax Worker Thread)、C++以外の22言語、折り畳み・アウトライン・ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlighting。詳細は`master_roadmap.md` §7参照。

**ベンチマーク実測(Release、`BM_ParseCpp_Synthetic`):** 5万イテレーション(実質30万行、UTF-16で約10.8MB)を1977msで解析。1行あたり約6.6μs、100万行換算で約6.6秒 — roadmap §7.11目標(≤5秒)には未達。同期単発パースのベースライン値として記録、非同期化(後続サブフェーズ)で設計自体が変わる見込みのため現時点での追加最適化は見送り。

### 10.4 RenderPipelineへのシンタックスハイライト統合 (Phase 7b実装)

`neomifes::syntax::parseCpp()`(§10.3)を`neomifes::render::RenderPipeline`へ統合し、C++ファイルを開いた際に実際にトークン別配色で描画するようにした。C++単一言語のみ、非同期増分解析・多言語対応は後続サブフェーズへ据え置き。

**`RenderPipeline`側の追加API(Phase 7a時点、C++単一言語版 - Phase 7dで`setLanguage(std::optional<syntax::Language>)`へ一般化、§10.6参照):**
```cpp
// render_pipeline.h (Phase 7b時点)
void setSyntaxHighlightingEnabled(bool enabled) noexcept;
```
呼ぶと`m_syntaxHighlightingEnabled`を更新し、`m_hasCachedSnapshot = false`を立てて次回`render()`で無条件に`refreshDocumentCacheIfStale()`の再取得パスへ入るよう強制する(切り替え直後の新規Documentの`version()`が偶然一致するケースを気にせず済む)。

**設計上の要点:**
- **トークン色はハードコード定数(VSCode Dark+準拠)。** Theme(色定義)システムは本コードベースに存在しない(roadmap §7.8が想定していた`detailed_design.md` §5のThemeは未実装、実際の§5はEditor Core)。既存の選択色/マッチ色/ブックマーク色と同じ`ensureXBrush()`パターンをKeyword/Type/String/Number/Comment/Preprocessorの6ブラシに拡張しただけ。Text/Variable/Punctuationは専用ブラシを持たず、`DrawTextLayout()`の既定ブラシ(`m_textBrush`)にそのままフォールスルーする
- **`refreshDocumentCacheIfStale()`が`m_cachedSnapshot`更新と同じタイミングで`m_tokens`(`std::vector<syntax::Token>`)を再計算する。** `Document::version()`が動いた時だけ`syntax::parseCpp()`を全文書に対して同期実行 — 大ファイルでは編集のたびに視認できるカクつきが出ることは既知の制約(§7.11の非同期増分解析待ち)
- **`IDWriteTextLayout::SetDrawingEffect(brush, range)` + `ID2D1DeviceContext::DrawTextLayout()`が範囲ごとに異なるブラシを自動的に使う標準機構であることを確認し、カスタム`IDWriteTextRenderer`は書いていない。** ただし`TextLayoutCache`(ADR-011)はデバイスロスト時に明示的クリアされない設計のため、色ブラシをキャッシュ済みレイアウトへ"焼き込む"(cache miss時にだけ`SetDrawingEffect`する)実装にすると、デバイス再生成後に古いブラシへのダングリング参照が残ってしまう。**この問題を避けるため、`drawTokensOnLine()`は`TextLayoutCache`のヒット/ミスに関わらず`drawVisibleLines()`の可視行ループから毎フレーム呼ばれる**(`SetDrawingEffect()`自体は再シェイピングを伴わない軽量なメタデータ書き込みのため、既存の`drawSelectionsOnLine()`/`drawMatchesOnLine()`と同じコスト特性)。`TextLayoutCache`自体・デバイスライフタイム関連コードは無変更
- **`drawTokensOnLine()`は`m_tokens`(`parseCpp()`が左→右ソート済みで返すことをテストで保証済み)に対する二分走査を、`drawVisibleLines()`の可視行ループ全体を跨いで前進する`std::size_t tokenCursor`で実装。** `O(可視行数 × 全トークン数)`ではなく一回の前進走査で`O(可視範囲と重なるトークン数)`のコストに収まる。複数行にまたがるトークン(ブロックコメント等)はそのトークンのrange.endが現在行のlineStartを超えるまで`tokenCursor`が進まないため、正しく複数行にわたって再訪される

**C++判定 (`neomifes::app::isCppSourceFile()`、`src/app/include/neomifes/app/syntax_language.h`、Phase 7b時点。Phase 7dで`detectLanguage()`へ完全に置き換わった - §10.6参照):**
```cpp
[[nodiscard]] inline bool isCppSourceFile(const std::filesystem::path& path) noexcept;
```
拡張子ベース(`.cpp/.cc/.cxx/.h/.hpp/.hxx/.hh`、大文字小文字無視)のヘッダオンリー純粋関数。`document::Document`は自分のロード元パスを保持しないため、`main.cpp`に新規状態`currentDocumentPath`(`std::optional<std::filesystem::path>`)を追加し、起動時(`--open`)・F12タグジャンプ成功時・Grep結果ジャンプ成功時の3箇所で更新して`setSyntaxHighlightingEnabled()`へ渡す。汎用言語レジストリ(roadmap §7.3の`SyntaxEngine::registerLanguage()`)は2言語目が実際に増えるまで作らない判断。`--measure-frame`モードはこの配線の対象外のまま(既存ベンチマークベースラインへの影響を避けるため)。

**意図的にスコープ外とした項目:** C++以外の22言語、非同期増分解析(Syntax Worker Thread、→Phase 7cで実装)、Theme(ユーザー設定可能な配色)システム、`TokenKind::Function`/`Operator`/`Attribute`/`Error`、折り畳み・アウトライン・ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlighting。詳細は`master_roadmap.md` §7参照。

### 10.5 非同期シンタックス再解析 (Phase 7c実装)

`neomifes::render::SyntaxWorker`(`src/render/include/neomifes/render/syntax_worker.h`)が、§10.4の同期`syntax::parseCpp()`呼び出しを本コードベース初の`std::thread`へ移し、UIスレッドが大ファイルの全文書再解析でブロックされる問題(7aベンチマーク: 100万行で約6.6秒)を解消する。真の増分再解析(`ts_tree_edit()`)は未実装のまま、全文書再解析を非同期化しただけ。

```cpp
// src/render/include/neomifes/render/syntax_worker.h (Phase 7dでLanguage引数を追加)
inline constexpr UINT kMsgSyntaxTokensReady = WM_APP + 2;

class SyntaxWorker {
public:
    explicit SyntaxWorker(HWND targetHwnd);
    ~SyntaxWorker();  // シャットダウン通知 + join()

    // 単一スロット合流 - 未着手の保留分は新しい方で上書き(キューなし)
    void requestParse(std::shared_ptr<const document::BufferSnapshot> snapshot,
                      syntax::Language                               language) noexcept;
};
```

**設計上の要点:**
- **単一スロットのリクエスト合流。** `std::mutex`+`std::condition_variable`で保護された`m_pending`(1個のみ)。ワーカーが処理中に新しいリクエストが来たら、まだ着手していない保留分を上書きするだけでキューには積まない。高速タイピング中に版が古いリクエストを律儀に処理する無駄を避ける
- **完了通知は`PostMessageW(hwnd, kMsgSyntaxTokensReady, 0, ヒープ確保したvector<Token>*)`。** 受信側(`main.cpp`)が即座に`unique_ptr`で所有権を回収する契約。`PostMessageW`が失敗した場合(シャットダウン競合等)は`tokens`(まだ`unique_ptr`が握っている)のデストラクタが回収し、リークしない
- **メンバ宣言順が重要。** `m_mutex`/`m_cv`/`m_pending`/`m_shuttingDown`を`m_thread`より前に宣言する(コンストラクタの初期化子リストの順序ではなく宣言順で構築される) — さもないと`workerLoop()`が未構築のmutexを触る可能性がある
- **`neomifes::render`は`neomifes::ui`に依存しない設計を維持した。** `kMsgSyntaxTokensReady`はrender::側に定義。`ui::MainWindow`には型を一切知らない汎用`onAppMessage(HWND, UINT msg, WPARAM, LPARAM)`フックを新設し(`onCommand`と同じ「未解釈のまま転送」パターン)、`WM_APP`以上の未解釈メッセージを全て転送するだけにした。main.cppだけが両方の型を知っており、`msg == kMsgSyntaxTokensReady`を判定して`RenderPipeline::applyAsyncSyntaxTokens()`を呼ぶ
- **ワーカーは`RenderPipeline::attach()`後、`refreshDocumentCacheIfStale()`内で遅延生成する。** `setSyntaxHighlightingEnabled(true)`はmain.cppの起動シーケンスで`attach()`より前に呼ばれることがあり、その時点では`m_hwnd`がまだ`nullptr`。`refreshDocumentCacheIfStale()`は`render()`経由でしか到達しないため`m_hwnd`が有効であることが保証される。`--measure-frame`/`-startup`/`-memory`はシンタックスハイライトを一切有効化しないため、これらの計測モードでは背景スレッドが1つも生成されない(既存ベンチマークベースラインへの影響を避ける)
- **`refreshDocumentCacheIfStale()`は`Document::version()`変更時に`m_tokens`を即座にクリアし、非同期`requestParse()`を発火するだけ。** 全文書再解析のため編集後は既存トークンのオフセットが無効になりうる — roadmap §7.9の「解析中は古いトークンを表示し続ける」から意図的に外れ、色を一旦落として安全性を優先した(`applyAsyncSyntaxTokens()`が結果到着時に`m_tokens`を差し替え、`m_lastRenderedFrameState`をリセットしてADR-011の粗粒度フレームスキップに握りつぶされないようにする)

**意図的にスコープ外とした項目:** 真の増分再解析(`ts_tree_edit()`、`Document`の編集範囲通知機構が前提)、デバウンス(合流ロジックで無駄な二重処理は防げているためベンチマーク根拠なしに追加しない)、C++以外の22言語。詳細は`master_roadmap.md` §7参照。

### 10.6 シンタックス多言語対応 + 言語ディスパッチ機構の一般化 (Phase 7d実装)

§10.3〜10.5がC++単一言語専用に作っていた`neomifes::syntax`/`RenderPipeline`/`SyntaxWorker`/`neomifes::app::syntax_language.h`を、Python(2言語目)追加と同時に一般化した。**多言語対応と汎用化を同時に行うことで、C++単独では検証できなかった抽象の妥当性(`TokenKind`・`classifyAnonymousLeaf()`が本当に言語非依存かどうか)を実データで確認した。**

```cpp
// src/syntax/include/neomifes/syntax/syntax.h (Phase 7d、現在の完全なシグネチャ)
enum class Language { Cpp, Python };

[[nodiscard]] std::vector<Token> parseCpp(std::u16string_view text);
[[nodiscard]] std::vector<Token> parsePython(std::u16string_view text);
[[nodiscard]] std::vector<Token> parse(std::u16string_view text, Language language);

// src/render/include/neomifes/render/render_pipeline.h
void setLanguage(std::optional<syntax::Language> language) noexcept;  // nullopt = ハイライト無効

// src/app/include/neomifes/app/syntax_language.h (isCppSourceFile()を置き換え)
[[nodiscard]] inline std::optional<syntax::Language> detectLanguage(
    const std::filesystem::path& path) noexcept;
```

**設計上の要点:**
- **`tree-sitter-python`(v0.25.0)は`tree-sitter-cpp`と全く同じCMake回避パターン(`SOURCE_SUBDIR "does-not-exist"` + 自前`add_library`、`cmake/Dependencies.cmake`参照)がそのまま流用できることを、実装着手前のスタンドアロンprobeで確認した。** `src/parser.c`に加え`src/scanner.c`(インデント/デデント処理の外部スキャナ)もコンパイル対象に含める点がC++との唯一の差
- **`syntax.cpp`内部は言語共通部分(`classifyAnonymousLeaf()`・`walkTree()`・`parseWithLanguage()`ヘルパー・RAIIラッパ型)と言語固有部分(`namedLeafKindsForCpp()`/`namedLeafKindsForPython()`の2独立テーブル)に分離した。** `classifyLeaf()`/`appendLeafToken()`/`walkTree()`はどのテーブルを使うか引数で受け取るよう変更
- **`classifyAnonymousLeaf()`(匿名リーフを構造的に分類する関数)は1行も変更せずPythonにもそのまま通用することを実機確認した。** Pythonの`async`/`await`/`lambda`/`and`/`or`/`not`/`is`等の全キーワードも、`:=`/`==`/`@`等の全演算子・記号も、「全ASCII英字ならKeyword、それ以外はPunctuation」という既存ルールと矛盾しなかった — Phase 7a設計時点の「C++の文法上、演算子/記号トークンに純英字のものが存在しない性質を利用した一般化」という狙い通りの結果
- **`TokenKind`も無変更のままPythonに通用した。** Pythonの`True`/`False`/`None`/`...`(ellipsis)はC++の`true`/`false`/`this`/`null`と同じKeyword扱い。Pythonの型注釈(`x: int`)はtree-sitter-pythonの文法上プレーンな`identifier`ノードにしかならないため、`TokenKind::Type`は一度もPythonトークンに割り当てられない(既知の限界、LSP統合待ち)。`RenderPipeline`の描画側コード(`drawTokensOnLine`/`tokenBrush`/`ensureTokenBrushes`)は1行も変更していない — Phase 7bの6色ブラシがそのままPythonにも使えることが、`TokenKind`の言語非依存設計の実証になった
- **既知の限界: `string_content`が`escape_sequence`を含む場合、`string_content`ノード自体はleafでなくなり(子ノードを持つcompound node)、`escape_sequence`前後のプレーンテキスト部分にはトークンが一切生成されない(無色表示)。** 例: `"hi\n"`の`"hi"`部分。標準プローブの完全ツリーダンプ(leaf以外のノードも出力する一時的な拡張)で確認した構造的事実。`walkTree()`がleafノード(`child_count()==0`)のみを訪問する設計のため、compound化した`string_content`の「子ノードでカバーされない自身のテキスト範囲」は捕捉されない。修正にはcompoundノードの「子の隙間」を埋める追加ロジックが必要だが、C++側の`Operator`非分離等と同種の受容済み制約として本フェーズのスコープには含めなかった
- **`SyntaxWorker::m_pending`は当初`std::optional<PendingRequest>`(snapshot+languageの組)として実装したが、clang-tidyの`bugprone-unchecked-optional-access`が`m_cv.wait()`の述語と後続の`request->`アクセスの相関を追跡できず誤検知した。** `std::shared_ptr<const BufferSnapshot> m_pending`(nullptrで「保留なし」を表す元の設計のまま)+ 独立した`syntax::Language m_pendingLanguage`(`m_pending != nullptr`の間だけ意味を持つ)という2フィールド構成に変更し、`std::optional`自体を使わないことで誤検知を構造的に回避した
- **`neomifes::app::isCppSourceFile()`を`detectLanguage()`へ完全に置き換えた(旧関数は削除、共存させていない)。** `.py`/`.pyw`/`.pyi`をPython、既存のC++拡張子群をC++として認識、それ以外は`nullopt`。シバン行によるPython判定は、C++判定も拡張子のみである対称性を優先し見送った

**意図的にスコープ外とした項目:** C++/Python以外の21言語(3言語目以降は同じパターンを複製する)、真の増分再解析、Theme(ユーザー設定可能な配色)システム、折り畳み・アウトライン・ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlighting。詳細は`master_roadmap.md` §7参照。

### 10.7 Indent guides (Phase 7e実装)

インデント階層を薄い縦線で表示する`RenderPipeline::drawIndentGuidesOnLine()`を実装した。カーソルが乗っている行のガイドは明るい色で強調する(VSCodeの「アクティブなインデントガイド」相当の簡略版)。

```cpp
// src/render/include/neomifes/render/indent_guide_math.h
[[nodiscard]] constexpr std::uint32_t computeIndentColumns(
    std::u16string_view lineText, std::uint32_t tabWidth) noexcept;
[[nodiscard]] constexpr std::uint32_t computeIndentGuideCount(
    std::uint32_t indentColumns, std::uint32_t tabWidth) noexcept;

// render_pipeline.h
void drawIndentGuidesOnLine(ID2D1DeviceContext6& dc, float y, std::u16string_view lineSpan,
                            bool isActiveLine) noexcept;
```

**設計上の要点:**
- **`indent_guide_math.h`は`resize_math.h`/`viewport_math.h`と同じ「Windows SDK非依存・ヘッドレステスト可能」パターンのヘッダオンリー純粋関数。** `computeIndentColumns()`は`core::computeIndentationConversionEdits()`(`indentation_conversion.cpp`、Phase 4b8d)と同じタブ幅規約(スペース+1、タブは次のタブ幅倍数まで前進)に意味論だけ揃え、実装は独立させた。DirectWriteのタブ描画(`SetIncrementalTabStop`)には一切依存しない
- **`drawIndentGuidesOnLine()`は既存の`drawGutterOnLine`/`drawTokensOnLine`等と同列の呼び出しとして`drawVisibleLines()`の可視行ループに追加した。** roadmapスケッチが想定していた専用`LineLayout`クラス(`src/render/line_layout.cpp`)は実在しない(Phase 7a〜7dで繰り返し確認済みの通り) — `RenderPipeline`が全ての描画対象状態を直接保持する既存パターンをそのまま踏襲
- **「アクティブなインデントガイド」は簡略版。** VSCode本家はカーソルの現在スコープ(ブロック/関数の範囲)全体でガイドをハイライトするが、それには`FoldingModel`(ブロック範囲検出、未実装)が要る。本フェーズでは`computeCaretDraws()`が既に1フレームに1回計算する`caretDraws`を再利用し、**カーソルが乗っている行1行分のガイドだけ**を明るいブラシ(`m_activeIndentGuideBrush`)で描画する — スコープ全体のハイライトは`FoldingModel`実装後の後続改善として明記するに留めた
- **タブ幅は`kTabWidth=4`を`render_pipeline.cpp`側に複製した。** `main.cpp`の同名定数(Phase 4b8dのタブ⇔スペース変換コマンドで確立済み)と値は一致させるが、設定システムが本コードベースに無いため2箇所の手動同期が必要になる既知のトレードオフとして受容
- **常時描画・トグル不可。** 既存のキャレット/ガター/選択ハイライトと同じく、ON/OFFスイッチを設ける根拠が無いと判断。`--measure-frame`の合成ベンチマーク文書(先頭空白を含まない行のみ)への実測影響は「桁数0→ガイド0本」の早期リターンのみで実質ゼロだったことを確認済み

**意図的にスコープ外とした項目:** アクティブガイドのスコープ全体ハイライト(`FoldingModel`実装後に再検討)、空行のガイド継承、タブ幅のユーザー設定UI、Bracket Pair Colorization(roadmap記述の誤記と判明、無関係な別機能)。詳細は`master_roadmap.md` §7参照。

### 10.8 アウトライン抽出 (Phase 7f実装)

関数/クラス/構造体/名前空間のシンボルツリーをヘッドレスに抽出する`neomifes::syntax::extractOutline()`を実装した。`Document`/`RenderPipeline`/UIへの依存は一切無く、`outline_pane`(WC_TREEVIEW)配線・折り畳みは後続サブフェーズへ据え置いた。

```cpp
// src/syntax/include/neomifes/syntax/outline.h
enum class SymbolKind : std::uint8_t { Function, Class, Struct, Namespace };

struct OutlineNode {
    std::u16string           name;
    document::TextPos        pos;              // シンボル名identifierの開始位置
    document::TextRange      containingRange;  // 定義全体の範囲(将来のBreadcrumb逆引き用)
    SymbolKind                symbolKind;
    std::vector<OutlineNode> children;         // ネストした定義(メンバ関数・内部クラス等)
};

[[nodiscard]] std::vector<OutlineNode> extractOutline(std::u16string_view text, Language language);
```

**設計上の要点:**
- **`SymbolKind`は`syntax::TokenKind`を再利用せず新設した。** `TokenKind`はPhase 7aでリーフレベルのテキスト着色専用に設計されており、`Function`/`Class`/`Namespace`等は「呼び出しと定義の文脈判定が必要」という理由で意図的に未実装のまま公開APIに置かれていない。アウトライン抽出は複合ノード(定義そのもの)だけを訪問するため衝突は起きないが、無関係な2つの分類概念を1つのenumに混在させないため独立させた
- **既存`syntax.h`の`parseCpp()`/`parsePython()`/`parse()`とはツリーを共有しない、独立した2回目のパースとして実装した。** ファイルを開いた時/変更が落ち着いた時のみ低頻度で呼ばれる想定であり、ツリー共有によるパース回数削減はベンチマーク根拠の無い最適化と判断(CLAUDE.mdルール10)
- **C++の`function_definition`は`"name"`フィールドを持たず、`"declarator"`フィールドの中に名前が入れ子になっている。** `declaratorChild()`ヘルパーが named field(`pointer_declarator`/`function_declarator`)と positional child(`reference_declarator`)の両方に対応する — node-types.jsonで確認した通り、`reference_declarator`は`"fields": {}`(位置引数のみ)という、`pointer_declarator`とは非対称な文法構造を持つ。qualified_identifier(`Widget::doThing()`のような out-of-line 定義)は自身の`"name"`フィールドが非修飾名を直接指すことを利用して解決する
- **`resolveSymbolName()`は`Language`引数で言語ごとに分岐する。** C++/Pythonの両文法が関数定義ノードを同じ`"function_definition"`という型名で持つため、ノード型名だけで分岐すると言語混同を起こす(Pythonの`function_definition`は直接`"name"`フィールドを持つ素直な構造で、C++専用のdeclarator-unwrapパスを通すと名前解決に失敗する)
- **`walkForOutline()`は明示スタックによる反復実装。** AST深さは編集対象のソースファイル依存で安全に有界ではないため、`syntax.cpp`の`walkTree()`(`TSTreeCursor`ベースの反復pre-order走査)と同じ理由で再帰を避けた。ネストした`OutlineNode`ツリーを構築する必要があるため、単純なcursor走査ではなく`scanStack`(スキャン再開点)+`resultLevels`/`pendingSymbols`(構築中の`vector<OutlineNode>`とその親シンボル)からなる明示的な2段スタック構成にした

**意図的にスコープ外とした項目:** `outline_pane`(WC_TREEVIEW UI)・`main.cpp`配線、Breadcrumb(逆引き)、折り畳み(`FoldingModel`、Core+Rendering層を横断する別規模の変更と判明)、テンプレート特殊化・ラムダ式・演算子オーバーロード等の複雑なC++宣言構文からの名前抽出。詳細は`master_roadmap.md` §7参照。

### 10.9 アウトラインUI統合 (Phase 7g実装)

Phase 7fの`extractOutline()`を実際にUIへ繋いだ。新規`ui::OutlinePane`(WC_TREEVIEW)をCtrl+Shift+Oでトグル表示し、クリックで同一ドキュメント内の該当位置へジャンプする。

```cpp
// src/ui/include/neomifes/ui/outline_pane.h
struct OutlineItem {
    std::u16string            name;
    std::uint64_t             targetPos = 0;  // opaque、ui::層は解釈しない
    std::vector<OutlineItem>  children;
};

struct OutlinePaneConfig {
    std::function<void(std::uint64_t targetPos)> onItemSelected;
    std::function<void()> onClosed;
};

class OutlinePane {
public:
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const OutlinePaneConfig& config);
    void showWith(std::vector<OutlineItem> items) noexcept;
    void hide() noexcept;
    void onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale) noexcept;
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;  // TVN_SELCHANGEDW
    // ...
};

// src/app/include/neomifes/app/outline_bridge.h
[[nodiscard]] std::vector<ui::OutlineItem> buildOutlineItems(
    const std::vector<syntax::OutlineNode>& nodes);
```

**設計上の要点:**
- **`WC_TREEVIEW`はこのコードベース初出のコントロール型で、通知が`WM_COMMAND`ではなく`WM_NOTIFY`で届く。** `MainWindowConfig`に新規`onNotify`フック(`onCommand`/`onAppMessage`と同じ「未解釈のまま転送」形)を追加した。`InitCommonControlsEx`の`dwICC`に`ICC_TREEVIEW_CLASSES`を追加(既存`ICC_STANDARD_CLASSES`だけでは`WC_TREEVIEWW`クラス未登録)
- **アウトライン項目を選択したら即座にジャンプするが、パネルは閉じない。** `FindBar`/`GrepBar`/`CommandPalette`(検索/コマンド実行という単発ツール)の「アクション後に隠れる」設計とは意図的に異なる — アウトラインは複数シンボルを連続して見て回るナビゲーション補助のため。Escapeキーで明示的に閉じる
- **`ui::OutlinePane`は`syntax::OutlineNode`を直接知らない。** `ui::OutlineItem`(UI専用ミラー型、`targetPos`は解釈しないopaqueな`std::uint64_t`)を公開APIとし、`app::buildOutlineItems()`(ヘッダオンリー)が変換を担う — 既存の「`ui::`はWin32機構のみ」原則を踏襲
- **ジャンプは`app::openDocumentAt()`を使わない。** `OutlineNode::pos`は既に開いている同一ドキュメント内の絶対`document::TextPos`のため、`jumpToGotoTarget()`と同型の`selectionModel.moveAllTo()` → `viewport.ensureVisible()` → `syncRenderStateAndInvalidate()`をそのまま踏襲(行/桁変換不要)
- **パネルは右ドッキング・フル高さのオーバーレイ(`FindBar`等の固定サイズボックスとは意図的な逸脱)。** `RenderPipeline`の描画幅は狭めない(既存オーバーレイと同じ「重ねるだけ」設計)
- **`populateTree()`はTreeView項目挿入を明示スタックで反復実装した。** `OutlineNode`のネスト自体はシンボル定義の入れ子(生AST深さより浅い)だが、Phase 7fの`walkForOutline()`が`misc-no-recursion`指摘を受けた直後だったため、同じ轍を踏まないよう予防的に反復にした。子を逆順にスタックへ積むことで`TVI_LAST`挿入順が元の左→右順を保つ
- **既存4オーバーレイ(FindBar/CommandPalette/GotoLineBar/GrepBar)に共通する潜在バグを`EnumChildWindows`で発見した。** `onDeferredInit`(`WM_SIZE`より後に走る投稿メッセージ)内で`.create()`されるため、`cfg.onResize`経由の位置決めが二度と発火せず、ユーザーが手動リサイスするまでプレースホルダ座標に留まる。`OutlinePane`は`create()`直後に`::GetClientRect`+`::GetDpiForWindow`で明示的に`onParentResized()`を呼んで解消したが、既存4オーバーレイの修正は別タスクへ切り出した(spawn_task、CLAUDE.mdルール8)

**意図的にスコープ外とした項目:** 表示中のライブ追従(編集のたびの自動再計算)、シンボル種別ごとのアイコン表示、折り畳み状態の永続化、ファイル切替時の自動再表示、`RenderPipeline`描画幅の真のドッキング狭小化。詳細は`master_roadmap.md` §7参照。

### 10.10 Breadcrumb (Phase 7h実装)

Phase 7f/7gの`OutlineNode`ツリー資産をカーソル位置からの逆引きに再利用し、`RenderPipeline`の上部に常時表示のBreadcrumbストリップを追加した。

```cpp
// src/syntax/include/neomifes/syntax/outline.h
[[nodiscard]] std::vector<const OutlineNode*> findBreadcrumbPath(
    document::TextPos pos, const std::vector<OutlineNode>& nodes);

// src/render/include/neomifes/render/render_pipeline.h
struct CursorVisual {
    document::TextPos   position       = 0;
    document::TextRange selectionRange{};
    std::uint32_t       virtualColumnOffset = 0;
    bool                 isPrimary           = false;  // Phase 7h
    // ...
};
```

**設計上の要点:**
- **`findBreadcrumbPath()`は`OutlineNode::containingRange`([start, end)半開区間)の線形探索で実装した。** `nodes`の各レベルで`pos`を含むノードを探し、見つかればそのノードの`children`へ降りて繰り返す明示ループ — `src/.clang-tidy`の`WarningsAsErrors: '*'`下で`misc-no-recursion`が(木の実際の浅さに関わらず)自己再帰を一律検出するため、`walkForOutline()`と同じ理由で反復実装にした
- **`CursorVisual::isPrimary`(デフォルト`false`)を新設し、`main.cpp`の`syncRenderStateAndInvalidate()`が`cursor.isPrimary`をそのまま転送する。** `drawBreadcrumb()`は`m_cursorVisuals`を線形探索して`isPrimary == true`の1件だけを使う(複数カーソル時も主カーソルの位置のみ)
- **`m_cachedOutline`は`refreshDocumentCacheIfStale()`内で`m_tokens`と同じタイミング(ドキュメントバージョン変更時)に同期的に再計算する。** `syntax::extractOutline()`を直接呼び出し、`SyntaxWorker`のような非同期化はベンチマーク根拠が無いため見送った(Phase 7b→7cの前例と同じ順序)
- **`kBreadcrumbHeightDips`(24.0F)を新設し、`kGutterWidthDips`の縦版として3箇所を更新した:** `drawVisibleLines()`の`y`初期値、`hitTest()`の`yDip`(帯内クリックは先頭行にクランプ)、`drawVisibleLines()`内の`computeVisibleLineCount()`呼び出しに渡す実効高さ(`m_height`からBreadcrumb帯の高さを引いたpx値)。`computeVisibleLineCount()`自体のシグネチャは変更しない
- **`drawBreadcrumb()`は`m_breadcrumbBackgroundBrush`で帯を塗った後、`findBreadcrumbPath()`の結果を`" > "`で連結し、その場限りの`IDWriteTextLayout`(`TextLayoutCache`は行番号キー専用のため流用しない)で描画する。** パスが空でも帯自体は描画する(「このシンボルの外にいる」ことを示す)
- **実アプリ視覚確認で、Breadcrumbとは別に既存の潜在バグを発見・修正した。** `onDeferredInit`が起動時の初期カーソル状態を`RenderPipeline`へ一度も同期していなかった(`syncRenderStateAndInvalidate()`が呼ばれておらず`m_cursorVisuals`が空のまま)ため、ファイルを開いた直後はBreadcrumbもキャレットも不可視だった。`onDeferredInit`末尾の`InvalidateRect()`を`syncRenderStateAndInvalidate()`に置き換えて解消した

**意図的にスコープ外とした項目:** Breadcrumbクリックでのジャンプ・ドロップダウン、アウトライン抽出の非同期化、非対応言語ファイルでの代替表示。詳細は`master_roadmap.md` §7参照。

---

### 10.11 折り畳み コア基盤 (Phase 7i実装)

Phase 7f/7gの`OutlineNode`ツリーを折り畳み対象領域としてそのまま流用し、`core::FoldingModel`(論理行番号ベース、二重座標系は不採用)+ キーボードトグルのみのv1を実装した。ガター+/-クリックでのトグルは次サブフェーズへ据え置いた。

```cpp
// src/core/include/neomifes/core/folding_model.h
struct FoldRegion {
    document::LineNumber headerLine;        // 折り畳んでも常に見える行
    document::LineNumber endLineInclusive;  // 折り畳み時に隠れる範囲の最終行
    bool                  folded = false;
};

class FoldingModel {
public:
    void setFoldableRegions(std::vector<FoldRegion> regions);  // headerLineで既存folded状態を引き継ぐ
    void toggleFold(document::LineNumber headerLine) noexcept;
    [[nodiscard]] bool isLineHidden(document::LineNumber line) const noexcept;
    [[nodiscard]] bool isFoldHeader(document::LineNumber line) const noexcept;
    [[nodiscard]] std::optional<FoldRegion> foldedRegionContaining(document::LineNumber line) const noexcept;
    bool revealLine(document::LineNumber line) noexcept;  // lineを覆う全折り畳みを展開
    // ...
};

// src/render/include/neomifes/render/render_pipeline.h
struct FoldVisual {
    document::LineNumber headerLine;
    document::LineNumber endLineInclusive;
    bool                  folded = false;
};
void setFoldRegions(std::vector<FoldVisual> regions) noexcept;

// src/app/include/neomifes/app/editor_input.h
bool handleKeyDown(UINT vkCode, bool shiftDown, bool ctrlDown, core::CommandDispatcher& dispatcher,
                   core::SelectionModel& selection, core::Viewport& viewport,
                   const document::Document& document,
                   const core::FoldingModel* folding = nullptr);  // Phase 7i、既定nullptrで既存呼び出し無改修
```

**設計上の要点:**
- **二重座標系(roadmap §7.10原案の「`Viewport`が表示行を管理」)は不採用にした。** `document::LineNumber`は全レイヤーで論理行番号のまま維持し、`RenderPipeline::isLineHidden()`を`drawVisibleLines()`(可視行のみ描画、`y`は隠れた行では進めない)・`hitTest()`(可視行のみを数えて着地行を決定、構造的にクリックが隠れた行へ到達不可能)の2消費箇所で使う。`core::Viewport`/`core::SelectionModel`は無改修
- **`app::buildFoldRegions()`(`src/app/include/neomifes/app/fold_bridge.h`)は`OutlineNode`ツリーを平坦化して`FoldRegion`のリストに変換する。** 1行に収まるシンボル(`endLineInclusive <= headerLine`)は除外する。`walkForOutline()`の`misc-no-recursion`指摘を踏まえ、最初から明示スタックによる反復実装にした
- **`FoldingModel`は`BookmarkManager`と同じ「編集追従なし」制約を踏襲する。** EditEvent購読機構がこのコードベースに無いため、再計算はファイルを開いた時点で1回+アウトラインパネルを開くたびの2箇所のみ(`extractCurrentOutline()`ヘルパーで同じパース結果を`outlinePane.showWith()`と`foldingModel.setFoldableRegions()`の両方へ供給、二重パースを回避)。既存の折り畳み状態は`headerLine`一致で引き継ぐ
- **移動キー(Up/Down/PageUp/PageDown)による隠れた行への着地は`editor_input.cpp`の新規`snapPastHiddenLine()`が補正する。** `selection.moveAll()`実行後、着地行が隠れていれば`foldedRegionContaining()`で覆っている領域を取得し、移動方向の境界(Up/PageUp→`headerLine`、Down/PageDown→`endLineInclusive + 1`)へスナップする(列位置は保持しない、v1の簡略化)。`applyMovementKey()`自体は`editor_input.cpp`の無名namespace内で内部リンケージのため、`main.cpp`から直接folding引数を渡せない — 公開APIの`handleKeyDown()`側に`const core::FoldingModel* folding = nullptr`引数を追加し内部で伝播する形にした
- **ジャンプ経路は「同一ドキュメント内」と「別ファイル」で異なる補正にした。** Ctrl+G(`jumpToGotoTarget()`)・F2ブックマーク次/前(`handleBookmarkKey()`)は着地行を`foldingModel.revealLine()`で自動展開する。Grep結果ジャンプ・タグジャンプ(F12)は`openDocumentAt()`が`Document`を丸ごと差し替えるため、展開ではなく`foldingModel.setFoldableRegions({})`で全クリアする — 旧ファイルの行番号キーの折り畳み領域を新ファイルへ持ち越すと無関係な行を隠す実害があるため。アウトラインパネルからのジャンプ(常にシンボル見出し行へ着地)とマウスクリック(`hitTest()`自体が可視行しか歩かない)は補正不要
- **ガター折り畳みマーカ(▶折畳/▼展開)は`drawGutterOnLine()`に追加描画するが、クリック判定は実装しない(v1のスコープ外、次サブフェーズへ)。** 唯一のトグル手段はコマンドパレットの新規コマンド`view.toggleFoldAtCursor`(「Fold/Unfold at Cursor」) — 主カーソル行が折り畳み見出しでなければno-op
- **`RenderPipeline::drawVisibleLines()`の1行描画ロジック(ハイライト・インデントガイド・トークン・グリフ・キャレット・ガター・折り畳みヘッダマーカーの一式)を新規`drawTextLine()`private関数へ抽出した。** 隠れた行スキップロジック追加により`readability-function-cognitive-complexity`(閾値25)を実測31で超過したためのclang-tidy対応 — `computeCaretDraws()`のPhase 4b7a抽出と同じ理由

**意図的にスコープ外とした項目:** ガター+/-クリックでのトグル、`{}`ブレースマッチングによる任意ブロック折り畳み、折り畳み状態のファイル跨ぎ永続化・Undo/Redo連動、毎編集ごとの折り畳み領域再計算、複数カーソル対応の折り畳みトグル。詳細は`master_roadmap.md` §7参照。

---

### 10.12 折り畳み ガター+/-クリックトグル (Phase 7j実装)

Phase 7iが意図的に据え置いたガター+/-クリックでのトグルを実装し、roadmap上の「折り畳み」機能を完結させた。

```cpp
// src/render/include/neomifes/render/render_pipeline.h
// ガター全幅×フォールド見出し行を対象とする(マーカーの描画幅~7dipsへの精密
// クリックは要求しない)。hitTest()と同じ可視行ウォーク(visibleLineAtRow())
// を共有する。
[[nodiscard]] std::optional<document::LineNumber> hitTestFoldMarker(
    std::int32_t xPx, std::int32_t yPx) noexcept;

// hitTest()から抽出した共有ヘルパー。
[[nodiscard]] document::LineNumber visibleLineAtRow(
    document::LineNumber startLine, document::LineNumber visibleRowOffset) const noexcept;
```

**設計上の要点:**
- **`hitTestFoldMarker()`はガター全幅(`[0, kGutterWidthDips)`)×該当行をクリック可能領域とする。** `drawGutterOnLine()`が実際に描画する▶/▼マーカーの幅は約7dipsしかないが、精密クリックを要求せずVSCode等の一般的な折り畳みUIと同じ「寛容なヒット領域」にした
- **`hitTest()`内にインラインだった「可視行をrowOffset分歩いて対象論理行を求める」ウォークを`visibleLineAtRow()`へ抽出し、`hitTest()`/`hitTestFoldMarker()`の両方から呼ぶ。** 抽出しなければ`drawVisibleLines()`のendLineExclusive計算と合わせて同種ロジックが3箇所目の重複になっていた
- **`main.cpp`の`cfg.onMouseDown`は`hitTestFoldMarker()`を`hitTest()`より先にチェックし、値があれば`foldingModel.toggleFold()` → `syncFoldingState()` → 即returnして通常のカーソル配置経路を完全にバイパスする。** クリック回数は無視し、フォールド見出し行のガタークリックは常にトグルする
- **`onMouseDown`ハンドラ全体を、`onKeyDown`/`onChar`/`onSysKeyDown`で既に確立していた「ラムダは薄いラッパーのみ、本体は独立関数`handleXEvent()`に完全移譲する」パターンへ合わせて再構成した。** `tryToggleFoldMarker()`という別関数へ分岐ロジックを切り出しても、呼び出し元の`if (...) return;`という1個の分岐がラムダ内に残っている限り`wireNormalMode`のcognitive complexityが下がらないと判明したため(閾値25に対し実測26)、新規`handleMouseDownEvent()`(`tryToggleFoldMarker()`チェック+既存`hitTest()`/`dispatchMouseDown()`ロジック全体を包含)へ完全移譲する形にした
- **実アプリでのマウスクリック合成(`SetCursorPos`+`mouse_event`)により、ガター上のフォールドマーカークリックでの折り畳み/展開の往復トグルを実際にスクリーンショットで確認できた。** Phase 7g/7hの「修飾キーを伴う合成キーボード入力は受け付けない」制約はマウスクリック自体には適用されず、この自動化環境から完全に対話的検証ができた最初の折り畳みUI操作になった

**意図的にスコープ外とした項目:** マウスドラッグでの複数行一括トグル、フォールドマーカーのホバー時ビジュアルフィードバック、フォールドマーカークリック直後のドラッグ時のアンカー整合性改善(既知の軽微なエッジケースとして許容)。詳細は`master_roadmap.md` §7参照。

---

### 10.13 真の増分再解析 コア基盤 (Phase 7k実装)

`syntax_worker.h`/roadmap §7.9が繰り返し記録してきた技術的負債(「Documentに編集範囲追跡が無い」「非同期化はしたが全文書再解析のまま」)に着手した。**本フェーズはヘッドレスな正しさの証明までに限定し、`SyntaxWorker`統合・`RenderPipeline`配線は次サブフェーズ(Phase 7l)へ据え置いた。**

```cpp
// src/document/include/neomifes/document/document.h
// startLine/startColumnは新旧座標系で共通(編集はそこから始まる)。
// oldEndLine/oldEndColumnは変更前の行構造に対して、newEndLine/newEndColumn
// は変更後の行構造に対して計算する - Documentはその場でPieceTableを書き換
// えるため(バージョンごとのスナップショットは無い)、旧側は変更前に、新側
// は変更後に、それぞれのタイミングで計算しておく必要がある。
struct EditDelta {
    TextPos       startPos;
    LineNumber    startLine;
    std::uint32_t startColumn = 0;
    TextPos       oldEndPos;
    LineNumber    oldEndLine;
    std::uint32_t oldEndColumn = 0;
    TextPos       newEndPos;
    LineNumber    newEndLine;
    std::uint32_t newEndColumn = 0;
};

// 前回呼び出し以降に蓄積した編集差分を排出する。UIスレッド専用(version()と
// 同じくADR-009のシングルライタ前提、同期不要)。
[[nodiscard]] std::vector<EditDelta> takePendingEdits() noexcept;
```

```cpp
// src/syntax/include/neomifes/syntax/incremental_parser.h
// tree-sitterのTSInputEditを、tree-sitter型を公開せず表現したもの
// (syntax.hの「TSNode/TSTreeは.cppに閉じ込める」規約と同じ)。バイト
// オフセットはUTF-16コードユニットオフセット×2 (syntax.cppの
// appendLeafToken()と同じ規約)。
struct ReparseEdit {
    std::uint32_t startByte = 0, oldEndByte = 0, newEndByte = 0;
    std::uint32_t startRow = 0, startColumn = 0;
    std::uint32_t oldEndRow = 0, oldEndColumn = 0;
    std::uint32_t newEndRow = 0, newEndColumn = 0;
};

// ステートフルな単一言語増分パーサ。前回のTSTreeを保持し、ts_tree_edit()
// で更新してから再解析することでtree-sitterのサブツリー再利用を活かす。
// スレッド非対応(Phase 7l未配線) - 構築/破棄/reparse()は全て同一スレッド
// で行うこと(Documentと同じシングルライタ前提)。
class IncrementalParser {
public:
    explicit IncrementalParser(Language language);
    [[nodiscard]] std::vector<Token> reparse(std::u16string_view text,
                                              std::span<const ReparseEdit> edits);
    // ... (move-only、詳細はヘッダ参照)
};
```

**設計上の要点:**
- **`EditDelta`の新旧位置情報は、`insertText()`/`eraseRange()`/`replaceRange()`各メソッド内で`PieceTable`変更の前後に分けて計算する。** 旧側(`oldEnd*`)は変更前の`offsetToLine()`/`lineToOffset()`呼び出しで、新側(`newEnd*`)は変更後の呼び出しで求める。`edit_commands.cpp`の全コマンド(execute/undo双方)はこの3メソッドを直接呼ぶため、Undo/Redoは新規の分岐無しに自動的にカバーされた
- **`LineIndex::build()`のO(N)フルスキャンは新規コストではないと判断した。** `Document::ensureLineIndex()`は既存の「次の問い合わせ時に1回だけ再構築」設計(`docs/issues/line_index_o_log_n.md`で意図的に許容済み)を持ち、`RenderPipeline`は既に毎フレームこれを呼ぶため、`EditDelta`計算で`offsetToLine()`を呼んでも既に発生する再構築を1箇所前倒しするだけで新たな漸近コストは生まれない
- **`syntax.cpp`の匿名namespace内にあった`walkTree()`・leaf分類テーブル(`namedLeafKindsForCpp()`等)を新規`src/syntax/src/syntax_internal.h`(header-only、`namespace neomifes::syntax::detail`)へ切り出し、`syntax.cpp`(既存の単発フルパース)と新規`incremental_parser.cpp`の両方から共有する。** 本コードベース初の「`src/*/src/`直下に置く非公開ヘッダ」(`include/`ではない) — header-onlyのためCMakeのソースリスト追加が不要という最小限の選択
- **`IncrementalParser::reparse()`は、保持木があれば`edits`の各要素を`ts_tree_edit()`で順に適用してから`ts_parser_parse_string_encoding()`(第2引数に保持木を渡す点のみ既存`parseWithLanguage()`と異なる)を呼び、結果全体を`detail::walkTree()`で再度トークン列化して保持木を差し替える。**
- **正しさは「増分再解析結果が同じ最終テキストへの全文書再解析結果と完全一致する」ことを単体テストで直接証明した。** 単一文字挿入/削除・複数行にまたがる置換・改行挿入(行構造そのものが変わる編集)・3回連続の編集・Pythonでも確認済み。テスト作成中に「改行挿入」のテストケース自体の`oldEndPos`計算が誤っていた(スペースを`\n`で置換する編集を、スペースを保持したまま`\n`を挿入する編集として誤記述していた)バグを自己発見・修正 — 実装ではなくテスト記述側の誤りだった
- **ベンチマーク実測(roadmap §7.11のDoD「1文字入力後の増分解析: ≤ 50ms」に対する評価、CLAUDE.mdルール10):** 5万行の合成C++ソースに対し、全文書再解析(`BM_ParseCpp_Synthetic`)が約1306ms/callであるのに対し、単一文字の置換編集を挟んだ増分再解析(`BM_IncrementalReparse_SingleCharEdit`)は約321ms/call(約4倍高速化)。**DoDの≤50msには未達。** 編集位置を文書の中央/末尾近くに変えてもほぼ同じ実測値(326ms/341ms/321ms)になったことから、`reparse()`が呼び出しのたびに行う`walkTree()`でのトークン列**全体**の再構築(O(文書サイズ)、tree-sitter内部の増分解析自体とは無関係に発生)が支配的コストだと判明した。次サブフェーズ(Phase 7l)では`ts_tree_get_changed_ranges()`で変更範囲だけを抽出し`RenderPipeline`側の既存トークン列へマージする設計へ転換する必要がある

**意図的にスコープ外とした項目:** `SyntaxWorker`統合(「破棄して最新のみ残す」キューモデルの置き換え)、`RenderPipeline::refreshDocumentCacheIfStale()`の書き換え、`ts_tree_get_changed_ranges()`を使った変更範囲限定トークン抽出、アウトライン抽出の増分化。詳細は`master_roadmap.md` §7参照。

---

### 10.14 真の増分再解析の SyntaxWorker 統合 (Phase 7l実装)

Phase 7kが意図的に据え置いた「`SyntaxWorker`統合」に着手し、`syntax::IncrementalParser`が実際に使われる機能になった。

```cpp
// src/render/include/neomifes/render/syntax_worker.h
// document::EditDelta -> syntax::ReparseEditの純粋変換(単体テスト可能)。
[[nodiscard]] syntax::ReparseEdit toReparseEdit(const document::EditDelta& delta) noexcept;

class SyntaxWorker {
public:
    // edits: 前回requestParse()以降に記録された全EditDelta(発生順)。
    // 未pickupのリクエストがあれば追記(上書きしない) - 取りこぼし厳禁。
    // resetIncrementalState: trueならワーカーが保持中のIncrementalParser
    // インスタンスを新規構築で丸ごと差し替える(保持木を破棄)。
    void requestParse(std::shared_ptr<const document::BufferSnapshot> snapshot,
                      syntax::Language language,
                      std::vector<document::EditDelta> edits,
                      bool resetIncrementalState) noexcept;
private:
    // m_pendingSnapshot: 最新のみ保持(上書き)。
    // m_pendingEdits: 蓄積(追記、上書きしない)。
    // m_pendingReset: OR-latch(一度trueになったら排出まで維持)。
};
```

**設計上の要点:**
- **`SyntaxWorker`のキューモデルを「最新の1件のみ保持し古いものは破棄」から「`edits`を蓄積し取りこぼさない」へ刷新した。** `m_pendingSnapshot`/`m_pendingLanguage`は最新のもので上書き(最終テキスト/言語だけが意味を持つため)だが、`m_pendingEdits`は`requestParse()`が呼ばれるたびに`insert(end(), ...)`で追記する。ワーカーの`workerLoop()`はpickup時に蓄積分をまとめて`toReparseEdit()`で変換し、1回の`IncrementalParser::reparse()`呼び出しに渡す
- **`workerLoop()`は`std::optional<syntax::IncrementalParser>`をループのローカル変数として保持し、`resetIncrementalState`が真、または保持中のパーサの言語が今回のリクエストと食い違う場合(初回呼び出しも含む)、新規インスタンスで丸ごと差し替える。** `IncrementalParser`自体に`reset()`メソッドは追加していない — 新規構築すれば保持木は自動的に`nullptr`から始まり、`reparse()`の「`edits`が空、かつ保持木が無ければ全文書再解析」パスへ自然に入る
- **`edits`が空でも保持木が非nullなら`IncrementalParser::reparse()`が無条件にtree-sitterの再解析ヒントとして渡してしまうハザードを実装前に発見した。** F12タグジャンプ/Grep結果ジャンプ等で無関係な別ファイルへ切り替わった直後に空`edits`だけを渡すと、無関係な保持木を使った誤った再解析結果になりうるため、明示的な`resetIncrementalState`引数が必要と判断した
- **「ドキュメントが切り替わった」の検出は既存の`RenderPipeline::setLanguage()`(`m_hasCachedSnapshot = false`を立てる)をそのまま再利用した。** `refreshDocumentCacheIfStale()`内で`m_hasCachedSnapshot`を`true`に更新する前に`const bool forceFullReparse = !m_hasCachedSnapshot;`を捕捉するだけで、初回呼び出しとドキュメント切り替えの両方を検出できた
- **`RenderPipeline::m_document`を`const document::Document*`から`document::Document*`へ変更した。** `Document::takePendingEdits()`が非constメソッドのため。既存の全呼び出し箇所はconstメソッドのみを呼んでいたため後方互換
- **`render_syntax_worker_test.cpp`の既存「無関係な2つのDocumentを連続要求→古い方は破棄される」テストは、Phase 7lで廃止する挙動そのものをピン留めしていたため書き直した。** 新版`RapidSequentialEditsNeverLoseAnEditEvenWhenCoalesced`は同一Documentへの連続編集を間を置かず2回要求し、ワーカーが2回を1回にまとめて処理しても最終トークンが最終テキストの全文書再解析と完全一致することを確認する。加えて`ResetIncrementalStateDiscardsStaleTreeAcrossUnrelatedDocument`(同一言語のまま無関係な別ドキュメントへ切り替えても保持木が正しく破棄されることの検証)を新設した
- **性能: 本フェーズは「取りこぼさないスレッド統合」という正しさの軸のみを達成し、`IncrementalParser::reparse()`自体が抱える「呼び出しのたびにトークン列全体を`walkTree()`で再構築する」ボトルネック(Phase 7k実測、約321ms/call)は未解消のまま。** roadmap §7.11のDoD「≤50ms」達成には次サブフェーズでの`ts_tree_get_changed_ranges()`対応が必要
- **実アプリでの視覚確認は本セッションでは実施できなかった。** 確立していたはずのスクリーンショット手法(`CopyFromScreen`)が機能せず(`GetWindowRect`/`IsWindowVisible`は正常値を返すがウィンドウ領域を撮るとデスクトップが写り込む、ウィンドウ中心への実クリックでもフォーカスが移らない)、これはこのセッションの自動化から実際に見えている画面にウィンドウが合成されていないことの一貫した証拠と判断した。代替として自動テスト(非同期ワーカー統合テスト4件、実スレッド・実メッセージ配送で検証)+プロセス生存確認(ファイルを開いた状態で約2分間`Responding=True`維持)で代替した。恒久的な環境退行と断定せず次回再検証する前提を`reference_no_win32_gui_automation.md`に記録した

**スコープ外(意図的、後続サブフェーズへ):** `ts_tree_get_changed_ranges()`による変更範囲限定トークン抽出(`walkTree()`全件再構築の解消、DoD達成に必要)、アウトライン抽出の増分化、複数言語を同時に保持するワーカー設計。詳細は`master_roadmap.md` §7参照。

---

### 10.15 増分再解析のトークン部分更新 (Phase 7m実装)

Phase 7l/7kが据え置いてきた性能課題(`reparse()`が呼び出しのたびにトークン列全体を`walkTree()`で再構築する)に着手した。`ts_tree_get_changed_ranges()`を使った内部最適化により定数倍の高速化は得られたが、期待していた漸近的改善は実測で否定された。

```cpp
// src/syntax/src/incremental_parser.cpp (無名namespace内、非公開)

// tree-sitterのバイト範囲同士が重なる/接触するかを判定する。TextRangeの
// 半開区間の慣習とは異なり、意図的に接触も重なりとみなす(数字直後への
// 数字挿入によるリーフ伸長・純粋削除によるゼロ幅変更範囲、いずれも接触型
// の境界ケースであり、両方とも実測で失敗するテストから発見した)。
bool rangesOverlap(uint32_t aStart, uint32_t aEnd, uint32_t bStart, uint32_t bEnd) noexcept;

// 各editの文字通りの範囲を、バッチ内の後続editを通じてts_range_edit()で
// 最終座標系へ前方伝播する。ts_tree_get_changed_ranges()単体では検出でき
// ない境界接触型の変更(構造は変わらずリーフの長さだけ変わる等)を捕捉する
// ための、木の構造差分とは独立した第2の「変更範囲」情報源。
std::vector<TSRange> computeDirtyRangesInFinalCoordinates(std::span<const ReparseEdit> edits);

// walkTree()の枝刈り版。ノードの範囲がchangedRanges(ts_tree_get_changed_
// ranges()の出力 + 上記dirty range)のどれとも重ならなければ、その部分木
// 全体を降りずにスキップし、位置シフト済みのoldTokensから該当区間のトーク
// ンをそのまま採用する。重なるノードは通常通り降りてappendLeafToken()で
// 新規分類する。
std::vector<Token> walkTreeIncremental(TSNode newRoot, const LeafKindTable& namedKinds,
                                        std::span<const Token> oldTokens,
                                        std::span<const TSRange> changedRanges);
```

**設計上の要点:**
- **`IncrementalParser`の公開契約は一切変更しなかった。** `reparse()`は引き続き「全文書再解析と完全一致する完全なトークン列を返す」契約のまま、内部実装だけを差し替えた。`render::SyntaxWorker`/`RenderPipeline`/`main.cpp`への変更は不要だった
- **`ts_tree_get_changed_ranges()`単体では不十分と実測で判明した。** 同APIは「新旧木で構文構造(祖先ノード)が変化した範囲」のみを報告し、数字の直後に数字を挿入して1つのリーフが伸びるだけ(構造自体は変わらない)といった境界接触型の変更では空配列を返す。対策として各editの文字通りの範囲(`computeDirtyRangesInFinalCoordinates()`)も無条件に「変更範囲」として扱う設計にした
- **範囲重なり判定(`rangesOverlap()`)は「接触も重なりとみなす」包含的な判定にした。** 純粋な削除(ゼロ幅の変更範囲)がノード境界を正しく検出できない失敗が実測で見つかったため
- **正しさは既存の「増分再解析結果 == 全文書再解析結果」というテストオラクルで証明した。** 境界条件(文書先頭/末尾)・未終端コメント挿入による構造カスケード・複数editバッチ・4回連続の増分再解析・Pythonを含む7件の新規テストを追加。テスト作成中に2件、テスト自体のオフセット計算ミス(実装ではなくテスト側)を自己発見・修正した
- **ベンチマーク実測(CLAUDE.mdルール10):** 5万行合成C++ソースで、単一文字編集を挟んだ増分再解析は約148ms/call(全文書再解析1243ms比で約8.4倍、Phase 7kの旧実装321ms比で約2.2倍)。**ただし50万行(10倍)版の同一ベンチマークが約1419ms/call(ほぼ10倍)となり、期待していた「文書サイズに依存しない一定コスト」は実測で否定された。** `reparse()`が依然として呼び出しのたびに文書全体サイズのトークン列を確保・返却する設計であり、`shiftTokensForEdits()`が保持トークン列全体を毎回舐める設計であることが根本原因と判明した。達成できたのはtree-sitterのAPI呼び出し(木のトラバース・型判定・ハッシュマップ検索)を安価な配列操作へ置き換えたことによる**定数倍**の高速化であり、計算量クラス自体の変更ではない。roadmap §7.11のDoD「≤50ms」は5万行の最良ケースでも未達のまま
- **真にO(編集サイズ)を達成するには、`IncrementalParser`の公開契約自体を「完全なトークン列」から「変更分の差分」を返す設計へ転換する必要があると判明した。** 呼び出し側(`SyntaxWorker`/`RenderPipeline`)が差分を永続化済みのトークン列へマージする責務を負うことになり、これはPhase 7kが当初のroadmapスケッチから意図的に外した設計そのもの。本フェーズはブラスト半径を`IncrementalParser`単体に抑えるためにこれを避けたが、次にDoD達成を目指すならこの契約変更が必要

**スコープ外(意図的、後続サブフェーズへ):** `IncrementalParser`の公開契約を「差分のみ返却」へ変更する設計(真のO(編集サイズ)達成に必要、`SyntaxWorker`/`RenderPipeline`側のマージロジック新設を伴う大規模変更)、残り21言語対応、ミニマップ、Sticky scroll。詳細は`master_roadmap.md` §7参照。

---

### 10.16 追加言語対応 バッチ1 (Phase 7n1実装)

`neomifes::syntax::Language`にC/JavaScript/Java/Go/Rust/Jsonを追加し(roadmap §7.2必須23言語のうち計8言語が完了)、`Language`→`TSLanguage*`の対応を1箇所に一元化した。

```cpp
// src/syntax/src/syntax_internal.h (detail名前空間、非公開)

enum class Language { Cpp, Python, C, JavaScript, Java, Go, Rust, Json };  // syntax.h

// syntax.cpp/incremental_parser.cpp/outline.cppの3ファイルが共有する
// 唯一のLanguage->TSLanguage*対応。Phase 7n1以前はincremental_parser.cpp
// のみが自前switchを持ち、outline.cppは2値の三項演算子で代用していた
// (Cpp以外は無言でPython文法へ誤って流し込まれる潜在バグだった - 8言語化
// で顕在化)。
[[nodiscard]] const TSLanguage* tsLanguageFor(Language language) noexcept;

// 真の葉(child_count()==0)、または「子を持つがLeafKindTableに直接
// エントリを持つ名前付きノード」ならtrue - 後者はtree-sitter-rustの
// line_comment/block_commentが非葉ノード(区切り文字"//"/"/*"/"*/"だけを
// 子に持ち、コメント本文はどの子にも属さない)であることが実機probeで
// 判明したための一般化。walkTree()/walkTreeIncremental()の両方で
// child_count()==0による葉判定をこれに置き換えた。
[[nodiscard]] bool isAtomicNode(TSNode node, const LeafKindTable& namedKinds);
```

**設計上の要点:**
- **6言語の選定基準は「tree-sitter公式organization配下で最新リリースタグをGitHub APIで直接確認できたこと」に絞った(CLAUDE.mdルール3)。** C(v0.24.2)・JavaScript(v0.25.0)・Java(v0.23.5)・Go(v0.25.0)・Rust(v0.24.2)・JSON(v0.24.8)。各文法がscanner.c(外部スキャナ)を要するかも`contents/src`のAPI応答で確認した(C/Java/Go/JSONはparser.cのみ、JavaScript/Rustは既存Python/Cppと同じ2ファイル構成)
- **`namedLeafKindsForX()`テーブルは全て実機probe(一時的なスタンドアロンプログラムでtree-sitterに実際にサンプルコードをパースさせ、`ts_node_type()`の出力をダンプ)で検証してから記入した。** probeプログラム自体はコミットしない(Phase 7a/7d/7fの前例通り)
- **`outline.cpp`の`extractOutline()`が持っていた2値の三項演算子(`language == Cpp ? tree_sitter_cpp() : tree_sitter_python()`)は、`Language`が2種類だった間は正しかったが8種類に増えた今、Cpp以外の全言語を無言でPython文法として誤ってパースする潜在バグだった。** `detail::tsLanguageFor()`への一元化でこれを修正。新6言語のシンボルテーブル(`symbolTableFor()`)は空の`SymbolTable`を返す(安全な劣化 — outline抽出ロジック本体は次バッチへ据え置き)
- **`isAtomicNode()`の一般化は、当初Rust対応のためだけに導入したが、Pythonの既知のギャップ(文字列エスケープを含む`string_content`が非葉ノードのため平文部分が無彩色になっていた、Phase 7dで「KNOWN, ACCEPTED gap」として文書化済み)も意図せず解消した。** 既存テストをこの改善された挙動に合わせて更新し、コメントで偶発的な副産物であることを明記した
- **`classifyAnonymousLeaf()`にバックティック(`` ` ``)を引用符として追加した。** GoのRaw文字列リテラルとJavaScriptのテンプレート文字列がどちらもバックティックの無名リーフを区切り文字に使うため(実機probeで確認)、既存の`"`/`'`と同じString扱いにした
- **`RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更は不要だった。** Phase 7dで確立済みの「`Language`引数を受け取るだけの汎用ディスパッチ」がそのまま新6言語でも機能する

**スコープ外(意図的、後続バッチへ):** TypeScript/PHP/HTML/CSS/XML/YAML/SQL/Markdown/PowerShell/VB/VBS/BAT/Shell/INI/TOML/SAP ABAP、新6言語のoutlineシンボル抽出ロジック本体。詳細は`master_roadmap.md` §7参照。

---

### 10.17 Sticky scroll (Phase 7o実装)

roadmap §7.6の実装。`Breadcrumb`(Phase 7h)のすぐ下に、現在の`topLine`が包含されている折り畳まれていないfold regionの見出し行を固定表示する。

```cpp
// src/render/src/render_pipeline.cpp (無名namespace内)
constexpr float kStickyScrollHeightDips = 24.0F;  // Breadcrumbと同じ高さ

// RenderPipeline (private)

// m_foldRegionsのうち折り畳まれていないregionで、(headerLine,
// endLineInclusive]がtopLineを含む最も内側(headerLineが最大)のものを
// 返す。折り畳み済みregionは除外(本文が非表示のため「スクロールして
// 本文に入り込む」状況が原理的に発生しない)。
std::optional<FoldVisual> stickyScrollRegionAt(document::LineNumber topLine) const noexcept;

// kBreadcrumbHeightDips + (stickyScrollRegionAt()があればkStickyScrollHeightDips)。
// drawVisibleLines()のy起点・実効高さ計算、hitTest()/hitTestFoldMarker()の
// yDipオフセットが共有する唯一の情報源(Phase 7i/7jのisLineHidden()/
// visibleLineAtRow()と同じ「3箇所以上で使う小さな共有ヘルパー」パターン)。
float reservedTopHeightDips() const noexcept;

// m_cachedSnapshotから単一行の生テキストを抽出(末尾の'\n'を除く)。
std::u16string extractLineText(document::LineNumber line) const noexcept;

// Breadcrumbのすぐ下に帯を描画。該当regionが無ければ何も描画しない
// (帯自体を出さない動的高さ)。プレーンテキスト、m_breadcrumbBackgroundBrush
// を再利用。renderOnce()内でdrawBreadcrumb()の直後に呼ばれる。
void drawStickyScroll(ID2D1DeviceContext6& dc) noexcept;
```

**設計上の要点:**
- **依存基盤の全てが既存だった。** `m_foldRegions`(Phase 7i)は折り畳み中かどうかに関わらず全regionの`headerLine`/`endLineInclusive`を保持しており、`m_topLine`は`main.cpp`の`syncRenderStateAndInvalidate()`が既に毎フレーム更新していた(そのヘッダコメントが「まだ誰も呼んでいない」という古い記述のままだったため本フェーズで修正した)。`main.cpp`側の新規配線は不要
- **帯は動的高さ(該当regionが無ければ描画しない)を採用した。** Breadcrumbの「常に固定高さの帯を描く」前例とは異なる判断 — enclosing scopeが無い場所(ファイル冒頭等)で常時空帯を表示し続けるのは視覚的ノイズになるため。この判断により、`kBreadcrumbHeightDips`を直接参照していた4箇所(`drawVisibleLines()`のy起点/実効高さ計算、`hitTest()`/`hitTestFoldMarker()`のyDipオフセット)を`reservedTopHeightDips()`へ一元化する必要が生じた
- **Sticky scroll行はプレーンテキスト描画(シンタックスハイライト無し)にした。** `drawBreadcrumb()`が自身の合成パス文字列に対して同じ簡略化を行った前例に倣った判断。`TextLayoutCache`(行番号キー)ではなく毎フレーム使い捨ての`IDWriteTextLayout`を使う点もBreadcrumbと同じ
- **背景ブラシは新規追加せず`m_breadcrumbBackgroundBrush`を再利用した。** Breadcrumbのすぐ下に連続して表示される同種の帯であり、新規ブラシ・新規デバイスロスト時リセット処理を増やさずに済んだ
- **正しさの検証は`hitTest()`のyオフセットを介した間接検証で行った。** `stickyScrollRegionAt()`/`reservedTopHeightDips()`は非公開のため、統合テストは`setTopLine()`で状態を設定した上で`hitTest()`が返すオフセットが「帯の高さが正しく反映された位置」を指すことを確認する形にした(`HitTestReturnsPositionsWithinKnownLineBounds`がBreadcrumbの帯について既に使っていた技法の踏襲)
- **実アプリでの視覚確認は、合成キーボード入力(矢印キー・PageDown)が今回反応せず断念した。** ウィンドウ所有プロセスIDの一致は確認済みで対象取り違えではなく、Phase 7l・7n1に続く3つ目の異なる失敗モード。統合テスト+プロセス生存確認で代替した

**スコープ外(意図的、後続サブフェーズへ):** ネストした複数regionのスタック表示、Sticky scroll行のシンタックスハイライト、行クリックでのジャンプ機能。詳細は`master_roadmap.md` §7参照。

### 10.18 LineIndex インクリメンタル更新 (Phase 7p実装、性能リグレッション修正)

Phase 7j〜7oの12コミットをまとめてpushした直後のCI (`gh run` 30367272798) が、`Build & Test (debug)`/`(release)`両ジョブとも`neomifes_core_bench.exe`実行中に停止したまま進まなくなり、6時間のジョブ上限でキャンセルされた。原因はPhase 7k (`document::EditDelta`導入) が`Document::insertText()`/`eraseRange()`/`replaceRange()`の中で編集の都度`offsetToLine()`を呼ぶようになったことで、`m_lineIndexDirty = true`をセットした直後にこれを呼ぶため**1回の編集ごとに必ず1回`LineIndex::build()`のO(文書長)フルスキャンが発生**するようになっていた。`BM_UndoStack_PushOneMillion`(100万回の逐次`insertText()`)はこれによりΣi (i=1..1,000,000) ≈ 5×10¹¹相当のO(N²)となり実質ハングしていた。詳細な経緯・実測値は[`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md)の追記セクション参照。

```cpp
// line_index.h — 新設 (「案C」、issue docの既存提案を採用)

// O(pos以降の行数 + textの改行数) - build()と違い文書全体を再走査しない。
// 文書末尾への挿入(タイピング・逐次追記の典型パターン)では実質O(1)。
void applyInsert(TextPos pos, std::u16string_view text);

// 同上の増分更新版erase。(range.start, range.end]内のline-startを削除し、
// range.end以降をrange.length()分左シフトする。
void applyErase(TextRange range);
```

`Document::insertText()`/`eraseRange()`/`replaceRange()`は`m_lineIndexDirty = true`をセットする代わりにこれらを直接呼ぶよう書き換えた(`replaceRange()`は`applyErase()`→`applyInsert()`の2段適用、`PieceTable::replace()`自身の「eraseしてからinsert」という意味論に合わせた)。結果、`m_lineIndexDirty`は構築直後の初回クエリでのみ意味を持ち、以降は`offsetToLine()`/`lineToOffset()`が常にO(log n)の二分探索で完結する。

**実測値 (Release、ローカル):** `BM_UndoStack_PushOneMillion` 412.5ms (修正前: CI 6時間タイムアウトで未完走)、`BM_UndoStack_UndoOneMillion` 267.1ms。要件定義書§5「Undo: 100万回以上」の定量的な裏付けにもなった(CLAUDE.mdルール10)。

**設計上の要点:**
- **Documentの公開契約(`offsetToLine`/`lineToOffset`/`EditDelta`の値)は一切変更していない。** 既存の`DocumentEditDeltaTest`群がそのままオラクルとして機能し、全件無変更でパスすることを確認した
- **`LineIndex`自体の`offsetToLine`/`lineToOffset`はO(log n)のまま変わらない。** 変わったのは「インデックスを最新に保つコスト」のみ。issue doc本来のスコープ(PieceTreeのツリー集約化によるオフセット↔行変換自体のO(log n)化、案A/B)は引き続き未着手
- **正しさの検証は`build()`相当の期待値との手計算突合で行った。** `LineIndex`はDocument経由でしか外部から触れないため、`document_line_index_test.cpp`に境界条件(先頭/末尾/既存行頭ちょうど/複数改行の挿入、削除範囲が複数行頭をまたぐ/ちょうど行頭で終わる)を狙った12件を追加し、末尾への逐次1文字挿入という実際にハングを起こしたシナリオそのものも回帰テストとして固定した

---

### 10.19 IncrementalParser 差分返却化 (Phase 7q実装、DoD未達)

Phase 7k〜7mの`incremental_parser.h`ヘッダコメント自身が「真のO(edit size)化には`reparse()`の契約を『差分のみ返却』へ変更し、呼び出し側が永続トークン列へマージする責務を持つ必要がある」と明記していた積み残し課題への着手。

```cpp
// incremental_parser.h — Phase 7q

struct TokenPatch {
    document::TextRange invalidatedRange;  // 最終(編集後)テキスト座標系
    std::int64_t         shiftAmount = 0;   // このバッチの全edits合計(newEnd-oldEnd)
    std::vector<Token>   replacementTokens; // invalidatedRange内の新規トークン
};

// tokensへpatchをマージする。invalidatedRangeと重なる既存トークンを破棄し、
// それ以降のトークンをshiftAmountだけシフトし、replacementTokensを挿入する。
// O(tokens.size() + replacementTokens.size())の単一線形パス。
[[nodiscard]] std::vector<Token> applyTokenPatch(std::vector<Token> tokens, const TokenPatch& patch);

// reparse()(完全なトークン列を返す契約)を完全に置き換え。
[[nodiscard]] TokenPatch reparseDelta(std::u16string_view text, std::span<const ReparseEdit> edits);
```

**設計:** tree-sitter公式ヘッダで`ts_node_descendant_for_byte_range(TSNode, start, end)`(「指定バイト範囲をspanする最小のノードを返す」)の存在を確認し、Phase 7mの`walkTreeIncremental()`(木全体をpre-order走査しつつ変更されていないノードだけ既存トークンをスプライスする)を、「変更範囲(`ts_tree_get_changed_ranges()` + 各editの字面上の範囲を1つの連続範囲にマージしたもの)を包含する最小の祖先ノードを1回で特定し、そのノード配下だけを既存の`detail::walkTree()`(Phase 7a、rootノード引数を取る汎用関数、無変更のまま再利用)で新規に歩く」設計に置き換えた。`shiftTokensForEdits()`/`walkTreeIncremental()`/`nodeOverlapsAnyChangedRange()`等、Phase 7mのロジックの大半を削除できた。永続トークン列の保持は`IncrementalParser`(Phase 7mの`lastTokens`)から呼び出し側(`render::SyntaxWorker::workerLoop()`)へ移し、`RenderPipeline::applyAsyncSyntaxTokens()`は無変更のまま。

**バグ修正:** 純粋な削除編集(`"12"→"1"`)は無効化範囲がゼロ幅([18,18)バイト)になり、`ts_node_descendant_for_byte_range()`がノード境界上のこのクエリに対して「削除により縮んだ`number_literal`ノード」ではなく無関係な直後のトークンを返してしまい、残るべきトークンが欠落するバグを実装直後のテストで発見した。ゼロ幅になる無効化範囲の開始位置を1コード単位(2バイト)後退させ、常に非ゼロ幅かつ削除位置の直前ノードを確実に含むよう修正した。

**実測値 (Release、ローカル):** `BM_IncrementalReparse_SingleCharEdit`(5万行) 103ms、`_LargeDocument`(50万行) 989ms。Phase 7m比で約30%の定数倍改善(148ms→103ms、1419ms→989ms)を達成したが、比率(約9.6倍/文書サイズ10倍)は依然としてほぼ線形であり、**roadmap §7.11のDoD「≤50ms」は未達のまま。**

**未達の原因:** `applyTokenPatch()`自体が「無効化範囲より後ろの全既存トークンをシフトする」というO(永続トークン列サイズ)の線形走査であり、tree-sitter側の再walkコストをO(edit size)化しても、マージ処理自体が文書サイズに比例するボトルネックとして残った。真のO(edit size)達成には、永続トークン列自体のデータ構造の再設計(可視範囲のみ保持する等)が必要と判明し、次サブフェーズへ意図的に据え置いた。CLAUDE.mdルール10(期待は大規模文書での追加ベンチマーク実測なしに完了報告に書いてはならない、Phase 7mで確立した規律)に従い、DoD未達を正直に記録する。

**スコープ外(意図的、後続サブフェーズへ):** 永続トークン列のデータ構造再設計(真のO(edit size)化)、複数の独立した変更範囲を個別のTokenPatchとして返す設計(1バッチ内の離れた複数editsは1つの連続範囲にまとめる簡略化を維持)。

---

### 10.20 追加言語対応 バッチ2 (Phase 7r実装)

`neomifes::syntax::Language`にHtml/Css/Shell/Yaml/Toml/Xmlを追加した(roadmap §7.2必須23言語のうち計14言語が完了)。Phase 7n1で確立した6言語追加の機械的パターン(FetchContent追加+実機probe+`namedLeafKindsForX()`テーブル+`parseX()`実装+`detectLanguage()`拡張)をそのまま再利用した。

```cpp
// src/syntax/include/neomifes/syntax/syntax.h
enum class Language { Cpp, Python, C, JavaScript, Java, Go, Rust, Json,
                       Html, Css, Shell, Yaml, Toml, Xml };  // Shell = tree-sitter-bash

[[nodiscard]] std::vector<Token> parseHtml(std::u16string_view text);
[[nodiscard]] std::vector<Token> parseCss(std::u16string_view text);
[[nodiscard]] std::vector<Token> parseShell(std::u16string_view text);
[[nodiscard]] std::vector<Token> parseYaml(std::u16string_view text);
[[nodiscard]] std::vector<Token> parseToml(std::u16string_view text);
[[nodiscard]] std::vector<Token> parseXml(std::u16string_view text);
```

**言語選定 (CLAUDE.mdルール3、`gh api`によるGitHub直接確認):** roadmap §7.2残り15言語のうち、tree-sitter公式org(`tree-sitter/`)・準公式org(`tree-sitter-grammars/`)配下に存在する9言語(TypeScript/PHP/HTML/CSS/Shell/YAML/TOML/Markdown/XML)を発見。うちTypeScript/PHP/Markdownは1リポジトリに複数`src/`ディレクトリ(文法)が同居し主要文法選択の設計判断を要するため、AskUserQuestionでユーザーに確認の上、単一`src/`構造の6言語(HTML/CSS/Shell/YAML/TOML/XML)へ絞った。SQL/PowerShell/VB/VBS/BAT/INIは公式org不在(コミュニティ文法のみ)のため対象外。

**非葉ノードの新規発見 (`isAtomicNode()`のRust以来2件目・3件目の適用例):**
- TOMLの`string`ノードは子が引用符2つのみ(内容を持つ子ノードが無い)非葉ノード。`namedLeafKindsForToml()`に`{"string", TokenKind::String}`を登録しないと、引用符内のテキストがトークンストリームから丸ごと欠落する
- XMLの`AttValue`ノードも同型(属性値の引用符2つのみ)。同じ理由で`namedLeafKindsForXml()`に登録した

**文法自体の構造的曖昧さ(受容した既知の制約):**
- YAMLの`string_scalar`はマッピングキーと値の両方に使われ、区別する専用ノード型が無い(キーもStringとして着色される、JSONのオブジェクトキー/文字列値が`string_content`を共有する既存の前例と同種)
- XMLの`Name`は要素タグ名と属性名の両方に使われる専用ノード型が無い(属性名もTypeとして着色される)

**YAMLビルド:** `src/`が`parser.c`/`scanner.c`に加え`schema.core.c`/`schema.json.c`/`schema.legacy.c`の3ファイルを要することを実機ビルドで確認(YAML文法自体がスキーマ検証をスキャナに埋め込んでいる)。XMLは`xml/`+`dtd/`の2ディレクトリ構成だが`xml/`が明確に主要文法であり単一文法として扱った。

**設計上の要点:**
- **6言語すべて、Phase 7n1確立の「正しい文法選択+空`SymbolTable`」パターンをそのまま踏襲した。** `detail::tsLanguageFor()`/`namedKindsFor()`/`symbolTableFor()`の一元化switchに6ケースずつ追加するだけで済み、Phase 7n1が修正した「三項演算子による誤パース」バグの再発を構造的に防げた
- **`RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更は不要だった。** Phase 7dで確立済みの汎用ディスパッチがそのまま新6言語でも機能する
- **正しさの検証は実機probeの生出力(スタンドアロンプログラムでtree-sitterに実際にパースさせたノードダンプ)からトークン列を手計算し、`syntax_syntax_test.cpp`の各言語テストでトークン数・種別・範囲を直接アサートする形で行った(推測実装をしない、CLAUDE.mdルール3)。** 全834件のローカルテストがgreenであることで手計算の正しさを裏付けた
- **実アプリでの視覚確認は、過去複数セッションのスクリーンショット/入力合成不調を踏まえ、`--open`引数でHTML/YAMLサンプルを開きプロセスが3秒後も生存していることを確認する軽量スモークテストに切り替えた**

**スコープ外(意図的、後続バッチへ):** TypeScript/PHP/Markdown(複数文法サブディレクトリの主要文法選択判断が必要)、SQL/PowerShell/VB/VBS/BAT/INI/SAP ABAP(公式org不在)、新6言語のoutlineシンボル抽出ロジック本体。詳細は`master_roadmap.md` §7参照。

---

### 10.21 追加言語対応 バッチ3 (Phase 7s実装)

`neomifes::syntax::Language`にTypeScript/Tsx/Php/Markdownを追加した(roadmap §7.2必須23言語のうち計18言語が完了)。

```cpp
// src/syntax/include/neomifes/syntax/syntax.h
enum class Language {
    Cpp, Python, C, JavaScript, Java, Go, Rust, Json, Html, Css, Shell, Yaml, Toml, Xml,
    TypeScript, Tsx, Php, Markdown
};

[[nodiscard]] std::vector<Token> parseTypeScript(std::u16string_view text);
[[nodiscard]] std::vector<Token> parseTsx(std::u16string_view text);
[[nodiscard]] std::vector<Token> parsePhp(std::u16string_view text);
[[nodiscard]] std::vector<Token> parseMarkdown(std::u16string_view text);
```

**言語選定・主要文法判断 (CLAUDE.mdルール3、`gh api`/`curl`によるGitHub直接確認):** Phase 7rで「主要文法選択の判断が必要」として据え置いたTypeScript/PHP/Markdownの3リポジトリを個別に精査した結果、実際に判断が必要だったのはPHPのみと判明した。

- **TypeScript(`tree-sitter/tree-sitter-typescript` v0.23.2)の`typescript/`と`tsx/`は、どちらも`parser.c`+`scanner.c`+`grammar.json`+`node-types.json`を完備した独立した完全な文法で、拡張子(`.ts`/`.tsx`)により使い分ける設計(公式`CMakeLists.txt`が両者を並列`add_subdirectory()`している)。** 「主要文法を1つ選ぶ」判断は不要と判明し、`Language::TypeScript`/`Language::Tsx`の2エントリを追加した
- **PHP(`tree-sitter/tree-sitter-php` v0.24.2)の`php/`(完全な文法、`<?php ?>`タグ+埋め込みHTML込み)と`php_only/`(タグなし純PHPコード専用、他言語への埋め込み用途)は、実際に`.php`ファイルを開く用途では`php/`が唯一の正解であり曖昧さは無い。** `php_only/`は対象外
- **Markdown(`tree-sitter-grammars/tree-sitter-markdown` v0.5.3)の`tree-sitter-markdown/`(ブロック)と`tree-sitter-markdown-inline/`(インライン: 強調/リンク/インラインコード等)は「主要文法を選ぶ」構造ではなく、tree-sitterの言語注入(language injection)機構でブロック文法がインライン文法を段落テキストへ注入する設計(nvim-treesitter等の標準パターン)。** `neomifes::syntax`には言語注入の仕組みが無く新設は非自明な拡張を要するため、v1はブロック文法のみ採用しインライン文法は対象外とした

**TypeScript/TSXのCMake配線 (`cmake/Dependencies.cmake`):** 両者のscanner.cはリポジトリルート直下の`common/scanner.h`を`#include "../../common/scanner.h"`という相対パスで参照する(実ファイル確認済み)。公式`CMakeLists.txt`自身も`common/`を`target_include_directories`に追加していない(相対includeが自動解決するため)ので、追加のインクルードパス設定は不要だった。

**`namedLeafKindsForX()`テーブルの共有設計:**
```cpp
// syntax_internal.h
// TypeScriptはJavaScriptの表(Phase 7n1)と大部分を共有 - tree-sitter-
// typescriptがtree-sitter-javascriptの文法を拡張する公式アーキテクチャ。
// ただし共有元の各エントリ(comment/identifier/string_fragment/escape_
// sequence/regex_pattern/regex_flags/true/false/null/undefined/this/
// super)はTypeScript側でも独立して実機probeし直し、名前が同一であること
// を確認してから記入した(継承関係からの類推だけで済ませない、CLAUDE.md
// ルール3)。type_identifier/predefined_typeはTypeScript固有の追加。
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForTypeScript() { ... }

// TSXはJSX固有の新規named leaf型が実機probeで見つからなかったため、
// TypeScriptの表をそのまま再利用する(別テーブルを複製しない)。
[[nodiscard]] inline const LeafKindTable& namedLeafKindsForTsx() {
    return namedLeafKindsForTypeScript();
}
```

**`predefined_type`のisAtomicNode()登録 (データ欠落回避ではなく一貫性のための選択):** TypeScriptの組み込み型キーワード(`number`/`string`等)を表す`predefined_type`ノードは非leaf(子1つ、親と同一バイト範囲を覆う無名子のみ)だが、子が既に全範囲をカバーしているためTOMLの`string`/XMLの`AttValue`(Phase 7r)のような「登録しないとテキストが欠落する」バグではない。それでもテーブルへ登録し、Cpp/Rustの`primitive_type`と一貫してTypeとして着色する設計にした(未登録のままだと`classifyAnonymousLeaf()`の「全アルファベット文字ならKeyword」ルールでKeyword扱いになり、他言語の組み込み型表示と食い違う)。

**設計上の要点:**
- **PHPの`php_tag`/`php_end_tag`(`<?php`/`?>`)はPreprocessorに分類した。** タグ外の埋め込みHTML(`text`ノード)は無彩色のまま(HTMLのraw_text/CSSのplain_valueと同種の「組み込み言語のネストハイライトはしない」簡略化)
- **Markdownはブロック文法のみのため、`` ` ``(バックティック)と`*`が既存の`classifyAnonymousLeaf()`のルール(バックティックは引用符扱い→String、`*`は非アルファベット→Punctuation)により偶発的にインライン区切り文字として着色される。** 意図した機能ではなく、既存ロジックの無害な副産物として文書化した(段落本文自体は無彩色のまま)
- **6言語すべて、Phase 7n1/7r確立の「正しい文法選択+空`SymbolTable`」パターンをそのまま踏襲した。** `RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更は不要(Phase 7dの汎用ディスパッチがそのまま機能する)
- **正しさの検証は実機probeの生出力からトークン列を手計算し、`syntax_syntax_test.cpp`の各言語テストで直接アサートする形で行った。** 全864件のローカルテストがgreenであることで手計算の正しさを裏付けた

**スコープ外(意図的、後続バッチへ):** SQL/PowerShell/VB/VBS/BAT/INI/SAP ABAP(公式org不在)、Markdownのインライン文法+言語注入機構の新設、新4言語のoutlineシンボル抽出ロジック本体。詳細は`master_roadmap.md` §7参照。

---

### 10.22 可視範囲スコープ化トークン再設計 (Phase 7t実装)

Phase 7qが明示的に積み残した課題(`incremental_parser.h`のヘッダコメント「Reaching true O(edit size) end-to-end would require restructuring how the persisted token list itself is stored」)への着手。`TokenPatch`/`applyTokenPatch()`/`reparseDelta()`を丸ごと廃止し、`m_tokens`自体が文書全体ではなく可視範囲(+プリフェッチ余白)のみをカバーする設計へ全面置換した。

```cpp
// incremental_parser.h — Phase 7t (TokenPatch/applyTokenPatch()/reparseDelta()を置き換え)
[[nodiscard]] std::vector<Token> reparseRange(std::u16string_view text, std::span<const ReparseEdit> edits,
                                               std::uint32_t rangeStartByte, std::uint32_t rangeEndByte);
```

**設計:** `ts_tree_edit()`を全editsに適用→`ts_parser_parse_string_encoding()`で再解析、の前半はPhase 7qと変わらない(tree-sitter自身の内部増分再利用に必要)。後半はPhase 7qが行っていた「`ts_tree_get_changed_ranges()`で変更範囲を特定→`computeDirtyRangesInFinalCoordinates()`で字面上の編集範囲とマージ→その範囲を覆うノードをウォーク→`TokenPatch`(無効化範囲+シフト量+置換トークン)を組み立てる」という一連の処理が全て不要になった。呼び出し側が渡した`[rangeStartByte, rangeEndByte)`を`ts_node_descendant_for_byte_range()`でそのままノード解決し、`detail::walkTree()`(Phase 7a、無変更のまま再利用)でウォークした結果をそのまま返すだけになる。返却契約は「要求範囲を**少なくとも**カバーする」(tree-sitterの最小包含ノードの性質上、要求範囲より広がることがある — 構文的にネストした親ノードの端まで広がるのは、tree-sitterベースのハイライタ全般が持つ既知の粒度特性)。

**`drawTokensOnLine()`は無変更で済んだ:** この関数(Phase 7b)は`m_tokens`(ソート済み)に対する単調な`tokenCursor`スイープであり、「トークンが無い区間はデフォルトブラシで描画される」を既に前提として実装されていた。`m_tokens`が可視範囲だけをカバーする(範囲外は「ギャップ」として無彩色になる)設計に変えても、ペイントロジック自体には一切手を入れる必要がなかった。

**`SyntaxWorker`/`main.cpp`側の変更:**
```cpp
// syntax_worker.h — requestParse()にrange引数を追加(snapshot/languageと同じ
// 「最新優先」、editsのように蓄積はしない)
void requestParse(std::shared_ptr<const document::BufferSnapshot> snapshot,
                  syntax::Language language, std::vector<document::EditDelta> edits,
                  bool resetIncrementalState, document::TextRange range) noexcept;
```
`workerLoop()`のループローカル変数`persistedTokens`(Phase 7q)は完全に削除し、毎イテレーション`parser->reparseRange(text, reparseEdits, range.start*2, range.end*2)`を呼んでその結果をそのまま`kMsgSyntaxTokensReady`で送る。**`kMsgSyntaxTokensReady`のペイロード形状・`main.cpp`のハンドラ・`RenderPipeline::applyAsyncSyntaxTokens()`のシグネチャは無変更で済んだ** — `SyntaxWorker`は単一バックグラウンドスレッドで直列に1件ずつリクエストを処理する設計(Phase 7c以来不変)であり、古いレスポンスが新しいレスポンスより後に届くという競合は構造的に起こり得ないため、レスポンスに「実際にカバーした範囲」を含める必要が無いと判明した。

**`RenderPipeline`側のトリガー統合:** 純粋なスクロール(`setTopLine()`のみ変化、編集なし)では`Document::version()`が変わらないため、既存の`refreshDocumentCacheIfStale()`はそもそも呼び出し本体まで到達しない(早期return)。新規`ensureSyntaxTokensCoverVisibleRange()`を新設し、`renderOnce()`から`refreshDocumentCacheIfStale()`の直後に毎フレーム無条件で呼ぶことで、「編集された」(`refreshDocumentCacheIfStale()`が`m_pendingSyntaxEdits`/`m_forceFullReparseNextRequest`へ暫定的にステージ)と「スクロールで可視範囲が要求済み範囲(`m_requestedTokenRange`)からはみ出た」の両トリガーを1箇所に統合した。可視範囲+余白は既存の`drawVisibleLines()`の可視行計算ロジックを`visibleLineRange()`として抽出・共有し、新規`viewport_math.h::widenLineRangeWithMargin()`(可視行数と同じだけ上下に1画面分、文書境界でクランプ)で広げる。

**ベンチマーク実測 (Release、`syntax_parse_bench.cpp`の`BM_ReparseRange_SingleCharEdit_*`):**

| ベンチマーク | 実測値 | Phase 7q比 |
|---|---|---|
| 5万行、narrow window(~150行) | 15.65ms | 103ms→15.65ms(約6.6倍) |
| 50万行、narrow window(~150行) | 155.95ms | 989ms→155.95ms(約6.4倍) |
| 50万行、full document(文書全体) | 155.45ms | (参考、narrow windowとほぼ同一) |

5万行ではroadmap §7.11のDoD「≤50ms」を達成した。しかし50万行ではnarrow windowとfull documentのコストがほぼ同一(155.95ms vs 155.45ms)になっており、**ウォーク範囲を絞ってもコストが変わらないことから、ボトルネックが`applyTokenPatch()`から`ts_parser_parse_string_encoding()`自体(文字列ベースAPIの制約で常に文書全体のテキストを要求する、文書サイズに比例するtree-sitter自身の再解析コスト)へ完全に移ったと確認した。** このベンチマークは既に実体化された`std::u16string`に対して`reparseRange()`を直接計測するもので、`SyntaxWorker::workerLoop()`が実際に払う`BufferSnapshot::extract()`(文書全体のテキスト実体化)のコストは含まない — つまり50万行の実際の per-keystroke コストはこの155ms以上になりうる。

**未達の原因と次候補:** 大規模文書のDoD達成には、tree-sitterの`TSInput.read`コールバックAPIを`document::BufferSnapshot`/`PieceTable`に対して実装し、文書全体のテキスト実体化・再解析自体を回避する必要があると判明した。これは本フェーズより大きな別のアーキテクチャ変更であり、次サブフェーズの課題として明記する。CLAUDE.mdルール10に従い、DoD未達を正直に記録する。

**スコープ外(意図的、後続サブフェーズへ):** `TSInput`コールバックAPI採用、余白サイズ(1画面分)のチューニング、大きくジャンプした際の一時的な無彩色表示の緩和、`extractOutline()`(Breadcrumb)の可視範囲スコープ化(Phase 7h以来の独立した同期・全文書解析のまま継続)。

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
| `RenderPipeline` | UI Thread のみ (内部の`SyntaxWorker`だけが別スレッド、下記) |
| `neomifes::render::SyntaxWorker` (Phase 7c実装、Phase 7dでLanguage引数対応) | 本コードベース初の`std::thread`。専用ワーカー1本(Worker Poolではない、§10.5参照)が`BufferSnapshot`を消費し`syntax::parse(text, language)`を実行、結果は`PostMessageW`(`WM_APP+2`)でUIスレッドへ通知 |
| `SearchService` | Worker Pool、結果はキュー経由 (未実装、roadmapスケッチのまま) |
| `PluginContext` | プラグインごとにアフィニティ (別スレッドから呼ばない) |

- **不変オブジェクト + shared_ptr 配布** を軸にロックを最小化
- どうしても必要な排他は `std::shared_mutex`、細粒度化
- `SyntaxWorker`は`std::mutex`+`std::condition_variable`による単一スロットのリクエスト合流(キューではない)、詳細は§10.5参照

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
