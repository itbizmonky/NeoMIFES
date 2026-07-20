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
