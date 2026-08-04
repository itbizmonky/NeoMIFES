# Issue: 文書保存機能が存在しない (P0 — 出荷不能)

**✅ ほぼ解消 (2026-08-04、WI-01 + WI-02)**: `document::saveFile()` / `isDirty()` / `markSaved()` (WI-01) に続き、`Ctrl+S`/`Ctrl+Shift+S`/`Ctrl+O`/`Ctrl+N`/D&D/未保存警告のUI配線 (WI-02) を実装済み。自動テスト (計1000件、Debug/Release/ubsan) は全green。**唯一残るのはドッグフーディング (NeoMIFES自身のソースをNeoMIFESで編集・保存・コミットする実地確認) — 実際にユーザーのリポジトリへ書き込む操作のため自動化せず、ユーザーへ実施を依頼中。** 完了後、本Issueをクローズする。詳細は [`build_plan.md`](../design/build_plan.md) WI-01/WI-02 節、[`detailed_design.md` §3.4](../design/detailed_design.md#34-filesaver-wi-01実装2026-08-04) 参照。

- **起票日:** 2026-08-04 (中間レビュー、Phase 8f / 7y 完了時点)
- **対象:** `src/document/` (`saveFile()` の新設)、`src/app/main.cpp` (`Ctrl+S` 配線)
- **優先度:** **最高 (P0)** — 本項目が未解消の限り、いかなる形態でもエンドユーザーへ配布してはならない
- **対応 Phase:** [Phase 8.5a / 8.5b](../design/master_roadmap.md) (roadmap v2.1 で新設)
- **親文書:** [`gap_analysis.md`](../design/gap_analysis.md) §3.1

## 事実

**NeoMIFES は編集したファイルを保存できない。**

実コードに対する機械的検証で確認済み (2026-08-04):

| 検証 | 結果 |
|---|---|
| `grep -rn "saveFile\|::WriteFile\|ofstream" src/ include/` | **0 件** |
| `grep -rn "CreateFileW" src/` | **1 件のみ** — `src/platform/src/file_mapping.cpp:13`、`GENERIC_READ` (mmap 用の読み取り専用オープン) |
| `Ctrl+S` のキーハンドラ | **存在しない** (`main.cpp` の Ctrl 系ハンドラは `Ctrl+F` / `Ctrl+Shift+P` / `Ctrl+Shift+F` / `Ctrl+G` の 4 つのみ) |
| コマンドパレット登録コマンド (`main.cpp:1356-1420`、10 件) | **ファイル操作系は 0 件** |

製品コード中でファイルを書き込むのは以下の 3 箇所のみで、いずれも文書とは無関係:
- `src/app/frame_profile.cpp:43` — 開発用フレーム計測 JSON
- `src/app/startup_profile.cpp:7` — 開発用起動計測 JSON
- `core::SearchHistory::saveTo()` — 検索履歴の永続化

## 影響

- **ユーザーが行った全ての編集が、プロセス終了時に無言で失われる**
- 要件定義書 §6 の以下が連鎖的に達成不能:
  - 文字コード変更 / 改行コード変更 / BOM 切替 — いずれも「変換して保存」が機能の本体。判定・読込 (Phase 6) は完了しているが出口が無い
  - 自動保存 / バックアップ (§6・§15 必須)
- `Document` に `isDirty()` が無いため未保存警告も不可能
- Grep 結果クリックやタグジャンプ (F12) は `openDocumentAt()` で `Document` を move-assign 破壊するため、**編集中の内容が警告なく消える** (データ損失に直結)

## なぜ 8 フェーズ見落とされたのか

推測ではなく構造的原因が特定できている ([`gap_analysis.md`](../design/gap_analysis.md) §6):

1. **roadmap のフェーズが全て「技術レイヤ名」で命名され** (Document Engine / Rendering / Editor Core / …)、CLAUDE.md §3 のレイヤ図と 1:1 対応していた。「アプリケーションシェル」はレイヤ図に存在せず、フェーズにもならなかった
2. **60 機能継承マトリクス (roadmap §1.5) に「ファイル保存」が一度も列挙されていなかった。** 三大エディタ全てが当然に備えるため「継承すべき差分」として認識されなかった
3. **各フェーズの完了判定が「そのフェーズの計画書」に閉じており**、プロダクト全体の要求に照らした検証が無かった

## 最大の技術的課題: mmap 中のファイルへの上書き

Phase 6d で `OriginalBuffer` は 10GB ファイルを `CreateFileW(GENERIC_READ)` + `MapViewOfFile` で読み取り専用マップしている。**自身がマップしているファイルへ直接書き戻すことはできない。**

roadmap §8.5.3 で採用方針を規定済み:

```
1. 同一ディレクトリに一時ファイルを作成
2. Piece Table をピース単位で走査し、指定エンコード/改行へ変換しつつ書き出す
   (全文を u16string へ実体化しない — 10GB 対応の生命線)
3. flush + close
4. OriginalBuffer のマップを解放 (元ファイルのハンドルを完全に手放す)
5. ReplaceFileW でアトミック置換 (ACL/タイムスタンプ保持)
6. 新ファイルを再 mmap し Piece Table を単一 Original ピースへ再構築
```

**未決事項:** U#22 (再構築後の Undo 履歴整合性)、U#23 (保存失敗時の一時ファイル処理) — roadmap §22 参照。いずれも Phase 8.5a 着手時に実機 probe で検証すること。

## 完了条件

- [x] `document::saveFile(doc, path, encoding, lineEnding, bom)` が実装され、10GB ファイルでも全文実体化なしに保存できる (WI-01、境界メモリはハイブリッドチャンク分割 `kLinesPerChunk`/`kMaxChunkCodeUnits` で保証、`document_save_bench.cpp`のpeak working set計測で確認)
- [x] `document::Document::isDirty()` / `markSaved()` が実装されている (WI-01)
- [x] `Ctrl+S` / `Ctrl+Shift+S` で保存でき、再度開くと編集内容が保持されている (WI-02、`performSave()`/`handleSaveKey()`)
- [x] 未保存のまま閉じようとすると警告が出る (WI-02、`confirmDiscardIfDirty()` を Ctrl+N/Ctrl+O/D&D/`WM_CLOSE` の全経路で共有)
- [x] 保存が他プロセスのロックで失敗した場合、元ファイルが破壊されない (WI-01、`replaceIntoPlace()`の実ファイル存在チェックで保証、統合テスト`FailedSaveLeavesTheOriginalFileUntouched`で実証)
- [x] `tests/integration/` に「開く → 編集→ 保存 → 再度開く → 内容一致」のラウンドトリップテストがある (WI-01、`document_save_roundtrip_test.cpp`)
- [ ] **ドッグフーディング: NeoMIFES 自身のソースを NeoMIFES で編集して保存し、そのままコミットできる** (WI-02実装完了・自動テスト全green。実際にユーザーのリポジトリへ書き込む操作のため自動化せず、ユーザーへ実施を依頼中 — 本項目のみ未達)

## 再検証コマンド

```bash
# 0 件であれば未解消
grep -rn "saveFile\|ReplaceFileW" --include=*.h --include=*.cpp src/
```
