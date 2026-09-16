---
name: ios-port-phase-status
description: iOS 移植の到達点。P0(ビルド基盤)完了。Android の着地で新規実装が大幅に減った
metadata:
  type: project
---

一次資料は `DirectX/設計書/iOS移植設計.md`(2026-09-14 に第 2 版へ更新)。

- **P0 〜 P5b 完了(2026-09-14、`db263b0` 〜 `f4097b6`)。実機で通しプレイできる。**
  **実機: iPhone 17(iPhone18,3)/ iOS 26.6.2 / Apple A19 GPU**。無料プロビジョニング(7 日間)
- 実機では**シミュレータの劣化経路 3 つがすべて本来の経路を通る**
  (BC yes / ボーダーカラー yes / read-write Tier2 → compute on)。
  compute が有効になってポストプロセスが初めて走ったが見た目に問題なし
- プリセットは `ios-simulator-xcode` / `ios-xcode` / `ios-ninja`。
  **Metal 固定・`CMAKE_OSX_DEPLOYMENT_TARGET=16.4`**(BC の API が入ったバージョン)
- 検証は**シミュレータ先行、実機は P5**(iOS 実機は手元に無い)
- 次は **P6(性能とパッケージング)**。残タスクは**実機の物理コントローラ**(手元に無い)
- **★ 実機でしか出ない不具合が 3 つあった。[[ios-device-only-failures]] を必ず読むこと。**
  シミュレータで P0〜P5a を全部通していたのに、実機では 1 フレームも出なかった
- **サスペンドは順序が肝。**`CADisplayLink` を止める前に**フレームを 1 回だけ回す**。
  そうしないと `Engine::SyncSoundActivity()` が走らず背面で BGM が鳴り続ける。
  この 1 回は `IsRenderable()` が false なので GPU を触らない
- **音は耳で確認できないので、レンダーコールバックの L/R ピークを一時ログで測る**。
  Mac と突き合わせると定位まで比較できる(P4 では 6 点中 5 点が完全一致した)
- **ImGui の倍率は iOS だけ 1.25**(Android は 2.0)。iOS は論理ポイント(874x402)が
  ImGui の座標になるため。解像度スケールを上げる P6 では見直すこと
- **デバッグ UI の表示切替は 4 本指ダブルタップ**(`HID/TouchGesture.h`)。
  スマホには F1 も中クリックも無いため。Android にも効く
- **アセットはバンドルの `Content/` 配下**(`<Bundle>/Content/Game/Assets/...`)。
  バンドル直下にはできない — 実行ファイル自身が `<Bundle>/Game` で名前が衝突する
- **シミュレータでだけ通っている劣化経路が 3 つある**([[ios-simulator-device-gaps]])。
  実機確認は P5
- `IPlatform::RunFrameLoop` は導入済。既定実装が従来の while ループなので他は挙動不変。
  `Engine::RunGame` の本体は `Engine::FrameStep()` へ切り出し済み

**Android 移植が先に着地したことで iOS の作業が激減した。**
`ITouchBackend` / `VirtualPadBackend` / `CompositePadBackend` / `TouchMouseBackend` /
`ImGuiPointerInput` / `IPlatform::GetUserDataDirectory` / `IsRenderable` /
`IGraphicsDeviceImpl::RecreateSurface` はすべて**プラットフォーム非依存として導入済**。
iOS の新規は `iOSTouchBackend` 1 本と `PlatformiOS` 一式だけ。
特に **720 行を見込んでいた `iOSImGui` は不要**(`ImGuiPointerInput` を使う)。

**iOS だけが自分でやる必要があるもの:**
- **`IPlatform::RunFrameLoop`** — Android は `android_main` が自前スレッドを持つので
  `while (PumpEvents())` が成立した。iOS だけ run loop を `UIApplicationMain` に取られる。
  `Engine::RunGame` のループ本体(SyncSoundActivity / EnsureSurfaceUpToDate /
  IsRenderable / SyncScreenSize / Update)を `FrameStep()` へ切り出して委譲する
- **Metal の `FindProjectRoot`** — [[findprojectroot-contentroot-gap]]
- **Metal の `GetSurfaceSize`** — Vulkan だけが override 済み。Mac にも効くので回帰注意

**How to apply:** 続きは設計書の「現在の到達点」→ §0.6 → 該当フェーズの評価欄の順に読む。
Mac のビルドは [[mac-build-environment]]、見た目確認は [[mac-visual-verification]]。
