# ADR-004: HTTP クライアントに WinHTTP を採用する

- **ステータス:** Accepted
- **決定日:** 2026-07-14

## コンテキスト

AI プラグインが Claude / ChatGPT / Gemini などの外部 API に HTTPS リクエストを送る必要がある (要件 §7)。本体コアは通信を行わないため、HTTP スタックは **AI プラグイン (別 DLL) の中でのみ使う**。

要件定義書 §3 で外部ライブラリ依存を最小化、CLAUDE.md 「絶対ルール §6」でも同旨。

## 選択肢

1. **WinHTTP (`winhttp.dll`)**
2. libcurl (静的リンク)
3. cpp-httplib (ヘッダオンリー)
4. Windows.Web.Http (WinRT)

## 決定

**WinHTTP を採用する。**

## 根拠

- **Windows 標準:** 追加ランタイム不要、`winhttp.dll` は OS 同梱
- **バイナリ膨張ゼロ:** ライブラリを配布する必要がない
- **HTTP/2 対応** (Windows 10 以降)
- **プロキシ自動検出** (WPAD、企業環境で重要)
- **TLS 1.3** サポート (Windows 11 以降、Win10 は TLS 1.2 まで)
- **ライセンス問題なし** (OS API)
- **AI プラグイン限定**なので、本体には一切影響しない

## 影響

- WinHTTP は完全非同期 API だが、コールバック地獄になりやすいため薄い `Promise` 風ラッパを AI プラグイン内に自作
- HTTPS 証明書検証はデフォルト有効。企業内自己署名証明書対応は設定でストア追加を許可
- ストリーミング応答 (SSE) は WinHTTP の `WinHttpReadData` ループで実装

## 却下理由

- **libcurl:** 機能豊富だが、ライセンス (MIT ライク) 問題は無いものの、静的リンクで数 MB のバイナリ膨張と `openssl` 依存が発生。要件との適合度で劣る
- **cpp-httplib:** 手軽だが HTTP/2 非対応、企業プロキシ対応が弱い
- **Windows.Web.Http (WinRT):** COM/WinRT 初期化が重く、起動 0.3s に影響しかねない
