# ADR-008: Direct2D/DXGI/DirectWrite の COM オブジェクトは Microsoft::WRL::ComPtr で所有する

- **ステータス:** Accepted
- **決定日:** 2026-07-16 (Phase 3a 着手時)
- **関連:** [`docs/design/detailed_design.md`](../design/detailed_design.md) §4、[`docs/handoff/RESUME_HERE.md`](../handoff/RESUME_HERE.md)

## コンテキスト

Phase 3a で `src/render/` レイヤを新設し、Direct2D/DXGI/DirectWrite の COM インターフェース (`ID2D1Factory7`, `IDWriteFactory7`, `ID3D11Device`, `IDXGISwapChain2`, `ID2D1DeviceContext6` 等) を初めて扱う。CLAUDE.md §4 は「ハンドル(HWND/HDC/HANDLE)は必ず RAII ラッパで包む」「所有権: `std::unique_ptr` > `std::shared_ptr` > 生ポインタ(observer only)」を規定しており、既存の `src/platform/include/neomifes/platform/handle_guard.h` が Win32 ハンドル向けの `HandleGuard<Handle, Deleter, InvalidValue>` テンプレートと専用の `FileHandle` クラスを提供している。

COM オブジェクトのライフタイム管理はこれらとは異なる意味論を要求する:

- COM は **参照カウント方式** (`AddRef`/`Release`) であり、コピーのたびに `AddRef` が必要 — `HandleGuard` はコピー禁止 (move-only) 設計で、この意味論に合わない
- `QueryInterface` によるインターフェース間キャストが頻繁に発生する (例: `ID3D11Device` → `IDXGIDevice` → `ID2D1Device`)
- `IUnknown` 派生インターフェースは共通の解放プロトコル (`Release()`) を持つため、汎用ラッパで表現しやすい

## 選択肢

1. **`Microsoft::WRL::ComPtr<T>`** — Windows SDK 同梱のヘッダオンリーテンプレート (`<wrl/client.h>`)。`AddRef`/`Release` を正しく実装したコピー可能スマートポインタ。`As<U>()` で `QueryInterface` をラップ。
2. **`HandleGuard` を COM 用に拡張** — `Deleter` を `Release()` 呼び出しにした新しいインスタンス化、またはコピー時 `AddRef` する新テンプレートを追加
3. **生ポインタ + 手動 `AddRef`/`Release`** — 却下 (CLAUDE.md の RAII 徹底ルールに明確に反する)

## 決定

**`Microsoft::WRL::ComPtr<T>` を採用する。** `src/render/` 配下の全ての COM インターフェースポインタ (`ID2D1Factory7` 以下) はこの型で保持する。

## 根拠

- **車輪の再発明を避ける:** `ComPtr` は `AddRef`/`Release`/コピー/ムーブ/`As<U>()` を Microsoft 自身が正しく実装済みで、Windows SDK に同梱 (追加の外部依存なし、CLAUDE.md「外部ライブラリ追加は最小限」に抵触しない — SDK 標準ヘッダの範囲内)。COM の参照カウント意味論を `HandleGuard` に後付けすると、本質的に `ComPtr` の再実装になる
- **`detailed_design.md` §4.1 が既に `ComPtr` 前提で書かれている:** `RenderPipeline` のクラススケッチが `Microsoft::WRL::ComPtr<ID2D1Factory7>` 等を明記しており、この ADR は既存設計判断を正式に記録する意味合いが強い
- **`/GR-` (RTTI 無効) と両立する:** COM の `QueryInterface`/`ComPtr::As<U>()` は vtable ベースであり RTTI (`dynamic_cast`) を必要としない。CLAUDE.md の `dynamic_cast` 禁止方針と無矛盾
- **本プロジェクト初の COM コードなので前例を残す価値がある:** 将来 Phase 5 (Search Engine) 等で COM を使う可能性がある場合、この ADR が参照点になる

## 影響

- `src/render/` の全ヘッダ/実装は `#include <wrl/client.h>` を用いる
- COM 呼び出しの失敗 (`HRESULT` が `FAILED()`) は例外を投げず、`RenderExpected<T> = std::expected<T, RenderError>` で呼び出し元に伝播する (回復可能エラーは `std::expected`/`std::optional` という CLAUDE.md 方針との整合)
- `HandleGuard`/`FileHandle` は Win32 ハンドル専用のまま変更しない — COM 用の新しいラッパを追加する必要はない

## 却下した選択肢の理由 (補足)

`HandleGuard` 拡張案は、コピー時 `AddRef` する挙動を追加すると既存の「move-only」設計思想と矛盾するため、実質的に別テンプレートが必要になり、`ComPtr` を採用する場合と比べてメンテナンスコストに見合うメリットがないと判断した。
