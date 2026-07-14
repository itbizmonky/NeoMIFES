# Issue: OriginalBuffer を Memory-mapped + Lazy Decode 化する

- **起票日:** 2026-07-14 (Phase 2a 完了時)
- **対象:** [`src/document/src/original_buffer.cpp`](../../src/document/src/original_buffer.cpp)
- **優先度:** 高 (10GB × 20MB を両立させる要件)

## 背景

Phase 2a MVP の `OriginalBuffer` はファイル全体を `std::u16string` に読み込む。1GB ファイルなら 2GB (UTF-8→UTF-16 の 2 倍化) がメモリに乗ってしまい、要件 (10GB 対応、初期メモリ 20MB) を満たせない。

## 設計 (詳細設計 §3.1 準拠)

```
OriginalBuffer:
  - HANDLE hFile
  - HANDLE hMapping                              // CreateFileMappingW
  - std::mutex viewsMutex
  - LRUCache<ChunkKey, MappedView> viewCache    // 1GB 単位でマップ、上限 N 枚
  - LRUCache<ChunkKey, std::u16string> decodeCache  // 64KB 単位で UTF-16 化
  - encoding::Encoding encoding
```

- `rawBytes(off, len)` → 該当バイト範囲を包含するビューを LRU に確保して span を返す
- `decodeUtf16(byteOff, byteLen, enc)` → 64KB 境界丸めチャンク単位で復号し、キャッシュに保持したまま `u16string_view` を返す
- `Piece.offset` の意味を **UTF-16 CU オフセット** に統一するため、各チャンクに「バイトオフセット → CU オフセット」のマッピングテーブルを持つ

## 実装ステップ提案

1. `MappedView` (RAII: `MapViewOfFileEx` + `UnmapViewOfFile`) を `src/platform/` に追加
2. `OriginalBuffer` の実装を差し替え、既存 `view()` API は保持 (ヘッダ据え置き)
3. `FileLoader` から生の `u16string` を渡す経路を廃止し、mmap 経由に一本化
4. **`PieceTable` の Piece.offset 意味変換** (Original ソースでは UTF-16 CU 数を保持するが、内部でバイト範囲を求めるためのマッピング)
5. 単体テスト: 各エンコード (Phase 6 の Encoding Engine 完成後)、大ファイルシナリオ (mock 10GB スパースファイル)
6. ベンチ: 1GB ファイル load-and-open ≤ 2s、Working Set 増分 ≤ 30MB

## リスク

- Windows のファイルロック挙動 (`FILE_SHARE_READ | FILE_SHARE_WRITE`) をどう設計するか
- ネットワークドライブ上のファイルは mmap で `EXCEPTION_IN_PAGE_ERROR` が起きうる → 例外 → SEH フィルタで検出し `LoadError::IoFailure` を返す
- 32bit プロセスは考慮しない (プロジェクトは x64 固定、CMakeLists.txt で `CMAKE_SIZEOF_VOID_P == 8` 検証済み)

## 完了条件

- [ ] 1GB UTF-8 ファイルを 2 秒以内にオープン
- [ ] オープン直後の Working Set 増分が 30MB 未満
- [ ] 既存 `document_file_loader_test` が全て pass
- [ ] mmap ビュー数上限 (例: 8 枚) を超えたときに LRU 追い出しが正しく動作する
