---
name: ios-simulator-touch-testing
description: iOS シミュレータでタッチ操作を検証する手順。simctl にタップ機能は無く、Simulator.app へホストのマウスを送る
metadata:
  type: project
---

**`xcrun simctl` にタップ/スワイプを送る機能は無い。** Simulator.app が
**ホストのマウスイベントをデバイスのタッチへ変換する**ので、そこへ CGEvent を投げる。

1. **Simulator.app を横向きにする**(アプリが横向き固定なので、これをしないと
   座標変換が回転ぶん複雑になる)。`Command + →`(System Events の keystroke)。
2. ウィンドウ位置とサイズを取る:
   `osascript -e 'tell application "System Events" to tell process "Simulator" to get {position, size} of window 1'`
3. `screencapture -x -o -l<winid>` でウィンドウを撮り、**デバイス画面の矩形を目視で拾って**
   アプリの論理解像度(iPhone 17 横向きなら 874x402)との対応を作る。
   撮影画像はホストポイントの 2 倍。
4. タップは短いドラッグ、スティックは**押しっぱなしのドラッグ**が要る。
   `evt` はドラッグ非対応なので専用ツールを書くこと
   (`kCGEventLeftMouseDown` → `kCGEventLeftMouseDragged` を一定間隔で投げ続ける → `Up`)。

**ベゼルに騙されないこと。** ウィンドウ撮影ではデバイスの丸角/ベゼルで端が欠けて見える。
**画面に収まっているかの判断は `xcrun simctl io <udid> screenshot`**(デバイス素の
フレームバッファ)で行う。

**4 本指以上のタッチは合成できない。** ホストのマウスは 1 本指相当、Option キーでも 2 本まで。
[[ios-port-phase-status]] の 4 本指ダブルタップはホスト側の単体テストで検証し、
実機確認は P5 へ回してある。

**単色のクリア画面では向きが判定できない**(デバイス画面は縦のまま)。
向きはログのウィンドウ寸法で見る。関連: [[ios-simulator-workflow]]
