# Issue: Phase 10.1 v2.0拡張候補 (リアルタイムテール/分散トレース/構造化ログ/統計ダッシュボード/ベンダー固有パターン) (P2 — 待機)

- **起票日:** 2026-08-16 (WI-14a、ログ解析モード ヘッドレス基盤)
- **対象:** `master_roadmap.md` §10.1 のv2.0拡張スケッチ全体
- **優先度:** P2 (MVPの中核機能ではなく、要件定義書§8自体にも明記が無い拡張)
- **対応 Phase:** 未定 (WI-14c でMVP達成後、実データが手元に用意できた時点で再評価)
- **親文書:** WI-14a (`build_plan.md` §5)

## 事実

`master_roadmap.md` §10.1 は「v2.0大幅拡張」として、リアルタイムテール・分散トレースID対応・Structured Log (JSON/Logfmt) 対応・正規表現テンプレート・統計ダッシュボード、および SAP/AWS CloudTrail/Azure Monitor/Oracle alert.log/SAP HANA/Tomcat catalina.out/Docker/Kubernetes/OpenTelemetry/AWS X-Ray/Grafana Loki/Fluentd といったベンダー固有ログパターン (roadmap スケッチでは組込パターン16種) を挙げている。

一方、要件定義書§8自体は「対象: SAP/AWS/Azure/Linux/Windows/Apache/Nginx/Oracle/HANA/Tomcat/Java/Docker/Kubernetes。検索速度最優先、時系列ジャンプ、ERROR/WARNING抽出、色分け、フィルタ、タイムスタンプ解析」という比較的シンプルな核心機能のみを求めている。

WI-14a では、CLAUDE.md ルール3 (推測実装をしない) に従い、公開・検証可能な標準4種 (RFC 5424/3164 syslog、Apache/Nginx Common+Combined Log Format、汎用ISO-8601+レベル行) のみを組込パターンとして実装し、上記のv2.0拡張・ベンダー固有パターンは全て意図的に先送りした。ベンダー固有フォーマットの正確な仕様 (SAP/AWS CloudTrail/Azure Monitor等の実際のログ行フォーマット) は実データを見ないまま書くと誤ったパターンを組み込むリスクがあり、記憶からの推測実装に該当する。

## 影響

- WI-14a〜dで達成するのはMVP (要件定義書§8の核心機能) までであり、roadmap §10.1のスケッチが描く「分散システム時代のSAP/AWS/Azureログを1つのビューで統合探索可能」というビジョンには届かない。
- ユーザーがSAP/AWS/Azure等のログファイルを開いても、汎用ISO-8601ルールにマッチしなければ無彩色のまま表示される (クラッシュはしないが、期待するハイライト/レベル抽出は得られない)。

## 対応方針 (未着手)

1. 実際のログサンプル (SAP/AWS CloudTrail/Azure Monitor等) を入手し、実データからパターンを起こす。
2. リアルタイムテール (`IO Watcher`による末尾追加検知) は新規の非同期監視基盤が要る — WI-14b (`LogIndexWorker`) 完了後に再評価。
3. 分散トレースID対応・統計ダッシュボードはUI実装 (WI-14c) 完了後、実際のユーザー需要を確認してから着手判断する。
4. Structured Log (JSON/Logfmt) は正規表現ベースの `LogPatternRule` とは異なる解析方式 (JSON/Logfmtパーサ) が必要になるため、別スコープとして扱う。

## 完了条件

- [ ] 実データに基づくベンダー固有パターンの追加方針を決定した
- [ ] リアルタイムテール/分散トレース/統計ダッシュボード/Structured Logのいずれかに着手する場合、本issueをそのサブWIの完了記録から解決済みへ移動した

## 再検証コマンド

```bash
grep -n "builtInLogPatterns" src/logmode/src/log_pattern.cpp
```
