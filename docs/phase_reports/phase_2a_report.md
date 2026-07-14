# Phase 2a 完了レポート — Document Engine (MVP)

- **期間:** 2026-07-14 (継続)
- **担当:** Claude Code (テックリード)
- **対象:** CLAUDE.md §7 Phase 2 のうち **API・正しさ・テストインフラ** を先行して確定。**性能最適化 (RB-tree / Lazy Decode / mmap)** は Phase 2b に切り出し。

Phase 2a の目的は「Document Engine の公開 API を確定させ、そのインターフェースを差し替え可能な MVP 実装 + 包括的テストで担保する」こと。1GB ファイルベンチの達成は Phase 2b の DoD とする。

---

## 1. 追加成果物

### 1.1 document レイヤ (L3)
| ファイル | 役割 |
|---|---|
| [text_pos.h](../../src/document/include/neomifes/document/text_pos.h) | `TextPos` (UTF-16 CU offset) / `TextRange` / `LineNumber` |
| [piece.h](../../src/document/include/neomifes/document/piece.h) | `Piece` 構造体 (source/offset/length/newlineCount)。`sizeof(Piece)<=32` を static_assert |
| [add_buffer.{h,cpp}](../../src/document/src/add_buffer.cpp) | append-only UTF-16 バッファ |
| [original_buffer.{h,cpp}](../../src/document/src/original_buffer.cpp) | 読み取り専用 UTF-16 バッファ (**MVP: 全読み込み**、Phase 2b で mmap + Lazy Decode) |
| [buffer_snapshot.{h,cpp}](../../src/document/src/buffer_snapshot.cpp) | immutable ビュー。`extract(range)` で UTF-16 化 |
| [piece_table.{h,cpp}](../../src/document/src/piece_table.cpp) | **MVP: std::vector<Piece>**。insert / erase / replace / snapshot |
| [line_index.{h,cpp}](../../src/document/src/line_index.cpp) | 改行インデックス。`offsetToLine` / `lineToOffset` |
| [document.{h,cpp}](../../src/document/src/document.cpp) | ファサード。PieceTable + LineIndex 統合、lazy rebuild |
| [file_loader.{h,cpp}](../../src/document/src/file_loader.cpp) | UTF-8 (BOM 対応) 同期読込。Phase 6 前提で BOM 剥がしのみ |

### 1.2 util レイヤ (Phase 1 宿題消化)
| ファイル | 役割 |
|---|---|
| [wchar_cast.h](../../src/util/include/neomifes/util/wchar_cast.h) | `char16_t ↔ wchar_t` の 0 コスト変換ヘルパ (detailed §22.1) |

### 1.3 テスト
| ファイル | ケース数 | 内容 |
|---|---|---|
| [document_add_buffer_test.cpp](../../tests/unit/document_add_buffer_test.cpp) | 4 | append/view/範囲外/空文字 |
| [document_piece_table_test.cpp](../../tests/unit/document_piece_table_test.cpp) | 14 | insert/erase/replace の境界ケース、Original シード、surrogate pair、snapshot 分離 |
| [document_line_index_test.cpp](../../tests/unit/document_line_index_test.cpp) | 5 | 空/単一/複数行/末尾改行/変更後リビルド |
| [document_property_test.cpp](../../tests/unit/document_property_test.cpp) | 1 (2000反復) | ランダム操作 vs `std::u16string` 等価性検証 |
| [document_file_loader_test.cpp](../../tests/unit/document_file_loader_test.cpp) | 7 | ASCII / BOM / 多バイト / surrogate / 不正 UTF-8 / NotFound / TooLarge |

### 1.4 ベンチ
| ファイル | 内容 |
|---|---|
| [document_piece_table_bench.cpp](../../tests/bench/document_piece_table_bench.cpp) | `InsertAtEnd` / `InsertRandom` / `Snapshot` / `ExtractAll` の 4 本 |

### 1.5 Issue 起票 (Phase 2b 引継ぎ)
- [docs/issues/piece_table_rb_tree.md](../issues/piece_table_rb_tree.md) — vector→RB-tree 置換
- [docs/issues/lazy_decode_mmap.md](../issues/lazy_decode_mmap.md) — Original の mmap + Lazy Decode 化

