---
name: ios-device-workflow
description: iOS 実機へのビルド・署名・インストール・ログ取得の手順。無料プロビジョニングで通る
metadata:
  type: project
---

実機: **iPhone 17(iPhone18,3)/ iOS 26.6.2 / UDID `00008150-001A49140E46401C`**。
Team: **`WLBBZ5VU2J`**(Tatsuyuki Sugahara / Personal Team / 無料 = **プロビジョニング 7 日間**)。

**前提(一度だけ)**
- `xcodebuild -downloadComponent MetalToolchain`(688MB)。**これが無いと `.metallib` を作れない**。
- デバイス側: **Developer Mode を有効化**(設定 → プライバシーとセキュリティ → 再起動)と
  **開発者証明書を信頼**(設定 → 一般 → VPN とデバイス管理)。どちらもユーザー操作が要る。
- Xcode に Apple ID を追加。Team ID は
  `defaults read com.apple.dt.Xcode | grep -A3 IDEProvisioningTeamByIdentifier` で読める。

```
source ~/.local/aq-mac-env.sh
cd DirectX
cmake --preset ios-xcode -D AQ_IOS_DEVELOPMENT_TEAM=WLBBZ5VU2J
cd build/ios-xcode
xcodebuild -project AquaDash.xcodeproj -scheme Game -configuration Debug \
  -destination 'platform=iOS,id=00008150-001A49140E46401C' \
  -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
cd ../.. && xcrun devicectl device install app --device 00008150-001A49140E46401C \
  build/ios-xcode/bin/Debug/Game.app
xcrun devicectl device process launch --device 00008150-001A49140E46401C --console com.aqengine.aquadash
```

**落とし穴**
- **`-allowProvisioningDeviceRegistration` が要る。** `-allowProvisioningUpdates` だけだと
  「Your team has no devices」で止まる(デバイスがチームに未登録)。
- アセットの同梱は `Game` の **POST_BUILD** に入れてある(署名の前に走らせるため)。
  署名済みバンドルを後から書き換えると起動時に弾かれる。
- **`devicectl` に画面キャプチャが無い。** 見た目の確認はユーザーに実機を見てもらう。
- `startup_timing.log` は
  `xcrun devicectl device copy from --device <id> --domain-type appDataContainer
   --domain-identifier com.aqengine.aquadash --user mobile --source Documents/startup_timing.log
   --destination <path>` で吸い出せる(`--user` であって `--username` ではない)。
- クラッシュの切り分けは [[ios-device-only-failures]] の「切り分けの道具」を見ること。

**How to apply:** 関連 [[ios-port-phase-status]] [[ios-simulator-workflow]]
