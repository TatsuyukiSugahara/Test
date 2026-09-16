---
name: ios-simulator-workflow
description: iOS シミュレータでのビルド・起動・ログ・撮影の手順と、P1 で踏んだ落とし穴
metadata:
  type: project
---

```
source ~/.local/aq-mac-env.sh
cd DirectX
cmake --preset ios-simulator-xcode
cmake --build --preset ios-simulator-xcode-debug

UDID=2D420C32-6FFA-45B0-B29B-C37388DF677E      # iPhone 17 / iOS 26.5
xcrun simctl boot $UDID
xcrun simctl install $UDID build/ios-simulator-xcode/bin/Debug/Game.app
xcrun simctl launch --console-pty $UDID com.aqengine.aquadash   # ログはここに出る
xcrun simctl io $UDID screenshot out.png
xcrun simctl terminate $UDID com.aqengine.aquadash
```
実機は `ios-xcode` プリセット + `cmake --build ... -- CODE_SIGNING_ALLOWED=NO` で
コンパイル確認だけできる(署名と実機投入は P5)。

**落とし穴:**

- **`StartupMark` は iOS では stderr へ出る**(`aq.cpp` の `AQ_PLATFORM_IOS` 分岐)。
  CWD が `/` で `startup_timing.log` が作れないため。`--console-pty` を付けないと見えない。
- **単色のクリア画面では向きが判定できない。** `simctl io screenshot` はデバイス画面
  (縦 1206x2622)を撮るので、アプリが横向きでも縦の絵が返る。
  **向きはログのウィンドウ寸法で判断する**(横向きなら 874x402)。
- **`applicationWillTerminate:` は `simctl terminate` では呼ばれない。**
  iOS は「サスペンド → 予告なく kill」が通常なので、終了時の後始末とリーク報告が
  走らない前提で設計すること。**リーク検出を iOS の品質ゲートに使わない。**
- **Info.plist テンプレートのコメントに `${...}` の形を書かない。** `configure_file` が
  展開しようとして configure ごと落ちる。
- **Xcode は `TARGETED_DEVICE_FAMILY` から `UIDeviceFamily` を上書きする。**
  テンプレートに書いても効かないので `XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY` を使う。

**How to apply:** 関連 [[ios-port-phase-status]]。Mac 側の手順は [[mac-visual-verification]]。
