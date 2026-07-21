# ADR-013: JSON 入出力ライブラリに nlohmann/json を採用する

- **ステータス:** Accepted
- **決定日:** 2026-07-21

## コンテキスト

Phase 5c5 (検索履歴永続化) で、Find bar / Grep ダイアログが共有する検索クエリ履歴 (直近 50 件以内の短い文字列配列) を `%APPDATA%\NeoMIFES\search_history.json` へ保存・読込する必要がある。本コードベースには JSON を含むあらゆる構造化テキストフォーマットの読み書き機能が一切存在しない。CLAUDE.md 「絶対ルール §6」で外部ライブラリ追加は最小限とされているため、本 ADR で採用理由を残す。

master_roadmap.md §5.5 のスケッチは `search_history.json5` (JSON5) を挙げていたが、JSON5 の追加機能 (コメント・末尾カンマ・無引用キー) は機械生成・機械読取専用のこのファイルには意味を持たず、より実績豊富で選択肢の多いプレーン JSON へ意図的に乖離した (Phase 5c5 完了記録参照)。

## 選択肢

1. **nlohmann/json** (ヘッダオンリー、MIT)
2. RapidJSON (ヘッダオンリー、MIT、SAX/DOM 両対応)
3. 自作パーサ (数十行程度の最小限 JSON サブセット)

## 決定

**nlohmann/json を単独採用する。**

## 根拠

- **永続化するデータの性質:** 「50 件以内の短い文字列配列」のみで、パース速度・メモリ効率は一切要件にならない。したがって選定基準は実装のしやすさ・実績・保守性に絞ってよい
- **API の使いやすさ:** `json::parse()`/暗黙変換によるシリアライズ/例外ベースのエラー処理が、本フェーズが必要とする「配列を読んで書くだけ」の用途に対して RapidJSON の SAX/DOM API より大幅に簡潔
- **実績・信頼性:** 業界で広く使われ長期間メンテナンスされている、C++11 以降の標準的な選択肢
- **既存 FetchContent パターンとの整合性:** RE2/Abseil (ADR-002、Phase 5a) と同じ `FetchContent_Declare` + `EXCLUDE_FROM_ALL` パターンで導入でき、ビルドインフラの新規学習コストがない
- **ライセンス問題なし** (MIT)

## 影響

- `cmake/Dependencies.cmake` へ nlohmann/json (タグ `v3.11.3`) を FetchContent 追加。`nlohmann_json::nlohmann_json` という INTERFACE ターゲットを提供する
- `core::SearchHistory`(Phase 5c5、`src/core/search_history.cpp`)のみが PRIVATE にリンクする。パブリックヘッダには `nlohmann::json` 型を一切露出しない — 将来別の永続化機能が JSON を必要とする場合も、同じ「実装詳細として隠蔽する」設計を踏襲する
- 内部文字列は `std::u16string` (UTF-16、CLAUDE.md §4) だが nlohmann/json は `std::string` (UTF-8) を扱う設計のため、境界で `neomifes::encoding::encode()`/`decode()` (Phase 6a〜6d) を使って変換する。新規の UTF-8 変換実装は追加しない

## 却下理由

- **RapidJSON:** パース速度に優れるが SAX/DOM いずれの API も「50 件の文字列配列を読み書きする」という本フェーズの用途に対して過剰に低レベル。速度上の利点は本フェーズのデータ規模では意味を持たない
- **自作パーサ:** CLAUDE.md ルール 3 (推測実装をしない) に照らし、JSON のエスケープシーケンス処理・Unicode エスケープ (`\uXXXX`)・数値パースの境界条件などを手書きで正しく実装するリスクが、確立されたライブラリを1つ追加するコストに見合わない

## 将来の再評価

- 設定ファイル (テーマ・キーバインドカスタマイズ等、要件定義書 §13 で将来言及) が必要になった場合も、同じ nlohmann/json をそのまま再利用できる見込み。新たな JSON 系ライブラリを追加する必要は今のところ無い
