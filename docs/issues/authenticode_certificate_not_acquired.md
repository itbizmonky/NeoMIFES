# Issue: 本物の Authenticode 証明書が未取得 (P1 — 出荷判断はユーザーに委ねる)

- **起票日:** 2026-08-16 (WI-13、MVP出荷判定)
- **対象:** `tools/create_dev_certificate.ps1` / `tools/sign_release_binary.ps1`
- **優先度:** P1 (実際のエンドユーザー配布には必須、実装機構自体は完成済み)
- **対応 Phase:** 未定 (実際の証明書購入・組織の身元確認が必要、Claude Codeが代行不可能)
- **親文書:** WI-13 (`build_plan.md` §5・§6) 着手前のAskUserQuestionでユーザーへ確認済み

## 事実

WI-13のDoD「Authenticode署名 + Portable Zip配布」のうち、署名の**仕組み自体**(証明書生成→`signtool sign`→`signtool verify`)は自己署名証明書(`CN=NeoMIFES Development Self-Signed - NOT FOR PRODUCTION`)で実装・実機動作確認済み(タイムスタンプ付与も含め正しく機能する)。しかし自己署名証明書はWindows SmartScreen等いかなるマシンからも信頼されるチェーンを持たず、`signtool verify /pa`は意図通り「信頼されないルート証明書で終端した」エラーを返す。

本物のAuthenticode証明書(コードサイニング証明書)の取得には、認証局(DigiCert/Sectigo等)からの購入と組織の身元確認(法人登記情報の提出等)が必要であり、Claude Codeが代行できない([CLAUDE.mdの禁止事項](../../CLAUDE.md)、およびシステムプロンプトが定める「実際の金銭取引」の禁止に該当)。

## 影響

- 自己署名バイナリをそのまま配布すると、エンドユーザーの環境でWindows SmartScreenの警告(「発行元不明のアプリ」)が表示される。実用配布には不適。
- Portable Zip自体(`tools/package_portable.ps1`)は完成しており、本物の証明書さえ用意できれば`tools/sign_release_binary.ps1`の証明書検索ロジック(`Subject`文字列一致)を実際の証明書のSubjectへ合わせて差し替えるだけで即座に対応可能。

## 対応方針 (ユーザー判断待ち)

1. ユーザーが実際のコードサイニング証明書(組織向けEV証明書推奨、SmartScreenのレピュテーション構築が早い)を購入・取得する
2. 証明書を`.pfx`として入手し、`tools/sign_release_binary.ps1`の`$subjectName`をその証明書のSubjectへ変更(または`-Thumbprint`引数を追加して直接指定できるよう拡張)
3. 出荷判断(自己署名のまま限定配布するか、本物の証明書を待つか)はユーザーに委ねる — 本Issueはその判断の記録として残す

## 完了条件

- [ ] 本物のAuthenticode証明書を取得した
- [ ] `tools/sign_release_binary.ps1`が本物の証明書で署名し、`signtool verify /pa`がエラー無く通ることを確認した
- [ ] Portable Zip配布物が実際のWindows環境でSmartScreen警告なく実行できることを確認した

## 再検証コマンド

```powershell
Get-ChildItem -Path Cert:\CurrentUser\My -CodeSigningCert
```