---

## 2. 設計上の意図的な MVP 縮退 (Phase 2b で解消)

| 項目 | 詳細設計 §3 のあるべき姿 | Phase 2a 実装 | Phase 2b で戻す |
|---|---|---|---|
| Piece コンテナ | RB-tree + 順序統計木 | `std::vector<Piece>` (O(n) 線形) | tree 差し替え、API は据え置き |
| snapshot() | 参照コピー O(1) | vector 全コピー O(n) | path-copying または RCU |
| OriginalBuffer | mmap + Lazy Decode | 全読み込み UTF-16 | mmap + LRU デコードキャッシュ |
| LineIndex | tree の集約から O(log n) | 変更のたび O(N) 再スキャン | tree の順序統計から派生 |
| encoding 対応 | UTF-8/16LE/16BE/32/SJIS/EUC-JP/ISO-2022-JP + 自動判定 | UTF-8 のみ | Phase 6 (Encoding Engine) 側で拡張 |
| async loader | Worker で並列読込 | 同期のみ | `std::async` + キャンセルトークン |

**API は据え置き**。Phase 2b は「実装差し替え + テスト再走行」で完了する構造を作った。

---

## 3. DoD 判定

| 項目 | 状態 | 補足 |
|---|---|---|
| **Phase 2a: Document Engine API 確定** | ✅ | 6 モジュール + ファサード |
| **Phase 2a: 単体テスト網羅** | ✅ | 31 ケース + 2000 反復プロパティテスト |
| **Phase 2a: ベンチマークインフラ** | ✅ | 4 本の基準ベンチを CI に載せられる状態 |
| Phase 2 全体 DoD (1GB 読込ベンチ通過) | ⏳ **Phase 2b で達成** | Issue 起票済み |

---

## 4. 制約付き検証状況

本エージェント環境には MSVC が無いためローカルビルドは未実施。ユーザー側で以下を確認予定:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
# Bench (Release)
cmake --preset release
cmake --build --preset release
./build/release/tests/bench/neomifes_document_bench.exe --benchmark_min_time=0.1s
```

または GitHub Actions CI で自動確認。

---

## 5. 既知の懸念事項

| # | 懸念 | 対応 |
|---|---|---|
| P2a-1 | `PieceTable` は vector ベースのため 10 万編集で O(n²) に劣化 | 意図的な MVP。Phase 2b で RB-tree 化 (Issue 起票済) |
| P2a-2 | プロパティテストが 2000 反復で軽め | Phase 2b で fuzz 化 (20,000 反復 + より広い演算子集合) |
| P2a-3 | `OriginalBuffer` の 512MB 上限 (`FileLoader::maxBytes`) は暫定 | Phase 2b の mmap 対応で 10GB 上限に拡張 |
| P2a-4 | `LineIndex.build` が O(N) の再スキャン + 全ピース extract | Phase 2b で解消 |
| P2a-5 | `WarningsAsErrors: '*'` 未切替 (Phase 0.5 P05-4) | CI 初回 green 後に切替。Phase 2b に持ち越し |

---

## 6. Phase 2b への引き継ぎ事項

順序:
1. **`piece_tree.h/cpp` を追加** (RB-tree + 順序統計、LineIndex 集約を内包) — Issue: piece_table_rb_tree
2. **`PieceTable` 実装だけ差し替え**、既存全テスト green を維持
3. **`OriginalBuffer` を mmap + Lazy Decode 化** — Issue: lazy_decode_mmap
4. **1GB モックファイル生成 + load ベンチ**、Working Set 差分測定
5. **`WarningsAsErrors: '*'`** に切替
6. **Named Mutex 単一インスタンス化** (basic §2.3、Phase 1 宿題)
7. **CI に clang-cl UBSan ジョブ追加** (self-review R4)

---

## 7. Definition of Done

**Phase 2a 完了。Phase 2b (RB-tree + Lazy Decode + 1GB ベンチ) 着手可能。**
