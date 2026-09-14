# iOS 移植 設計

> 対象コミット: 2f1af88 / 最終更新: 2026-09-14

## 現在の到達点

**設計承認済み(2026-09-14)。P0 に着手する。**

**2026-09-13 に Android 移植(P0〜P7)が着地したことで、本書が「iOS で新規に要る」と
していたものの過半が既にリポジトリへ入った**。差分の棚卸しは §0.6。
本書はその再評価を反映した第 2 版である。

**P2(シミュレータで実シーン)完了 — 2026-09-14。**
iPhone 17 シミュレータ(iOS 26.5)で **AquaDash のタイトル画面とステージが 60 FPS で描画**され、
BC 圧縮テクスチャの実行時展開まで通った。残るは入力(P3)。

| | 状態 |
|---|---|
| 設計 | 第 2 版。§0.5 の 6 論点すべて決着(2026-09-14) |
| 実装 | **P0 / P1 / P2 完了。次は P3(入力)** |
| 環境 | Xcode 26.6 / iOS SDK 26.5 / iOS Simulator SDK 26.5。**iOS 実機は接続なし**(シミュレータ 11 種) |
| 検証方針 | シミュレータ先行。実機は P5 |
| シミュレータ | `ios-simulator-xcode` が configure / build 成功。`Game.app/Game` = Mach-O arm64 / `platform IOSSIMULATOR` / `minos 16.4` / `sdk 26.5` |
| 実機 | `ios-xcode` が configure / build 成功(`CODE_SIGNING_ALLOWED=NO`)。`platform IOS` / `minos 16.4` |
| Ninja | `ios-ninja` も configure 成功(コンパイルエラーの潰し込み用) |
| バンドル | **`Contents/` の無いフラット構造**を確認(`Game.app/Game`)。§7.3 の前提どおり |
| 起動(P1) | `iOSMain` → ウィンドウ **874x402(横向き)** 48ms → Metal デバイス 49ms → **クリア提示 82ms** |
| 画面(P2) | **タイトル画面が正しく描画**(ロゴ / 背景 / ステージサムネ / フォント / ImGui)。**ステージも描画**(路面・キャラ・地形・草・空・HUD・ミニマップ) |
| 性能(P2) | タイトル・ステージとも **60.0 FPS(16.67 ms)** — VSync 上限に張り付き |
| BC 展開 | **実測で動作。** `utc_all2 / utc_nomal / utc_spec` の 3 枚(DXT5 / DX10)を計 34 image へ展開(11+11+12)。所要 67 / 28 / 187 ms |
| メモリ | **RSS 322 MB**(Debug / BC を RGBA8 へ展開した状態) |
| 書き込み先 | `startup_timing.log` と `imgui.ini` がアプリコンテナの `Documents/` に出る |
| Validation | **Metal API Validation を有効(`SIMCTL_CHILD_METAL_DEVICE_WRAPPER_TYPE=1`)にしてエラー 0** |
| フレーム駆動 | `CADisplayLink` で **16.2〜16.5 ms 間隔 = 60 FPS**。`UIApplicationMain` が戻らない構造で `RunFrameLoop` の委譲が効いている |
| シミュレータ実測 | `Apple iOS simulator GPU` / **BC 圧縮: no** / GPU family `Apple2` / unified memory: no(§0.2-3 の事前実測と一致) |
| Mac 実測(対照) | `Apple M5` / **BC 圧縮: yes** / GPU family `Apple9` / unified memory: yes |
| Mac 回帰 | **確認済。回帰なし。** Metal / Vulkan とも Release がビルドでき、Metal 構成を実行してタイトル〜ステージ走行(59.7 FPS / 入力 / HUD / ミニマップ / ブーストパッド)まで確認。**警告は 14 件で P0 前と同一箇所** |
| Windows 回帰 | **未確認**(この環境では Windows をビルドできない)。共通コードへの変更は §P0 の評価欄を参照 |

対象: `aqEngine/` + `Game/`。既存の **Metal バックエンド**を iOS で動かし、実機(または
シミュレータ)で AquaDash が起動〜プレイできる状態までを設計する。

姉妹文書:
- [Mac移植設計.md](Mac移植設計.md) / [Mac移植調査.md](Mac移植調査.md) — 非 Windows 化の土台。
  CMake・`AQ_PLATFORM_*`・`IPlatform`・入力/サウンド抽象・事前シェーダ生成は**すべてここで導入済**。
- [MetalBackend設計.md](MetalBackend設計.md) — Metal バックエンドの一次資料。本書はその iOS 差分だけを書く。
- [Android移植設計.md](Android移植設計.md) — **タッチ抽象(§5.2)とライフサイクル IF(§3.3)の一次資料**。
  同じ設計を二重に書かないため、本書はリンクで参照して iOS 実装の差分のみ記述する。
- [Sound設計.md](Sound設計.md) / [02_HID設計.md](02_HID設計.md) — サウンド・入力抽象の一次資料。

**本書のステータス: 第 2 版・ユーザー承認済み(2026-09-14)。実装は P0 から。**
§0.5 の 6 論点はすべて決着した。

---

## 0. 方針

### 0.1 結論(先に)

- **グラフィックスは新規実装ゼロで済む。** Metal バックエンドの 12 TU は **iOS SDK でそのまま
  コンパイルが通り、iOS 非対応の Metal API を 1 つも使っていない**ことを実測で確認した(§0.2)。
  深度形式・StorageMode・threadgroup サイズ・`CAMetalLayer` のプロパティという
  「iOS と非互換になりがちな 4 点」はすべて既に iOS 互換の形で書かれている。
- **作業の実体はプラットフォーム層・ビルド/署名・タッチ入力・アセット配置**。Mac 移植と同じ構図。
- Mac にも Android にも無かった **iOS 固有の新規要素が 3 つ**ある:
  1. **メインループの所有権が UIKit 側にある**([Engine::RunGame](../aqEngine/Engine.cpp) の
     `while (PumpEvents())` が構造的に成立しない) — §3.3
  2. **BC 圧縮テクスチャが使えない環境がある**(シミュレータは実測で `NO`) — §4.3
  3. **アプリバンドルが read-only で、カレントディレクトリに依存できない**
     (Mac は `chdir` で凌いでいる) — §7。
     ただし **Android 移植が読み込み経路を `GetContentRoot()` へ寄せたので、
     iOS は `chdir` せずに済むようになった**(§0.6-(2))
- ~~タッチ入力とライフサイクルは Android 移植と同じ課題~~ →
  **Android が先に着地し、抽象(`ITouchBackend` / `IsRenderable` / 仮想パッド /
  ポインタ合成 / ImGui シム)はすべて共通実装として入った。iOS はそれに配線するだけ**(§0.6)。

### 0.2 事前実測(この設計の土台)

設計に先立って、**現在のコードを iOS SDK(iPhoneOS 26.5 / arm64)に対して実際にコンパイル**し、
**iOS シミュレータ(iPhone 17 / iOS 26.5)で Metal の機能を実測**した。推測ではなく測定値。

**(1) configure は通ってしまう。コンパイルは 4 本だけ落ちる。**

```
cmake -S DirectX -B <dir> -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DAQ_GRAPHICS_API=Metal
  → Configuring done / Generating done    ← 通る(iOS を Mac と誤認するため。§2.1)
```

`aqEngine` + `Game` の **195 TU を iOS SDK で構文チェックし、失敗は 4 本だけ**だった。

| 失敗した TU | エラー |
|---|---|
| [Platform/Mac/PlatformMac.mm](../aqEngine/Platform/Mac/PlatformMac.mm):10 | `'Cocoa/Cocoa.h' file not found` |
| [Platform/Mac/MacImGui.mm](../aqEngine/Platform/Mac/MacImGui.mm) | 同 |
| [Game/Application/MacMain.mm](../Game/Application/MacMain.mm) | 同 |
| [Sound/CoreAudio/CoreAudioSoundBackend.mm](../aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.mm):9 | `'CoreAudio/CoreAudio.h' file not found` |

**残り 191 本は無改修で iOS SDK を通る。** 内訳として重要なもの:

| 通った TU 群 | 意味 |
|---|---|
| `Graphics/Metal/` の **12 本すべて** | Metal バックエンドは iOS 非対応 API を使っていない(下記の理由により、これは「通った」以上の意味を持つ) |
| `HID/Mac/GameControllerPadBackend.mm` | GameController.framework は iOS でも同一 API |
| `Sound/Decoder/ExtAudioFileDecoder.mm` / `Sound/Mixer/MixerSoundVoice.cpp` | AudioToolbox 経路はそのまま使える |

> **なぜ「コンパイルが通った」で API 非互換が無いと言えるのか**: clang は
> `API_UNAVAILABLE(ios)` が付いた識別子の使用を**警告ではなくエラー**にする。実際に
> `MTLStorageModeManaged` と `MTLPixelFormatDepth24Unorm_Stencil8` を使う試験 TU を書いて
> `error: 'MTLStorageModeManaged' is unavailable: not available on iOS` になることを確認した。
> つまり 12 本がエラー 0 で通ったことは、これらの macOS 専用 API を**使っていない**ことの証明になる。

**(2) 必要なフレームワークは `Cocoa` 以外すべて iOS SDK に存在する。**

[aqEngine/CMakeLists.txt:182-192](../aqEngine/CMakeLists.txt) がリンクする 7 つのうち:

| フレームワーク | iOS SDK | iOS Simulator SDK |
|---|---|---|
| `Cocoa` / `AppKit` | **無し** | **無し** |
| `QuartzCore` / `Metal` / `GameController` / `AudioToolbox` / `CoreAudio` / `AVFoundation` | あり | あり |
| `UIKit` | あり | あり |

**(3) シミュレータの Metal 実測値(iPhone 17 / iOS 26.5)**

| 項目 | 実測値 | 影響 |
|---|---|---|
| `device.name` | `Apple iOS simulator GPU` | — |
| **`supportsBCTextureCompression`** | **`NO`** | **BC 圧縮 DDS(7 枚)がシミュレータでは読めない。§4.3** |
| `maxThreadsPerThreadgroup.width` | `512` | 本エンジンの compute は最大 **64** スレッド/グループなので余裕(§4.4) |
| `supportsFamily` | `Apple1` / `Apple2` のみ | シミュレータの GPU family は低く報告される |
| `argumentBuffersSupport` | `0`(Tier1) | Argument buffer は未使用なので影響なし |
| `hasUnifiedMemory` | `NO` | Metal バックエンドは `StorageModeShared` 一本(統合メモリ前提)。動作はするが、シミュレータでは性能判定に使えない |

**(4) 手元の環境**

| 項目 | 状態 |
|---|---|
| Xcode | 26.6 / iOS SDK 26.5 / iOS Simulator SDK 26.5 |
| **iOS 実機** | **接続なし**(シミュレータ 11 種のみ) → §0.5-2 |
| MoltenVK | 導入済 Vulkan SDK 1.4.357.1 に **`ios-arm64` と `ios-arm64_x86_64-simulator` スライスあり** |
| アセット総量 | **98 MB**(Sound 32MB / Terrain 24MB / Font 17MB / …) |

### 0.3 グラフィックス: 道は 1 本(ネイティブ Metal)

Mac 移植のような「道A(Vulkan + MoltenVK)/ 道B(ネイティブ Metal)」の分岐は**採らない**。

| | 判断 |
|---|---|
| **ネイティブ Metal【採用】** | Metal バックエンドが既に実装済・macOS 実機で Vulkan 構成と画素一致(平均差 1.8/255)まで確認済で、**iOS SDK でそのままコンパイルが通ることを実測した**(§0.2)。追加コストは MSL の再生成(§4.2)のみ |
| Vulkan + MoltenVK【不採用】 | MoltenVK に `ios-arm64` スライスは**ある**が、(a) `.ipa` へ埋め込みフレームワーク + ICD 定義 JSON を同梱し署名する手間が増え、(b) 結局 Metal の上に乗るだけで、(c) Metal 経路が既に動いているので得るものが無い |

したがってルート [CMakeLists.txt:61-63](../CMakeLists.txt) の
`AQ_GRAPHICS_API=Metal は macOS 専用です` を iOS でも許すよう緩め、**逆に iOS では
Metal 以外(D3D11/D3D12/Vulkan)を禁止**する(現状は非 Windows で Vulkan / Metal の
2 択になっている)。

### 0.4 ビルド経路: CMake + Xcode ジェネレータ

Android と違い**外部ビルドシステム(Gradle 相当)は不要**。CMake が `.app` バンドルを直接作れる。

- **Xcode ジェネレータを主経路**にする。実機デプロイ・コード署名・プロビジョニングは
  Xcode のビルド設定(`XCODE_ATTRIBUTE_*`)経由でしか扱えないため。
- Ninja でも configure/コンパイルは通る(§0.2 の実測は Ninja 構成で行った)ので、
  **CI やコンパイルエラーの潰し込みには Ninja、実行と署名は Xcode** と役割を分ける。
- **シミュレータは署名不要**。実機は署名が必要(§0.5-2)。

### 0.5 決定事項(2026-09-14 にユーザー判断済み)

| # | 論点 | 選択肢 | 影響 |
|---|---|---|---|
| 1 | **BC 圧縮テクスチャ**(§4.3) | (a) **実行時に BC を展開**して RGBA8 で使う【推奨】 / (b) アセットを ASTC へ再エンコード / (c) BC 対応端末(`supportsBCTextureCompression == YES`)に限定 | (a) は同梱済 DirectXTex の BC ソフトコーデックで完結しアセット無改変。VRAM は増える。(c) はシミュレータで動かせなくなる |
| 2 | **検証環境**(実機 / アカウント) | (a) **シミュレータで P1〜P4 まで進め、実機は P5**【推奨】 / (b) 最初から実機 | 手元に iOS 実機は無い(§0.2-4)。実機には Apple ID(無料なら 7 日間のプロビジョニング)以上が必要。性能・サーマル・BC 対応の判定は**実機でしかできない** |
| 3 | **タッチ操作の方針** | (a) **オンスクリーン仮想パッド**(`IPadBackend` として供給)【推奨】 / (b) 物理コントローラ必須 / (c) タッチ専用操作を新規設計 | [Android移植設計.md §5.2](Android移植設計.md) と**同一の論点**。ここで決めた方式は両移植で共有する |
| 4 | **解像度スケール** | (a) **論理解像度(`contentsScale = 1.0`)で開始し P6 で調整**【推奨】 / (b) 最初からネイティブ解像度 | (b) は 3× で G-Buffer とシャドウが 9 倍になり、§4.4 の見積り 130〜140MB が破綻する |
| 5 | **画面の向き** | (a) **横向き固定**【推奨】 / (b) 回転対応 | (b) は drawableSize 変化への追従が必要だが、**Metal バックエンドにリサイズ経路が存在しない**(§4.5)。(a) なら本移植の範囲外にできる |
| 6 | **デバッグ UI(ImGui)** | (a) **P3 で最小限のタッチ対応**【推奨】 / (b) iOS では無効化(`AQ_IMGUI` を切る) | [MacImGui.mm](../aqEngine/Platform/Mac/MacImGui.mm) は 720 行で、`NSCursor` / `NSPasteboard` / Carbon キー表が置き換え対象。指での ImGui 操作は本質的に厳しい |

**すべて決着した(2026-09-14)。** #3 と #6 は Android 移植の着地により自動的に決まり(§0.6)、
残る 4 点はユーザー判断を得た。

| # | 論点 | 決定 |
|---|---|---|
| 1 | BC 圧縮テクスチャ | **(a) 実行時に BC を展開**。非対応環境では `DirectX::Decompress` で RGBA8 へ。アセット無改変(§4.3) |
| 2 | 検証環境 | **(a) シミュレータ先行**。P1〜P4 はシミュレータ、**実機は P5**(§9) |
| 3 | タッチ操作 | **(a) オンスクリーン仮想パッド**。Android と共有の `VirtualPadBackend`(§0.6 / §5.2) |
| 4 | 解像度スケール | **(a) `contentsScale = 1.0`(論理解像度)で開始し P6 で調整**。#5 で向きを固定するので当面変化しない(§3.5)。**これのみユーザーに個別確認せず既定を採用した** |
| 5 | 画面の向き | **(a) 横向き固定**。`drawableSize` を変えないことで、Metal にリサイズ経路が無い問題を本移植の範囲外に置く(§4.5) |
| 6 | デバッグ UI(ImGui) | **(a) 有効**。ただし新規実装ではなく共通の `ImGuiPointerInput` を再利用する(§5.3) |


### 0.6 Android 移植の着地による再評価(2026-09-14)★第 2 版の主題

初版(2026-09-12 / 対象コミット `17bdb6d`)の時点では、タッチ抽象もライフサイクル IF も
「Android と iOS のどちらが先に入れるか」が未定だった(初版 §8-7)。
**2026-09-13 に Android が P0〜P7 まで着地し、先に入れた側になった。**
その結果、本書が「iOS で新規に要る」としていたものの過半が**既にリポジトリに存在し、
しかも意図的にプラットフォーム非依存として書かれている**。

#### (1) もう書かなくてよくなったもの

| 初版で「iOS が新規に要る」としたもの | 現在の実体 | 備考 |
|---|---|---|
| **タッチ抽象**(初版 §5.2) | [HID/ITouchBackend.h](../aqEngine/HID/ITouchBackend.h) — `TouchPoint` / `TouchState`(最大 10 点) / `ITouchBackend::Poll` | ヘッダのコメントに **「将来 iOS も ITouchBackend の実装を足すだけで済む形にしてある」**と明記されている。iOS 側の新規は `iOSTouchBackend` 1 本だけ |
| **仮想パッド**(初版 §0.5-3 案 a) | [HID/VirtualPadBackend.{h,cpp}](../aqEngine/HID/VirtualPadBackend.h) 315 行 | クラスコメントが**「プラットフォーム非依存(Android / iOS で共有する)」**。当たり判定は正規化座標なので画面比が変わっても効く。`IsTouchConsumed` で指の取り合いも調停済み |
| 物理パッドと仮想パッドの合成 | [HID/CompositePadBackend.{h,cpp}](../aqEngine/HID/CompositePadBackend.h) | ボタン OR / 軸は絶対値の大きい方。「この型自体はプラットフォーム非依存」 |
| タッチを UI ポインタへ載せる経路 | [HID/TouchMouseBackend.{h,cpp}](../aqEngine/HID/TouchMouseBackend.h) | 同じく「プラットフォーム非依存(Android / iOS で共有する)」 |
| **`iOSImGui`(初版 §5.3 で 720 行の書き直しを見込んでいた)** | [Platform/Common/ImGuiPointerInput.{h,cpp}](../aqEngine/Platform/Common/ImGuiPointerInput.h) | **プラットフォームガードすら付いていない**(「マウス抽象しか見ないので、タッチでも物理マウスでも同じ経路で動く」)。`DisplaySize` / `DeltaTime` / ポインタだけを埋める 84 行。**初版 §5.3 の置き換え表は丸ごと不要になった** |
| ライフサイクル IF(初版 §3.4) | [IPlatform::IsRenderable()](../aqEngine/Platform/IPlatform.h) / `ConsumeSurfaceChanged()` | 既定実装ありなので Win32 / Mac / UWP は挙動不変 |
| **書き込み先の抽象(初版 §3.6 の `GetWritableRoot`)** | [IPlatform::GetUserDataDirectory()](../aqEngine/Platform/IPlatform.h) | **名前と契約が違う**: 純粋仮想(既定実装なし)で、全プラットフォームが実装済み。ヘッダのコメントが**「Android / iOS はアプリのコンテナ自体が既にアプリ専用」**と iOS を名指ししている。初版の `GetWritableRoot` 追加案は**破棄**する |
| サーフェス再生成の共通経路(初版 §4.5 で「Android が入れば乗る」としたもの) | [IGraphicsDeviceImpl::RecreateSurface / GetSurfaceSize](../aqEngine/Graphics/IGraphicsDeviceImpl.h)、`Engine::EnsureSurfaceUpToDate()` / `SyncScreenSize()` | **ただし実装しているのは Vulkan だけ。Metal は既定(no-op / false)のまま**。§4.5 参照 |
| サウンドの中断 IF(初版 §6) | `ISoundBackend::OnSuspend/OnResume`、`SoundEngine::OnSuspend/OnResume`、`Engine::SyncSoundActivity()` | 呼び出し経路は通っている。iOS は `CoreAudioSoundBackend` にこの 2 つを実装して AVAudioSession の中断へ繋ぐだけ |
| 非 Windows の PNG/JPG デコード | [ImageLoader.cpp](../aqEngine/Resource/ImageLoader.cpp) の stb_image 経路 | ガードが `AQ_PLATFORM_MAC` から **`!AQ_PLATFORM_WINDOWS_FAMILY`** へ広がった。iOS は無条件で乗る |

#### (2) 初版の判断が**覆った**もの ★重要

**初版 §7.1 の「案 (a) Mac と同じ `chdir` 方式【推奨】」は撤回する。**
Android 移植が、初版が「本来やるべき掃除だが iOS 移植とは分けて別途行う」とした
**案 (b)(`GetContentRoot()` への統一)を先にやってしまった**ため。

| 初版 §7.1 で「CWD 相対のまま」としていた系統 | 現在 |
|---|---|
| JSON 全般(Prefab / Level / UIDocument / AudioBank / Particle) | [SimpleJson::ParseFile](../aqEngine/Util/SimpleJson.cpp):205 が `ResolveExistingResourcePath()` を通す |
| 画像 | [ImageLoader.cpp](../aqEngine/Resource/ImageLoader.cpp):109 が同上。失敗時に `StartupMarkf` でログを残す |
| サウンド | [WavDecoder.cpp](../aqEngine/Sound/Decoder/WavDecoder.cpp):113 / [WavStreamDecoder.cpp](../aqEngine/Sound/Decoder/WavStreamDecoder.cpp):57 が同上 |
| リソース一般 | [Resource.cpp](../aqEngine/Resource/Resource.cpp):256 `FindProjectRoot` が **`Engine::GetContentRoot()` を最優先**で見るようになった(返れば上方探索をしない) |
| Vulkan シェーダ | [VulkanShader.cpp](../aqEngine/Graphics/Vulkan/VulkanShader.cpp):28 も同じ形に揃った |

つまり **iOS は `chdir` せず、`PlatformiOS::GetContentRoot()` が
`[[NSBundle mainBundle] bundlePath]` を返すだけでよい**。初版が「この負債が
3 プラットフォーム目に広がる」と自覚していた事態は回避できる。

> **ただし穴が 1 つ残っている(iOS 固有の実作業)。**
> `FindProjectRoot` の重複 6 本のうち **`GetContentRoot()` を見るようになったのは
> `Resource.cpp` と `VulkanShader.cpp` の 2 本だけ**で、
> [MetalShader.mm:35](../aqEngine/Graphics/Metal/MetalShader.mm) と
> [MetalRenderContextImpl.mm:51](../aqEngine/Graphics/Metal/MetalRenderContextImpl.mm) は
> **今も CWD からの上方探索のみ**。Android は Vulkan なので踏まなかったが、
> **iOS は Metal 一本なのでここで必ず詰まる**(シェーダが 1 本も読めない = 黒画面)。
> 残る 2 本(D3D11 / D3D12)は Windows 専用なので本移植の対象外。
> **P2 の必須項目**に格上げする(§7.1)。両ファイルはコード中に「写し。片方だけ
> 変えないこと」と明記されているので、必ず同時に直す。

#### (3) まだ iOS が自分でやらねばならないもの

| 項目 | 理由 | 節 |
|---|---|---|
| **`IPlatform::RunFrameLoop`** | Android は `android_main` が自前スレッドを持つので `while (PumpEvents())` がそのまま成立した。**iOS だけが run loop を OS に取られる。**初版 §3.3 の設計は生きている | §3.3 |
| `AQ_PLATFORM_IOS` / `AQ_PLATFORM_APPLE` | Android は `AQ_PLATFORM_ANDROID` を足しただけで、Apple 系の共通化はしていない | §2.1 |
| `PlatformiOS` / `AqMetalView` / `AqAppDelegate` / `iOSMain.mm` | 当然ながら新規 | §3 |
| `iOSTouchBackend` と 3 つの選択ヘッダの iOS 分岐 | [TouchBackend.h](../aqEngine/HID/TouchBackend.h) / [PadBackend.h](../aqEngine/HID/PadBackend.h) / [KeyboardMouseBackend.h](../aqEngine/HID/KeyboardMouseBackend.h) はいずれも `AQ_PLATFORM_ANDROID` 決め打ちの分岐になっている | §5.2 |
| **Metal の `RecreateSurface` / `GetSurfaceSize`** | Vulkan だけが override 済み | §4.5 |
| **Metal シェーダの `FindProjectRoot`** | 上記 (2) の穴 | §7.1 |
| BC 圧縮テクスチャの展開 | Android は ASTC/ETC 端末で、この論点自体が無かった | §4.3 |
| `--msl-ios` の MSL 生成 | Metal 固有 | §4.2 |
| AVAudioSession とカテゴリ / 中断 | iOS 固有 | §6 |
| ImGui のモバイル拡大(`MOBILE_UI_SCALE`) | [Application.cpp](../aqEngine/Core/Application.cpp):158 が `#if defined(AQ_PLATFORM_ANDROID)` で囲われている。**iOS も同じ理由で要る**(そのまま `AQ_PLATFORM_MOBILE` 相当へ広げるか、iOS を並記する) | §5.3 |
| `ShutdownMemory()` の呼び出し位置 | 4 つのエントリすべてが「Engine とプラットフォームを壊した**後**」に呼ぶ規約。**iOS は `main` が戻らないので、`applicationWillTerminate:` に置く**という構造上の差がある | §3.2 |

#### (4) Android から引き継ぐ既知の未解決

- **仮想パッドに描画が無い。** `VirtualPadBackend` は当たり判定の `Layout` を持つだけで、
  画面に何も出ない([Android移植設計.md §8-4](Android移植設計.md) が未決のまま)。
  Android 実機では「見えないパッドを勘で触る」状態。**iOS でも同じになる**ので、
  描画をどこに置くかは iOS の P3 でも同じ判断が要る(先に決めた側に合わせる)。

---

## 1. 現状の棚卸し

### 1.1 そのまま乗る資産

| 資産 | 実体 | iOS での扱い |
|---|---|---|
| **Metal バックエンド** | [Graphics/Metal/](../aqEngine/Graphics/Metal/) 21 ファイル | **12 TU が iOS SDK で無改修コンパイル可(実測)**。差分は §4 |
| `NativeWindowHandle` の契約 | `handle = CAMetalLayer*`([MetalGraphicsDeviceImpl.mm:407](../aqEngine/Graphics/Metal/MetalGraphicsDeviceImpl.mm)) | **不変**。iOS でも `UIView` の `+layerClass` で同じものが手に入る(§4.1) |
| プラットフォーム抽象 | [Platform/IPlatform.h](../aqEngine/Platform/IPlatform.h) | `PlatformiOS` を実装として追加。**ただし IF 拡張が 2 つ要る**(§3.3 / §3.6) |
| 入力 sink と backend | [HID/Mac/CocoaInputSink.{h,cpp}](../aqEngine/HID/Mac/CocoaInputSink.h) / `CocoaKeyboardBackend` / `CocoaMouseBackend` | **ObjC 非依存の `.cpp` なので無改修で流用可**。投入側(タッチ → sink)だけ書く(§5.1) |
| パッド | [HID/Mac/GameControllerPadBackend.mm](../aqEngine/HID/Mac/GameControllerPadBackend.mm) | GameController.framework は iOS でも同一 API。**無改修(実測でコンパイル通過)** |
| サウンド | `SoftwareMixer` / `MixerSoundVoice` / `ExtAudioFileDecoder` | **無改修(実測)**。出力ユニットだけ差し替え(§6) |
| シェーダ生成 | [Tools/ShaderCompile/compile_msl.cmake](../Tools/ShaderCompile/compile_msl.cmake) | `--msl-ios` と出力先の追加のみ。`AQ_MSL_OUT_DIR` は既にパラメータ化済(§4.2) |
| 数学 / 画像 / 物理 | ThirdParty 同梱の DirectXMath・DirectXTex(非 Windows 経路)・WinCompat・stb_image・Bullet | **実測で全 TU 通過**。ARM64 なので Mac と同条件 |
| ログ出力 | [Platform/Common/DebugOutputMac.cpp](../aqEngine/Platform/Common/DebugOutputMac.cpp) | `fputs(stderr)` なので iOS でもそのまま動く。**Mac/iOS 共通として改名する**(§3.1) |

Mac 固有 `#ifdef` はリポジトリ全体で 49 箇所(Mac 移植設計 §1.1 の計測)。**iOS はその大半を
Mac と共有できる**ため、`AQ_PLATFORM_APPLE` の導入で分岐の増加を抑えられる(§2.1)。

### 1.2 iOS で新たに要るもの(概観)

§0.6 の再評価を反映した現在の見積り。**初版から新規実装がかなり減っている**。

| 領域 | 新規 | 既存改修 |
|---|---|---|
| ビルド | `ios-*` プリセット、Info.plist、署名設定 | `PlatformDefs.h`、`AqCommon.cmake`、`aqEngine/CMakeLists.txt` の分岐(3 → 4)、ルート `CMakeLists.txt` の API 検証 |
| プラットフォーム | `Platform/iOS/`(3〜4 ファイル)、`Game/Application/iOSMain.mm` | **`IPlatform` にフレーム駆動の委譲**(§3.3)。`Engine::RunGame` のループ本体を切り出す。~~書き込み先~~ は `GetUserDataDirectory` として導入済 |
| グラフィックス | なし | MSL の `--msl-ios` 再生成、**BC 非対応時の展開経路**、機能クエリ、**Metal の `GetSurfaceSize`**(§4.5) |
| 入力 | **`iOSTouchBackend` 1 本のみ**(抽象・仮想パッド・ポインタ合成・ImGui シムはすべて導入済。§0.6) | `TouchBackend.h` / `PadBackend.h` / `KeyboardMouseBackend.h` の分岐、`MOBILE_UI_SCALE` の適用範囲 |
| サウンド | なし(出力ユニットの差し替えのみ) | `CoreAudioSoundBackend.mm` の include と subtype、AVAudioSession、`OnSuspend`/`OnResume` の実装 |
| リソース | なし | バンドルレイアウト、`package_app.cmake` の iOS 分岐、**Metal シェーダの `FindProjectRoot`**(§7.1)。~~`chdir`~~ は不要になった |

---

## 2. ビルドシステム

### 2.1 マクロ ★最初の罠

[PlatformDefs.h:11-23](../aqEngine/Platform/Common/PlatformDefs.h) は `__APPLE__` を見て
無条件に `AQ_PLATFORM_MAC` を定義する。**`__APPLE__` は iOS でも真**なので、現状 iOS は
「Mac」として扱われ、そのまま AppKit 経路に入って落ちる(§0.2 の実測はこれが原因)。

- 自動推定は `<TargetConditionals.h>` の **`TARGET_OS_IPHONE` を `__APPLE__` の判定より先**に見る
  (`TARGET_OS_IPHONE` はシミュレータでも真になる。実機/シミュレータの区別が必要なら
  `TARGET_OS_SIMULATOR` を別に見る)。
- 「ちょうど 1 つだけ定義」の静的検査に `AQ_PLATFORM_IOS` を加える。
- 派生マクロ:
  - **`AQ_PLATFORM_DESKTOP` に入れない**(Win32 / Mac のみ)。
  - `AQ_PLATFORM_WINDOWS_FAMILY` に入れない。
  - **`AQ_PLATFORM_APPLE`(Mac or iOS)を新設する。** Android 移植が `AQ_PLATFORM_MOBILE` を
    「使う場面が出てから足す」と保留したのとは対照的に、**iOS では初日から必要**:
    Metal・GameController・AudioToolbox/ExtAudioFile・stb_image・`sal.h` 経路・MRR 前提は
    すべて Mac と iOS で同一の分岐になるため、これが無いと `defined(AQ_PLATFORM_MAC) ||
    defined(AQ_PLATFORM_IOS)` が各所に散る。

> `AQ_PLATFORM_APPLE` 導入時は、既存の `AQ_PLATFORM_MAC` 49 箇所を 1 つずつ見て
> 「Mac 専用(AppKit / デスクトップ前提)」か「Apple 共通」かを判断する。**機械置換しない**
> (Mac 移植が `!defined(AQ_PLATFORM_UWP)` の 45 箇所に対して採ったのと同じ方針)。

### 2.2 CMake の分岐

現状は Windows と「それ以外(= Mac 前提)」の 2 分岐。

| ファイル | 現状 | 変更 |
|---|---|---|
| [cmake/AqCommon.cmake:68](../cmake/AqCommon.cmake) `aq_apply_platform_definitions` | `WIN32` / `APPLE` / `else() → FATAL_ERROR` | `APPLE` を **`IOS` と macOS に分ける**。CMake は `CMAKE_SYSTEM_NAME=iOS` のとき変数 `IOS` を真にする(`APPLE` も真なので **`IOS` を先に**書く) |
| [aqEngine/CMakeLists.txt](../aqEngine/CMakeLists.txt) のソース除外 | `if(WIN32) … else() …` | 3 分岐化。iOS では **`Platform/Mac/` と `Sound/CoreAudio/CoreAudioSoundBackend.mm` を除外**し `Platform/iOS/` を入れる。`HID/Mac/`・`ExtAudioFileDecoder`・`Graphics/Metal/` は**含める** |
| 同・フレームワーク([:182-192](../aqEngine/CMakeLists.txt)) | `elseif(APPLE)` で `Cocoa` 他 7 つ | iOS は **`Cocoa` → `UIKit`**。他 6 つは共通(§0.2-2) |
| 同・Vulkan SDK の解決 | `Vulkan` / `Metal` 構成で `VULKAN_SDK` を要求 | **変更なし**。iOS も MSL 生成に `dxc` / `spirv-cross` が要る(ホスト側で動かすツールなので iOS ターゲットでも同じ) |
| [Game/CMakeLists.txt:34](../Game/CMakeLists.txt) | `add_executable(Game WIN32 MACOSX_BUNDLE …)` | **`MACOSX_BUNDLE` のままで iOS でも `.app` が出る**。Info.plist と署名の設定を足す(§2.3) |
| 同 [:22-24](../Game/CMakeLists.txt) `if(NOT APPLE)` で `.mm` を除外 | Mac 前提 | `iOSMain.mm` / `MacMain.mm` の振り分けはガードマクロが行うので **CMake 側は変更不要**(両方コンパイルし、片方が空 TU になる) |
| ルート [CMakeLists.txt:57-63](../CMakeLists.txt) | `Metal` は macOS 専用で `FATAL_ERROR` | iOS も許可。逆に **iOS では Metal のみ**に制限(§0.3) |
| 同 `aqCompileMsl` の配線 | `AQ_GRAPHICS_API STREQUAL "Metal"` | iOS では `--msl-ios` 版のターゲットを配線(§4.2) |
| [CMakePresets.json](../CMakePresets.json) | `windows-*` / `macos-*` | `ios-xcode` / `ios-simulator-xcode` / `ios-ninja`(コンパイル検証用)を追加 |

注意点:

- **`APPLE` は iOS でも真、`UNIX` も真。** 分岐は必ず `if(IOS)` を**先に**書く。
- `CMAKE_OSX_SYSROOT` で実機(`iphoneos`)/ シミュレータ(`iphonesimulator`)を切り替える。
  Xcode ジェネレータなら `CMAKE_OSX_SYSROOT=iphoneos` + 実行時に destination 指定でもよい。
- `CMAKE_OSX_DEPLOYMENT_TARGET` を明示する(§0.5-1 の BC 判断と連動。BC の API 自体が
  iOS 16.4 で入ったため、それより低いターゲットでは `MTLPixelFormatBC*` の使用が
  availability 警告になる)。

### 2.3 バンドル・Info.plist・署名

iOS のアプリバンドルは **`Contents/` 階層を持たない**(リソースはバンドル直下)。ここが
Mac との最大の構造差で、§7.3 のレイアウトに直接効く。

必要な Info.plist キー(macOS では不要だったもの):

| キー | 値 | 理由 |
|---|---|---|
| `CFBundleIdentifier` | `com.aqengine.aquadash`(既存の `MACOSX_BUNDLE_GUI_IDENTIFIER` を流用) | iOS では**必須**。なお macOS でもこれが空だとキーウィンドウにならずキー入力が届かない既知の罠がある([Game/CMakeLists.txt:39-52](../Game/CMakeLists.txt)) |
| `UIRequiredDeviceCapabilities` | `metal` | Metal 非対応端末を除外 |
| `UISupportedInterfaceOrientations` | 横向き 2 つ(§0.5-5 で (a) を採る場合) | 回転対応を範囲外にする |
| `UILaunchScreen` | 空辞書でよい | iOS 14+ では launch screen が無いとリジェクト/黒画面になる |
| `CFBundleExecutable` / `UIDeviceFamily` | CMake が設定 | — |

署名は `XCODE_ATTRIBUTE_DEVELOPMENT_TEAM` / `CODE_SIGN_STYLE` / `PRODUCT_BUNDLE_IDENTIFIER` を
ターゲットプロパティで与える。**シミュレータ構成では `CODE_SIGNING_ALLOWED=NO` で足りる。**

### 2.4 デプロイと実行

| 対象 | 方法 | 署名 |
|---|---|---|
| シミュレータ | `xcrun simctl install <udid> Game.app` → `simctl launch` | 不要 |
| 実機 | Xcode から Run、または `xcrun devicectl device install app` | 必要 |

シミュレータは `simctl spawn` でバイナリを直接叩けるので、**機能クエリ等の単発検証は
アプリを組む前に確認できる**(§0.2-3 の実測はこの方法で取った)。

---

## 3. プラットフォーム層

### 3.1 責務表

| ファイル(新設) | 責務 |
|---|---|
| `aqEngine/Platform/iOS/PlatformiOS.h` / `.mm` | `IPlatform` 実装。`CreateMainWindow` で `UIWindow` + ルート `UIViewController` + `AqMetalView` を生成し `CAMetalLayer*` を返す。`PumpEvents` は**終了フラグを見るだけ**(イベント配送は UIKit が行う)。`RunFrameLoop` で `CADisplayLink` を張る。`GetContentRoot`(バンドルパス)/ `GetUserDataDirectory`(`NSDocumentDirectory`)/ `IsRenderable`。ObjC 型はヘッダに漏らさない(`iOSWindowObjects*` の前方宣言のみ。[PlatformMac.h:12-14](../aqEngine/Platform/Mac/PlatformMac.h) と同じ作法) |
| `aqEngine/Platform/iOS/AqMetalViewIOS.mm`(`PlatformiOS.mm` に同居でもよい) | `UIView` 派生。`+layerClass` を `CAMetalLayer` に override。`touchesBegan/Moved/Ended/Cancelled` を受けて **`iOSTouchBackend` が読むタッチ状態へ投入する**(ImGui へは `TouchMouseBackend` → `ImGuiPointerInput` の共通経路で自動的に届くので、ここから ImGui を直接叩かない)。`layoutSubviews` で `drawableSize` を更新 |
| `aqEngine/Platform/iOS/iOSAppDelegate.mm` | `UIApplicationDelegate`。エンジンのブートストラップと終了、ライフサイクル通知(§3.2 / §3.4) |
| ~~`aqEngine/Platform/iOS/iOSImGui.h` / `.mm`~~ | **不要になった。** 共通の [ImGuiPointerInput](../aqEngine/Platform/Common/ImGuiPointerInput.h) をそのまま使う(§0.6 / §5.3) |
| `Game/Application/iOSMain.mm` | `int main()` → `UIApplicationMain`。`#if defined(AQ_PLATFORM_IOS)` ガード。既存 3 本のエントリ(`Main.cpp` / `UWPMain.cpp` / `MacMain.mm`)に続く 4 本目 |

改修対象:

| ファイル | 変更 |
|---|---|
| [Platform/IPlatform.h](../aqEngine/Platform/IPlatform.h) | **`RunFrameLoop` を追加(§3.3)。これが本移植で共通コードへ入れる唯一の追加 IF。** `GetUserDataDirectory` / `IsRenderable` / `ConsumeSurfaceChanged` は Android 移植で導入済なので**実装するだけ**(§0.6) |
| [Engine.cpp](../aqEngine/Engine.cpp) `RunGame` | ループ本体を `FrameStep()` へ切り出し、`RunFrameLoop` へ渡す(§3.3)。**本体の中身は変えない** |
| [Platform/PlatformBudget.h](../aqEngine/Platform/PlatformBudget.h) | iOS プロファイル。**Win32/Mac の「上限なし・論理コア数」は使えない**(§3.6) |
| [Platform/Common/DebugOutputMac.cpp](../aqEngine/Platform/Common/DebugOutputMac.cpp) | `fputs(stderr)` は iOS でもそのまま動く。**`DebugOutputApple.cpp` へ改名**して Mac/iOS 共通にする(vcxproj には未登録で CMake からのみ拾っているため、改名コストはほぼゼロ) |

### 3.2 エントリと `UIApplicationMain`

UIKit では `UIApplicationMain` が**戻ってこない**。したがって [MacMain.mm:85-130](../Game/Application/MacMain.mm) の
「`main` が Engine の生成から `Finalize` まで全部持つ」形は使えず、**ブートストラップを
AppDelegate のコールバックへ移す**。`main` 自体は 3 行になる。

```
iOSMain.mm:
    int main(int argc, char* argv[]) {
        @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"AqAppDelegate"); }
    }

AqAppDelegate:
    application:didFinishLaunchingWithOptions:
        1. PlatformiOS を生成(寿命はデリゲートが持つ。MacMain はスタックに置いていた)
        2. UIScreen から実解像度を決める(§3.5)
        3. Engine::Create → CreateApplication<app::Application>
        4. InitializeParameter を埋めて Engine::Initialize
             ここで CreateMainWindow が UIWindow / View / CAMetalLayer を作る
        5. Engine::RunGame()   ← CADisplayLink を張って即 return する(§3.3)
        6. return YES
    applicationWillTerminate:
        Engine::Finalize()
    applicationDidEnterBackground: / applicationWillEnterForeground:
        §3.4
```

**`Engine::RunGame()` の直後に `Finalize()` を置かない**のが Mac との決定的な差。
`MacMain.mm` の形をそのまま写すと 1 フレームも回らずに終了する。

`main` を `.mm` に置くのは Mac と同じ。[Main.cpp](../Game/Application/Main.cpp) は
`AQ_PLATFORM_WIN32`、`MacMain.mm` は `AQ_PLATFORM_MAC` でガードされているので、
`iOSMain.mm` を足しても他構成では空 TU になる(既存の作法どおり)。

### 3.3 メインループの所有権 ★本移植で最大の新規設計

[Engine::RunGame()](../aqEngine/Engine.cpp) は現在こうなっている
(**Android 移植でループ本体が増えた**。初版執筆時は `Update()` 1 行だった):

```cpp
void Engine::RunGame() {
    while (platform_->PumpEvents()) {
        SyncSoundActivity();                        // 前面/背面をサウンドへ
        if (!EnsureSurfaceUpToDate()) { continue; } // サーフェス作り直し
        if (!platform_->IsRenderable()) { continue; }
        SyncScreenSize();
        Update();
    }
}
```

**この本体は iOS でもそのまま必要**(サーフェス追従とサウンドの前面/背面判定は
iOS でも同じ意味を持つ)。したがって委譲するのは `Update()` ではなく**本体全体**になる。

Win32 / Mac はこれで成立する(Mac は `[NSApp run]` を**呼ばず** `finishLaunching` だけ手動で
呼び、自前ループが run loop の役をしている。[MacMain.mm:105-107](../Game/Application/MacMain.mm))。
**iOS では `UIApplicationMain` が run loop を所有するので、この形は取れない。**

| 案 | 内容 | 判断 |
|---|---|---|
| **(a) `IPlatform` にフレーム駆動の委譲を足す**【推奨】 | `virtual void RunFrameLoop(const std::function<void()>& frame)` を追加。**既定実装が現行の while ループそのもの**なので Win32 / UWP / Mac は挙動不変。iOS だけ override して `CADisplayLink` を張り即 return する | **採用。** 追加 IF 1 本・既定実装で既存プラットフォームは無変更・呼び出し側は `Engine::RunGame` の 1 行だけ |
| (b) `PumpEvents` を `CFRunLoopRunInMode(…, 0, true)` の 1 回 spin にする | while ループを維持したまま run loop を回す | **不採用。** `UIApplicationMain` を呼ばずに `UIApplication` を成立させる必要があり、UIKit の想定外。ライフサイクル通知やタッチ配送が保証されない |
| (c) ゲームループを専用スレッドで回す | main スレッドは UIKit、別スレッドで `while` | **不採用。** UIKit のタッチ配送と `CAMetalLayer` 操作が main スレッド前提で、既存の RenderThread([05_マルチスレッド設計.md](05_マルチスレッド設計.md))と二重のスレッド境界ができる。事故の期待値が高い |

案 (a) の形:

```cpp
// IPlatform.h に追加(<functional> は aq.h:118 で既に PCH に入っている)
/// フレーム駆動をプラットフォームへ委譲する。
/// 既定実装はデスクトップ相当:PumpEvents が false を返すまで frame() を回す。
/// iOS のように run loop を OS が所有する環境では override し、
/// フレーム駆動を登録して**すぐ return する**。
virtual void RunFrameLoop(const std::function<void()>& frame)
{
    while (PumpEvents()) { frame(); }
}
```

`Engine::RunGame()` は次の形になる。**ループ本体を `FrameStep()` へ切り出すだけで、
中身は一切変えない**(Win32 / UWP / Mac / Android は既定実装を通るので挙動不変)。

```cpp
void Engine::FrameStep()   // ← 現在の while ループの中身をそのまま移す
{
    SyncSoundActivity();
    if (!EnsureSurfaceUpToDate()) { return; }
    if (!platform_->IsRenderable()) { return; }
    SyncScreenSize();
    Update();
}

void Engine::RunGame()
{
    platform_->RunFrameLoop([this]{ FrameStep(); });
}
```

`continue` が `return` に変わる点だけ注意する(意味は同じ = このフレームは何もしない)。
`std::function` の生成はフレームあたりではなく**起動時 1 回**なので性能上の懸念はない。

`PlatformiOS::RunFrameLoop` は `CADisplayLink` を `frame` を呼ぶセレクタに繋いで
run loop へ add する。`preferredFramesPerSecond` はゼロ(= ディスプレイ任せ)を既定とし、
[GameTimer](../aqEngine/Util/GameTimer.h) の FPS 制限とは二重にしない。

> `PlatformiOS::PumpEvents()` は「終了要求フラグを返すだけ」の実装になる。iOS にアプリ終了の
> 概念(閉じるボタン)は無いので、実質常に `true`。`IPlatform` の契約は保つ。

### 3.4 ライフサイクル

[IPlatform.h:37-38](../aqEngine/Platform/IPlatform.h) には既に `OnSuspend()` / `OnResume()` が
あり(UWP 用。**Mac は override していない**)、iOS はこれを使う。

- `applicationDidEnterBackground:` → **`CADisplayLink` を invalidate/paused にする**。
  バックグラウンドで Metal のコマンドを出すと iOS はアプリを kill する。
- `applicationWillEnterForeground:` → 再開。
- Android と違い **iOS では `CAMetalLayer` が破棄されない**ので、
  [Android移植設計.md §3.3](Android移植設計.md) が必要としている
  `IGraphicsDeviceImpl::RecreateSurface` は **iOS では不要**。ドローアブルが一時的に取れない
  だけで、それは既に「drawable が nil のフレームは丸ごと捨てる」実装が入っている
  ([MetalGraphicsDeviceImpl.mm:706-717](../aqEngine/Graphics/Metal/MetalGraphicsDeviceImpl.mm))。
  **ここは iOS が Android より明確に楽な点。**
- **`IsRenderable()` は Android が先に入れた**(§0.6)。iOS では
  「`CADisplayLink` を止める」で描画は止まるが、`Engine::SyncSoundActivity()` が
  `IsRenderable()` を見て**サウンドの停止/再開を決めている**ため、
  **`PlatformiOS` も背面で `false` を返すこと**。返さないと背面で BGM が鳴り続ける。
  逆に `ConsumeSurfaceChanged()` は既定(`false`)のままでよい(下記のとおり iOS では
  レイヤが差し替わらない)。
- サウンドの中断(電話着信等)は §6。

### 3.5 解像度とスケール

[Engine::InitializeWindow](../aqEngine/Engine.cpp) は `InitializeParameter` の値を
`screenWidth_` / `screenHeight_` に入れた**後**に `CreateMainWindow` を呼ぶ。つまり
**エンジンは「ウィンドウサイズは呼び出し側が知っている」前提**で、`CreateMainWindow` から
実サイズを受け取る経路が無い。

iOS で画面サイズは OS が決めるので、**`InitializeParameter` は実際に作られた
ウィンドウのサイズで埋める**。これで**エンジン側の改修はゼロ**。
`CreateMainWindow` は `desc` の寸法を使わない(OS が決めた値で上書きする)。

実装した形(P1。**Android の `AndroidMain.cpp` と同型**):

1. `PlatformiOS::CreateMainWindow` が `UIScreen` / `window.bounds` から実寸を決めてレイヤを作る
   (**冪等**。2 回呼ばれても同じレイヤを返す)
2. 呼び出し側は `GetDrawableWidth()` / `GetDrawableHeight()` で実寸を取り出す
3. P2 ではその値で `InitializeParameter` を埋めてから `Engine::Initialize` を呼ぶ
   (内部でもう一度 `CreateMainWindow` が呼ばれるが、冪等なので同じウィンドウが返る)

**P1 実測: iPhone 17 シミュレータで 874x402**(論理解像度・横向き)。

スケールの方針(§0.5-4):

- **`contentsScale = 1.0`(論理解像度)で開始する。** Mac も同じ判断で
  `setContentsScale:1.0` に固定している([PlatformMac.mm:45-61](../aqEngine/Platform/Mac/PlatformMac.mm) の設計コメント)。
  ImGui の `DisplayFramebufferScale = (1,1)` 前提([MacImGui.mm](../aqEngine/Platform/Mac/MacImGui.mm))も
  そのまま保たれる。
- 見た目は Core Animation の引き伸ばしになるので**ぼやける**。これは承知の上で、
  解像度スケール(1.0 / 2.0 / native の選択)は P6 の性能調整項目にする。
- 画面の向きは横向き固定(§0.5-5)。**回転を許すと `drawableSize` が変わり、
  Metal バックエンドにリサイズ経路が無い問題(§4.5)を本移植で解かねばならなくなる。**

### 3.6 ログ・診断・予算

**書き込み先の抽象が要る。** iOS ではアプリバンドルが read-only で、カレントディレクトリも
当てにできない。現在 CWD へ書いているもの:

| 書き込み先 | 場所 | iOS での扱い |
|---|---|---|
| `startup_timing.log` | [aq.cpp:55](../aqEngine/aq.cpp) | 書き込み先を付け替える |
| `imgui.ini` | ImGui 既定(`io.IniFilename`)。エンジン側に上書きが**無い** | `io.IniFilename` を明示設定する |
| `vk_debug.log` | [VulkanGraphicsDeviceImpl.cpp:152](../aqEngine/Graphics/Vulkan/VulkanGraphicsDeviceImpl.cpp) | iOS は Vulkan 構成を持たない(§0.3)ので無関係 |
| エディタ保存(Prefab / Level / UIDocument / TextStyle) | [SimpleJson.cpp:339](../aqEngine/Util/SimpleJson.cpp) 経由 | iOS では保存 UI を出さない |

**この IF は既に入っている(初版の `GetWritableRoot` 案は破棄する)。**
[IPlatform::GetUserDataDirectory()](../aqEngine/Platform/IPlatform.h) が
ゴーストリプレイ(P23)と Android 移植で導入され、Win32 / UWP / Mac / Android の
4 実装が揃っている。**純粋仮想なので `PlatformiOS` は実装が必須**。

- 返すのは `NSDocumentDirectory`(セーブ/ゴースト)。**末尾にセパレータを含める**契約。
- ヘッダのコメントが「Android / iOS はアプリのコンテナ自体が既にアプリ専用なので、
  そこへさらにアプリ名を足すのは冗長」と**iOS を名指しで**決めているので、
  アプリ名のサブフォルダは作らない。
- `startup_timing.log` / `io.IniFilename` はこのディレクトリ配下へ回す。
  ログだけ `NSCachesDirectory` に分けたければそれでもよいが、追加 IF は作らない。

`PlatformBudget` の iOS プロファイル:

- `memoryBudgetBytes`: iOS はメモリ上限で OS に kill される。**Win32/Mac の 0(上限なし)は
  使えない。** 具体値は実機の機種で決める(§0.5-2 が決まるまで暫定値)。
- `threadPoolWorkerCount`: `hardware_concurrency` そのままを避ける。iPhone も big.LITTLE 構成で、
  小コアに描画スレッドが載ると詰まる(Android 移植設計 §3.4 と同じ理由)。
- `stackSizeBytes`: Mac と同じ 4MB で開始。
- [aq.cpp](../aqEngine/aq.cpp) のクラッシュスタックロガーは VEH + dbghelp で Windows 専用。
  iOS では枠ごと `#ifdef` 対象のまま(Mac と同じ)。

---

## 4. グラフィックス(Metal)

一次資料は [MetalBackend設計.md](MetalBackend設計.md)。本節は iOS 差分のみ。

### 4.1 レイヤ生成

`NativeWindowHandle.handle = CAMetalLayer*` の契約は**不変**。生成側だけ変わる。

| | Mac | iOS |
|---|---|---|
| ホスト | `AqMetalView : NSView` + `wantsLayer=YES` + `setLayer:` | `AqMetalView : UIView` + **`+layerClass` を `CAMetalLayer` に override** |
| 親 | `NSWindow` | `UIWindow` + ルート `UIViewController` |
| 表示 | `makeKeyAndOrderFront:` + `[NSApp activate]` | `[window makeKeyAndVisible]` |
| 座標系 | 左下原点 → **Y 反転が必要** | **左上原点。Y 反転処理は入れない** |
| スケール追従 | `viewDidChangeBackingProperties` / `setFrameSize:` | `layoutSubviews` / `traitCollectionDidChange:` |

[MetalGraphicsDeviceImpl.mm:404-421](../aqEngine/Graphics/Metal/MetalGraphicsDeviceImpl.mm) が
レイヤに設定するのは `device` / `pixelFormat`(BGRA8Unorm) / `framebufferOnly=NO` / `opaque=YES` のみで、
**macOS 専用プロパティ(`displaySyncEnabled` 等)は使っていない**(実測 §0.2)。ここは無改修。

`framebufferOnly = NO` は iOS ではコストが高い(タイルメモリ最適化を捨てる)。これは
`CopyToBackBuffer` の blit 経路のために立てているもので、blit はフォーマット不一致で
ほぼ常にフルスクリーン描画へ落ちている。**`YES` に戻して描画一本化する**のは P6 の性能項目。

### 4.2 MSL の再生成(`--msl-ios`)

[compile_msl.cmake:11](../Tools/ShaderCompile/compile_msl.cmake) の現行呼び出しは

```
spirv-cross --msl --msl-version 20000 --msl-decoration-binding --output <out.metal> <out.spv>
```

で、**プラットフォーム指定が無い = macOS 版 MSL が出る**。iOS 用には `--msl-ios` を足し、
**出力先を `Game/Assets/Shader/msl-ios/` に分ける**。理由は Vulkan の `spv/` と Metal の `msl/` を
分けているのと同じ(同名で中身が別物になると混ざって壊れる)。

- `AQ_MSL_OUT_DIR` は既にパラメータ化済([compile_msl.cmake:53](../Tools/ShaderCompile/compile_msl.cmake))なので、
  ターゲットを 1 つ足すだけで済む。
- 読み出し側 [MetalShader.mm](../aqEngine/Graphics/Metal/MetalShader.mm) の `BuildMslPath()` と
  [MetalRenderContextImpl.mm](../aqEngine/Graphics/Metal/MetalRenderContextImpl.mm) の
  同名処理(**写し。片方だけ変えないこと**とコード中に明記されている)に `msl-ios` 分岐を入れる。
- `dxc` / `spirv-cross` は Vulkan SDK 同梱の**ホスト側ツール**なので、iOS ターゲットでも
  そのまま動く。`VULKAN_SDK` 要求は維持(§2.2)。
- 既知の未解決: `ClusterCull.main.cs` は生成 MSL がアドレス空間をまたぐキャストを含み
  `newLibraryWithSource:` が失敗する([MetalBackend設計.md §13-1](MetalBackend設計.md))。
  **iOS 固有ではない**ので本書では扱わない。

### 4.3 BC 圧縮テクスチャ ★iOS 固有の最大リスク

**実測: iOS シミュレータは `supportsBCTextureCompression == NO`**(§0.2-3)。
一方 iOS SDK のヘッダでは `MTLPixelFormatBC1_RGBA` 等が `API_AVAILABLE(ios(16.4))` で
宣言されており、`supportsBCTextureCompression` 自体も `ios(16.4)` から存在する。

つまり **「iOS では BC が使えない」は不正確**で、正しくは:

- **API としては iOS 16.4 以降に存在する。**
- **実際に使えるかは実行時の `supportsBCTextureCompression` 次第。**
- **シミュレータは `NO` を返す(実測)。実機がどうかは手元に端末が無いため未確認。**

影響範囲: [MetalCommon.h:106-116](../aqEngine/Graphics/Metal/MetalCommon.h) が BC1〜BC7 を
そのまま写像しており、DDS アセットは **7 枚**(最大 `utc_all2.DDS` 5.3MB)。非対応環境では
`newTextureWithDescriptor:` が nil を返し、テクスチャが全部落ちる。

| 案 | 内容 | 判断 |
|---|---|---|
| **(a) 実行時に BC を展開**【推奨】 | `supportsBCTextureCompression == NO` のとき、DDS ロード時に `DirectX::Decompress` で RGBA8 へ展開して渡す | **採用。** BC ソフトコーデック(`BC.cpp` / `BC4BC5.cpp` / `BC6HBC7.cpp` / `DirectXTexCompress.cpp`)は**非 Windows ビルドに既に入っている**([ThirdParty/CMakeLists.txt:84-104](../ThirdParty/CMakeLists.txt))。アセット無改変。7 枚なので展開コストも許容範囲 |
| (b) ASTC へ再エンコード | オフラインで変換し、ローダと写像に ASTC を追加 | VRAM は最小で済むが、変換ツールの導入・アセットの二重管理・`PixelFormat` 列挙の拡張が必要。(a) で困ってから |
| (c) BC 対応端末に限定 | `supportsBCTextureCompression == YES` を要求 | **シミュレータで動かせなくなる**ので、§0.5-2 の「シミュレータ先行」と両立しない |

あわせて**機能クエリを新設する**。現在 `Graphics/Metal/` には `supportsFamily` /
`supportsBCTextureCompression` 等の問い合わせが**一箇所も無く**(実測)、非対応を検出せずに
静かに失敗する。最低限 `supportsBCTextureCompression` と GPU family を起動ログへ出す。

### 4.4 メモリと性能(タイラー GPU)

1280×720 相当での実測見積り(内訳は [MetalBackend設計.md](MetalBackend設計.md) と
[01_レンダリング設計.md](01_レンダリング設計.md) 側の構成から算出):

| 用途 | 概算 |
|---|---|
| **シャドウ深度配列**(2048² × 4 スライス × Depth32Float) | **約 64 MB** ← 単独最大 |
| G-Buffer 4 枚(RGBA8 + RGBA16F×3)+ 深度 | 約 28 MB |
| メイン RT(RGBA16F × 2 + 深度) | 約 21 MB |
| Bloom(bright + 4 段ピラミッド) | 約 9 MB |
| トーンマップ最終 RT / HiZ / 定数バッファ | 約 10 MB |
| **合計** | **約 130〜140 MB** |

- **シャドウ 2048² × 4 スライスが最大の削減候補。** 1024² に落とせば 48MB 削れる。
- RT はすべて `MTLStorageModePrivate`。**iOS のみ `MTLStorageModeMemoryless`** に落とせる
  深度がある(パス内で読み返さないもの)ので、P6 の候補。
- compute の threadgroup は最大 **64** スレッドで、シミュレータ実測の
  `maxThreadsPerThreadgroup = 512` に全く触れない。**無改修で通る。**
- 解像度スケールを上げる判断(§0.5-4)はこの表を 2〜9 倍にする。**先にメモリ、次に見た目。**

### 4.5 リサイズ経路 — 共通経路は入ったが Metal は未実装

**共通側は Android 移植で入った。**
[IGraphicsDeviceImpl](../aqEngine/Graphics/IGraphicsDeviceImpl.h) に
`RecreateSurface()` / `GetSurfaceSize()` が足され、`Engine::EnsureSurfaceUpToDate()` と
`Engine::SyncScreenSize()` が毎フレーム面倒を見る。初版が「Android が入れば iOS の
回転対応も乗る」と書いた予測は**半分だけ当たった**。

| | 状態 |
|---|---|
| `IGraphicsDeviceImpl` の IF | **あり**(既定は `RecreateSurface` → `true`、`GetSurfaceSize` → `false`) |
| `Engine` 側の呼び出し | **あり**(`RunGame` のループ本体) |
| **Vulkan の override** | **あり**([VulkanGraphicsDeviceImpl.h:37-38](../aqEngine/Graphics/Vulkan/VulkanGraphicsDeviceImpl.h)) |
| **Metal の override** | **無し。** `Graphics/Metal/` に両方とも実装が無い |

`Graphics/Metal/` にリサイズ経路が無い事情は初版のまま:
メイン RT / G-Buffer / シャドウは初期サイズで固定され、drawable だけ大きくなると
`CopyToBackBuffer` が寸法不一致を検出してフルスクリーン三角形で拡大コピーする。

**本移植の方針:**

- **`RecreateSurface` は実装しない。** iOS では `CAMetalLayer` が破棄されないので
  呼ばれる契機が無い(§3.4)。`PlatformiOS::ConsumeSurfaceChanged` を既定の `false` の
  ままにしておけば `EnsureSurfaceUpToDate` は素通りする。
- **`GetSurfaceSize` は実装する**(P2)。`layer.drawableSize` を返すだけ。
  実装しないと `SyncScreenSize` が何もせず、`screenWidth_/screenHeight_` が
  AppDelegate の初期値に固定される。仮想パッドの当たり判定と ImGui の `DisplaySize` が
  これを見るため、Safe Area やスケール変更で**ずれると指の位置が合わなくなる**。
  - ⚠ **Mac への影響**: Metal は Mac と共有のバックエンドなので、これは Mac の挙動も
    変える(今まで `false` で据え置かれていた `screenWidth_` が drawableSize に追従する)。
    Mac は `contentsScale = 1.0` 固定なので値は一致するはずだが、**Mac の回帰確認に
    必ず含める**(§8-6)。
- **オフスクリーン RT の作り直しは範囲外。** §0.5-5 で横向き固定を選び、
  `drawableSize` が変わらないようにして回避する。

> Android は同じ問題を「スワップチェーンだけ作り直し、オフスクリーン RT は据え置く」で
> 通している([Android移植設計.md §4.3](Android移植設計.md))。Metal で回転対応まで
> やるならその形に倣うが、本移植では扱わない。

### 4.6 シェーダの起動時コンパイル(P6 の候補)

[MetalShader.mm](../aqEngine/Graphics/Metal/MetalShader.mm) は **59 本の MSL を毎起動
`newLibraryWithSource:` でコンパイル**する。macOS では Metal Toolchain が無い環境だったため
この方式を採ったが、**iOS 向けには Xcode に Metal Toolchain が同梱されている**ので
`.metallib` の事前ビルドが可能になる。起動時間に効くので P6 の候補に置く。
**P1〜P5 では現行方式を維持**する(変更点を増やさない)。

### 4.7 デバイス機能の実測ゲート ★P2 で追加

**コンパイルが通ることと、そのデバイスで動くことは別**だった。§0.2 の事前調査は
`API_UNAVAILABLE(ios)` の有無しか見られないので、**実行時のデバイス能力差は検出できない**。
P2 でシミュレータ(GPU family Apple2)を動かして 2 件のアサート即死を踏んだ。

| 機能 | 要件 | シミュレータ | 非対応時の挙動 | 対処 |
|---|---|---|---|---|
| サンプラのボーダーカラー(`MTLSamplerBorderColor*` / `ClampToBorderColor`) | **Apple7 / Mac2 以上** | Apple2 → **不可** | `MTLSamplerBorderColorOpaqueWhite is not supported on this device` で **Validation がアサート** | `metal::IsSamplerBorderColorSupported()` で判定し `ClampToEdge` へ落とす |
| read-write テクスチャ(compute のポストプロセス) | **`MTLReadWriteTextureTier2`**(HDR RGBA16Float を読み書きするため) | **Tier 0** | `hardware does not support read-write texture of this pixel format` で **アサート** | tier を実測し、足りなければ `SetComputeSupported(false)`(ポストプロセス無し) |
| BC 圧縮テクスチャ | `supportsBCTextureCompression` | **不可** | テクスチャ生成が nil を返して**静かに全滅** | `IsBlockCompressionSupported()` → `ImageLoader` が RGBA8 へ展開(§4.3) |

いずれも **`MetalGraphicsDeviceImpl::Initialize` で実測して起動ログに残し、
フラグへ流す**形に統一した。**Mac(Apple9 / Tier2)ではすべて対応ありなので挙動は変わらない。**

> **実機では 3 つとも対応している見込み**(A14 以降が Apple7、BC は iOS 16.4 以降)。
> つまり**シミュレータでだけ通る劣化経路**であり、実機で本来の経路を通ることの確認は
> P5 の項目になる。ここを取り違えると「シミュレータで動いたから大丈夫」と誤認する。

---

## 5. 入力

### 5.1 そのまま流用できるもの

| 資産 | 理由 |
|---|---|
| [CocoaInputSink.{h,cpp}](../aqEngine/HID/Mac/CocoaInputSink.h) | **ObjC を一切含まない `.cpp`**。座標変換と Y 反転は投入側(`PlatformMac`)に寄せてある設計なので、iOS では投入側だけ書けばよい。「1 フレーム内で押して離した入力を取りこぼさない」`pressedSinceFetch_` の仕組みは**タッチでも全く同じ問題が起きる**ので特に価値が高い |
| `CocoaKeyboardBackend` / `CocoaMouseBackend` | `Fetch*` を呼ぶだけの 26 行。無改修 |
| [GameControllerPadBackend.mm](../aqEngine/HID/Mac/GameControllerPadBackend.mm) | GameController.framework は iOS でも同一 API。**実測でコンパイル通過** |
| [KeyboardMouseBackend.h](../aqEngine/HID/KeyboardMouseBackend.h) / [PadBackend.h](../aqEngine/HID/PadBackend.h) | コンパイル時 typedef で選ぶ方式。**`AQ_PLATFORM_IOS` 分岐を 1 つ足すだけ** |
| `NullKeyboardBackend` / `NullMouseBackend` | 段階的立ち上げの足場として再利用(P1〜P2 で差しておく) |

**命名の整理**: これらは `HID/Mac/` にあるが iOS でも使うため、**`HID/Apple/` へ移して
`AppleInputSink` / `AppleKeyboardBackend` / `AppleMouseBackend` に改名する**。いずれも
vcxproj には未登録(Mac 専用として除外されている)なので CMake だけの変更で済む。
実施は P3(入力フェーズ)で、それまでは現名のまま `Platform/iOS/` から参照する。

### 5.2 タッチ ★実装は `iOSTouchBackend` 1 本だけ

`ITouchBackend` の**抽象定義は
[Android移植設計.md §5.2](Android移植設計.md) を一次資料とする**(同じ設計を二重に書かない)。
**Android 移植で実体が入った**([HID/ITouchBackend.h](../aqEngine/HID/ITouchBackend.h))ので、
iOS は「後から来た側」としてその定義に従う(初版 §8-7 の取り決めどおり)。

構成は Android と同型になる。**新規に書くのは網掛けの 1 本だけ**:

```
AqMetalView (touchesBegan/Moved/Ended/Cancelled)
        │  [touch locationInView:]  ← 左上原点。Y 反転しない
        ▼
  ┏━━━━━━━━━━━━━━━━━━━━┓
  ┃  iOSTouchBackend    ┃ ← 新規(Android は AndroidTouchBackend + AndroidInputSink)
  ┗━━━━━━━━━━━━━━━━━━━━┛
        │  ITouchBackend::Poll → InputManager が毎フレーム 1 回取り込む
        ├───────────────► VirtualPadBackend ──┐
        │                  (仮想スティック/ボタン)  ├─► CompositePadBackend ─► Pad / ActionMap
        │                  GameControllerPadBackend ┘
        └──(パッドが使っていない指)─► TouchMouseBackend ─► Mouse ─► UI / ImGuiPointerInput
```

`VirtualPadBackend` / `CompositePadBackend` / `TouchMouseBackend` /
`ImGuiPointerInput` はいずれも**プラットフォーム非依存として書かれている**(§0.6)ので、
iOS は選択ヘッダに分岐を足すだけで同じ組み立てになる。

| 選択ヘッダ | 現状 | iOS で足す分岐 |
|---|---|---|
| [HID/TouchBackend.h](../aqEngine/HID/TouchBackend.h) | `AQ_PLATFORM_ANDROID` → `AndroidTouchBackend` / それ以外 → `NullTouchBackend` | `AQ_PLATFORM_IOS` → `iOSTouchBackend` |
| [HID/PadBackend.h](../aqEngine/HID/PadBackend.h) | Mac → `GameControllerPadBackend` 単体 / Android → `CompositePadBackend`(物理 + 仮想) | **iOS は Mac と Android の合わせ技**: `CompositePadBackend` に `GameControllerPadBackend`(iOS でも同一 API。§5.1)と `VirtualPadBackend` を足す。`CreateDefaultPadBackend(touch)` の `#if` も同様 |
| [HID/KeyboardMouseBackend.h](../aqEngine/HID/KeyboardMouseBackend.h) | Android → `NullKeyboardBackend` + `TouchMouseBackend` | **iOS も同じ**。`CreateDefaultMouseBackend(pointerTouch)` の `#if` に iOS を並記 |

iOS 固有の差分:

| 論点 | 内容 |
|---|---|
| 投入元 | `AqMetalView` の `touchesBegan/Moved/Ended/Cancelled:withEvent:`。座標は `[touch locationInView:]`(**左上原点なので Y 反転しない**。`TouchPoint` の契約が「クライアント左上原点・ピクセル」なのでそのまま入る) |
| id の対応付け | Android は `AInputEvent` のポインタ ID が整数で来るが、**iOS の `UITouch*` はポインタ**。`UITouch*` を安定した `int32_t` へ写す表(スロット 10 個)を `iOSTouchBackend` が持つ。**`UITouch` を retain してはいけない**(Apple が明示的に禁止している)ので、**アドレスをキーに使うだけ**にする |
| `touchesCancelled` | Android の `ACTION_CANCEL` 相当。**忘れると指が張り付く**。該当スロットを未使用へ戻す |
| 1 フレーム内の押して離し | Android は `AndroidInputSink` が `pressedSinceFetch_` 相当の仕組みで取りこぼしを防いでいる。**iOS も `CADisplayLink` の間に began→ended が入りうる**ので同じ配慮が要る(`TouchPoint::pressed` の「離した瞬間のフレームだけ false で 1 回返る」契約に合わせる) |
| スレッド | `touches*` は main スレッド、`Poll` はフレーム駆動も main スレッド(`CADisplayLink`)なので、**Android と違ってロックが要らない**。要らないことをコメントに明記する |
| Apple Pencil / 3D Touch | 範囲外 |

**仮想パッドに描画が無い問題**(§0.6-(4))は iOS でも同じ。P3 で判断する。

### 5.3 ImGui(デバッグ UI) — 新規実装は不要になった

**初版の「720 行の `iOSImGui` を新規に書く」は撤回する。**
Android 移植が [Platform/Common/ImGuiPointerInput](../aqEngine/Platform/Common/ImGuiPointerInput.h)
(84 行)を入れ、**プラットフォームガードすら付けずに**「マウス抽象しか見ないので、
タッチでも物理マウスでも同じ経路で動く」形にした。iOS はこれをそのまま使う。

やることは 2 つだけ:

1. [Application.cpp](../aqEngine/Core/Application.cpp):169-179 の ImGui バックエンド選択に
   iOS 分岐を足す(`ImGuiPointerInput::Init` / `Shutdown` / `NewFrame` の 3 箇所)。
   Android と同じ実体なので `AQ_PLATFORM_ANDROID` を `AQ_PLATFORM_ANDROID || AQ_PLATFORM_IOS`
   へ広げるだけでよい。
2. 同 :158 の `MOBILE_UI_SCALE`(`ScaleAllSizes` + `FontGlobalScale` を 2.0 倍)を
   iOS でも効かせる。**指で触れる大きさにする措置で、iOS でも理由は同一**。

初版が置き換え対象として挙げていた `NSCursor` / `NSPasteboard` / Carbon キー表 /
スクロール / テキスト入力は、`ImGuiPointerInput` が**そもそも扱っていない**ので
論点ごと消える(`BackendFlags` を何も立てないので ImGui 側も期待しない)。

| 初版で書き直しが要るとした機能 | 現在 |
|---|---|
| マウス位置 / ボタン | `TouchMouseBackend` が 1 本目の指をカーソル・接触を左ボタンへ写し、`ImGuiPointerInput::NewFrame` がそれを ImGui へ流す。**ホバーが無い問題も、押下と同時に位置が入るので自然に解決している** |
| カーソル形状 / クリップボード / キー入力 / テキスト入力 / スクロール | **扱わない**(`ImGuiPointerInput` の明示的な設計判断) |
| `DisplaySize` / `DeltaTime` | `ImGuiPointerInput::NewFrame` が `Engine::GetScreenWidth/Height` と `GetDeltaTime` から埋める |
| `DisplayFramebufferScale` | §3.5 で `contentsScale = 1.0` を採るので (1,1) のまま整合 |

§0.5-6 は **(a) を採る前提で決着**とする(コストがほぼゼロになったため。無効化する理由がない)。

### 5.4 物理コントローラとハードウェアキーボード

- パッドは §5.1 のとおり無改修で動く見込み(実機のコントローラで P5 に確認)。
  `SetVibration` は **Mac でも未実装**なので iOS 固有の残タスクではない。
- ハードウェアキーボードは `UIPress` / `UIKey` で取れるが、**優先度を下げる**
  (iOS アプリとしてキーボード前提の操作設計にしないため)。
  キーボードは `NullKeyboardBackend` のまま(Android と同じ)。
  **マウスは `NullMouseBackend` ではなく `TouchMouseBackend`** を差す(§5.2)。
  Null を差すと UI と ImGui にポインタが 1 つも届かないため、P1〜P2 の足場としてのみ使う。

---

## 6. サウンド

一次資料は [Sound設計.md](Sound設計.md) と [Mac移植設計.md §5](Mac移植設計.md)。本節は差分のみ。

**実測で分かっていること**(§0.2): `SoftwareMixer` / `MixerSoundVoice` /
`ExtAudioFileDecoder.mm` は**無改修で iOS SDK を通る**。落ちるのは
[CoreAudioSoundBackend.mm](../aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.mm) 1 本だけ。

| 項目 | 変更 |
|---|---|
| include | `<CoreAudio/CoreAudio.h>` は **iOS に存在しない**(アンブレラヘッダが無い)。`<AudioToolbox/AudioToolbox.h>` + `<AVFAudio/AVAudioSession.h>` へ |
| 出力ユニット | `kAudioUnitSubType_DefaultOutput` → **`kAudioUnitSubType_RemoteIO`**。iOS SDK にはどちらの定数も宣言があるが、`DefaultOutput` は iOS では動かない。コード中の[コメント(:163-164)](../aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.mm)に既にこの旨が書かれている |
| **AVAudioSession(新規)** | カテゴリ(`AVAudioSessionCategoryAmbient` か `Playback`)の設定と `setActive:`。**iOS だけの必須手順**で、やらないと音が出ない/他アプリと競合する |
| **中断処理(新規)** | 電話着信・他アプリの再生で `AVAudioSessionInterruptionNotification` が来る。Began でユニット停止、Ended で再開。**やらないと復帰後に無音になる** |
| 前面/背面(**IF は導入済**) | `ISoundBackend::OnSuspend/OnResume` が Android 移植で入り、`Engine::SyncSoundActivity()` → `SoundEngine::OnSuspend/OnResume` → バックエンド、の経路が既に通っている(§0.6)。**`CoreAudioSoundBackend` にこの 2 つを実装するだけ**。Mac は既定の no-op のままでよい |
| デコーダ | `ExtAudioFileDecoder`(AudioToolbox)をそのまま。現在のアセットは **wav 5 本**なので `WavDecoder` で足りる |
| `SoundBackend.h` | `AQ_PLATFORM_IOS` を Mac と同じ `SOUND_BACKEND_COREAUDIO` 分岐へ(`AQ_PLATFORM_APPLE` を使えば分岐追加ゼロ。§2.1) |

バックグラウンド遷移時の扱いは §3.4 と連動(`OnSuspend` で停止)。

---

## 7. リソース / ファイル IO ★サンドボックスの壁

### 7.1 読み込み — Android 移植でほぼ解決済み。残るは Metal シェーダ 1 点 ★

**初版の「案 (a) `chdir` 方式【推奨】」は撤回する。**
初版が「本来やるべき掃除だが、Windows / Mac / UWP 全部の回帰確認を伴うので iOS 移植とは
分けて別途行う」とした**案 (b)(`GetContentRoot()` への統一)を、Android 移植が先に
やってしまった**(§0.6-(2))。iOS は `chdir` せずにその成果へ乗る。

現在の解決状況:

| 初版が挙げた系統 | 現在 | iOS での扱い |
|---|---|---|
| リソース一般(`BuildResourcePathCandidates`) | [Resource.cpp:256](../aqEngine/Resource/Resource.cpp) の `FindProjectRoot` が **`Engine::GetContentRoot()` を最優先**で見る(返れば CWD の上方探索をしない) | **無改修で通る** |
| **JSON**(Prefab / Level / UIDocument / AudioBank / Particle) | [SimpleJson.cpp:205](../aqEngine/Util/SimpleJson.cpp) が `ResolveExistingResourcePath()` を通す | 同上 |
| 画像 | [ImageLoader.cpp:109](../aqEngine/Resource/ImageLoader.cpp) が同上 | 同上 |
| サウンド | [WavDecoder.cpp:113](../aqEngine/Sound/Decoder/WavDecoder.cpp) / [WavStreamDecoder.cpp:57](../aqEngine/Sound/Decoder/WavStreamDecoder.cpp) が同上 | 同上 |
| シェーダ(Vulkan) | [VulkanShader.cpp:28](../aqEngine/Graphics/Vulkan/VulkanShader.cpp) の `FindProjectRoot` も同じ形へ揃った | iOS は Vulkan を使わない |
| **シェーダ(Metal)** | [MetalShader.mm:35](../aqEngine/Graphics/Metal/MetalShader.mm) / [MetalRenderContextImpl.mm:51](../aqEngine/Graphics/Metal/MetalRenderContextImpl.mm) は **今も CWD からの上方探索のみ** | **★これだけ iOS で直す(P2 必須)** |
| メッシュ(`.tkm` / `.obj` / `.pmd`) | `BuildResourcePathCandidates` 経由になっているか P2 で実測する | 未確認。実測して足りなければ同じ形へ寄せる |

**Metal の `FindProjectRoot` を直す(P2 の必須項目):**

`VulkanShader.cpp` に入った変更をそのまま写す。すなわち上方探索の**前**に

```objc
if (const char* contentRoot = aq::Engine::Get().GetContentRoot()) { return contentRoot; }
```

を置く。**注意点 2 つ:**

- **2 ファイルは互いの写しで、コード中に「片方だけ変えないこと」と明記されている。**
  必ず同時に直す。
- `VulkanShader.cpp` 側はこの変更と同時に **C++11 のスレッドセーフな static 初期化**へ
  作り直されている(「ワーカースレッドから並列に呼ばれるため」)。Metal 側も
  同じ理由が当てはまるなら合わせる。**ここは Mac にも効く変更**なので回帰確認に含める。

これで iOS の `PlatformiOS::GetContentRoot()` は
`[[NSBundle mainBundle] bundlePath]`(の C 文字列をメンバに保持したもの)を返すだけでよい。

### 7.2 書き込み

§3.6 のとおり **`IPlatform::GetUserDataDirectory()` は既に存在する**(純粋仮想)。
`PlatformiOS` が `NSDocumentDirectory` を返す実装を書けば済む。
バンドルが read-only なので**これは P2 の必須項目**(ログが黙って捨てられるだけならまだしも、
`imgui.ini` の保存失敗はデバッグ UI の状態が毎回リセットされる形で表面化する)。

付け替える先:

| 書き込み先 | 現在 | iOS |
|---|---|---|
| `startup_timing.log` | [aq.cpp:55 付近](../aqEngine/aq.cpp) が CWD へ `fopen`。UWP は `LocalState`、Android は `logcat` へ分岐済み | `GetUserDataDirectory()` 配下へ。**Android と同じく `#elif` を 1 本足す形**にする |
| `imgui.ini` | ImGui 既定(`io.IniFilename`)。エンジン側に上書きが**無い** | `io.IniFilename` を明示設定する |
| セーブ / ゴースト | 既に `Engine::GetUserDataDirectory()` 経由([AquaDashStates.cpp:243](../Game/Application/Flow/AquaDashStates.cpp)) | **無改修** |
| エディタ保存(Prefab / Level / UIDocument / TextStyle) | `SimpleJson` 経由 | iOS では保存 UI を出さない |

### 7.3 バンドルレイアウト

**iOS のバンドルは `Contents/` 階層を持たない。** リソースはバンドル直下に置かれる。

```
macOS:  Game.app/Contents/Resources/Game/Assets/...
iOS:    Game.app/Content/Game/Assets/...
```

> **`Game.app/Game/Assets/...` にはできない(P2 で実測)。** フラットバンドルでは
> **実行ファイル自身が `<Bundle>/Game`** なので、アセット側が要求する `Game/` と名前が衝突する。
> バンドル直下は `Info.plist` / `PkgInfo` / `_CodeSignature` も置かれる **OS 側の名前空間**でもある。
> よってこちらの持ち物は **`Content/` 1 段に隔離**し、`GetContentRoot()` は
> `<Bundle>/Content` を返す。

[package_app.cmake](../Tools/PackageApp/package_app.cmake) に iOS 分岐を足す:

- コピー先を `<Bundle>/Game/Assets` にする(`Game/` 1 段を再現するのは
  `BuildResourcePathCandidates` が `"Assets/…"` を `<root>/Game/Assets/…` に組むため。
  UWP の `install/Game/Assets/…`、Android の展開先と同じ理由)。
  したがって `GetContentRoot()` が返すのは **バンドル直下**であって `Game/` ではない。
- **Vulkan 分岐(`libMoltenVK.dylib` / ICD JSON / `install_name_tool`)は iOS では通らない**。
  §0.3 で Metal 一本にしたので分岐ごとスキップする。
- `.metal` / `.spv` は `Assets/Shader/msl-ios/` から取る(§4.2)。
- アセット 98MB。`.ipa` として問題になる量ではないが、`aqBundleApp` を ALL に入れない
  現行方針(毎ビルド 98MB コピーを避ける)は iOS でも維持する。
- **iOS では `CODE_SIGNING` の後にバンドルを書き換えると署名が壊れる。**
  `aqBundleApp` を署名の**前**に走らせる順序が必要(Xcode ジェネレータでは
  ビルドフェーズの順序に注意)。

---

## 8. オープン課題

1. **実機の `supportsBCTextureCompression`**(§4.3)。シミュレータは `NO` と実測したが、
   実機の値は端末が入手できるまで不明。`YES` の端末に限定できるなら案 (a) の展開経路は
   起動時に選ばれないだけで済む。
2. ~~`FindProjectRoot` の 6 重複と CWD 依存の掃除~~ — **Android 移植で大半が解決した**(§0.6-(2))。
   残るのは **Metal の 2 本**(§7.1)で、これは本移植の P2 でやる。
   D3D11 / D3D12 の 2 本は Windows 専用なので手を付けない。
3. **`AQ_PLATFORM_APPLE` の導入範囲**(§2.1)。既存 `AQ_PLATFORM_MAC` の分岐を
   1 箇所ずつ「Mac 専用(AppKit / デスクトップ前提)」か「Apple 共通」かで仕分ける。P0 の主作業。
   Android が `AQ_PLATFORM_DESKTOP` に入らない前例を作っているので、
   **iOS も `DESKTOP` には入れない**。
4. **メモリ予算の具体値**(§3.6 / §4.4)。実機の機種が決まるまで暫定値。
   Android は 2GB を「強制ではない設計目標」として置いた([PlatformBudget.h](../aqEngine/Platform/PlatformBudget.h))ので、
   **iOS も同じ流儀で暫定値を置く**。シャドウ 2048²×4 = 64MB をどこまで落とすかは
   実機の見た目と併せて判断。
5. ~~ImGui をどこまでやるか~~ — **`ImGuiPointerInput` の再利用で決着**(§5.3)。
6. **Windows / Mac の回帰**。本移植が共通コードへ入れるのは以下。どれも既定実装または
   同値で挙動不変になる設計だが、確認は要る:
   - `IPlatform::RunFrameLoop`(既定実装 = 現行 while ループ)と `Engine::FrameStep` の切り出し — §3.3
   - **Metal の `GetSurfaceSize` 実装** — **Mac の `screenWidth_/screenHeight_` が
     drawableSize に追従するようになる**。§4.5 の ⚠ を参照
   - **Metal シェーダの `FindProjectRoot`** — Mac も同じコードを通る。§7.1
   - `ImGuiPointerInput` / `MOBILE_UI_SCALE` の `#if` 拡張 — Mac は `MacImGui` 経路のままなので
     影響しないはずだが確認する
   - `AQ_PLATFORM_APPLE` の導入に伴う既存 `AQ_PLATFORM_MAC` の書き換え — **仕分けを誤ると
     Mac が壊れる。ここが本移植で Mac を壊す最大の経路**
7. ~~`ITouchBackend` の導入フェーズ~~ — **Android が先に入れた。iOS は既存定義に従う**(§0.6 / §5.2)。
8. **仮想パッドの描画**(§0.6-(4))。Android から引き継ぐ未解決。`UIScreenManager` の上に置くか
   デバッグ UI と同じ層に置くかは、Android / iOS のどちらか先に着手した側で決める。
9. **Metal の `.metallib` 事前ビルド**(§4.6)。iOS では Xcode に Metal Toolchain が
   同梱されるので可能になる。P6 の候補。
10. **シミュレータでだけ通っている劣化経路の実機確認**(§4.7)。ボーダーカラー・
    read-write テクスチャ(compute / ポストプロセス)・BC 圧縮の 3 つは、
    実機(Apple7 以降 / iOS 16.4 以降)では本来の経路を通るはず。**P5 で必ず確認する。**
    とくに **compute が有効になるとポストプロセス一式が初めて iOS で走る**ので、
    そこで新たな問題が出る可能性がある。
11. **`LoadFromDDSFile` / `LoadFromTGAFile` の失敗が無言**。WIC / stb_image 経路は
    失敗ログを出すのに DDS / TGA だけ出さない。P2 の「キャラクタが灰色」の切り分けに
    時間を要した直接の原因。`<Engine>` の小改善として切る。
12. **アセットの拡張子の大小が揃っていない**(`utc_all2.DDS` と `Terrain/grass.DDS` は大文字、
    tkm が要求するのは小文字)。P2 で解決経路を大小両対応にしたので実害は無くなったが、
    **アセット側を小文字へ揃えるのが本来**。移植とは別の片付けとして残す。

---

## 9. フェーズ計画

各フェーズは「実装内容 + 評価チェックリスト」で完結させ、1 フェーズ = 1 コミットとする。
**§0.6 の再評価により、初版に比べて P3(入力)と P4(サウンド)が大きく縮んだ。**

### P0: ビルド基盤(iOS 実機・シミュレータ不要)

`AQ_PLATFORM_IOS` / `AQ_PLATFORM_APPLE` の新設と CMake の分岐。**リンクが通るところまで。**
プラットフォーム実装は骨格(`CreateMainWindow` が false を返す程度)で構わない。

- [PlatformDefs.h](../aqEngine/Platform/Common/PlatformDefs.h) に `AQ_PLATFORM_IOS`
  (`TARGET_OS_IPHONE` を `__APPLE__` より先に見る)と `AQ_PLATFORM_APPLE`。
  「ちょうど 1 つだけ定義」の静的検査に加える。**`AQ_PLATFORM_DESKTOP` には入れない**
  (Android の前例に倣う)
- 既存 `AQ_PLATFORM_MAC` の分岐を 1 箇所ずつ仕分け(§2.1)。**機械置換しない**
- [AqCommon.cmake](../cmake/AqCommon.cmake) の `IOS` 分岐(**`APPLE` より先に書く**)、
  [aqEngine/CMakeLists.txt](../aqEngine/CMakeLists.txt) のソース除外を 3 → 4 分岐化。
  Android が導入した `AQ_ENGINE_*_ONLY_PATTERNS` の名前付き除外集合に
  `AQ_ENGINE_IOS_ONLY_PATTERNS` を足す形にする(`Platform/Mac/` と
  `Sound/CoreAudio/CoreAudioSoundBackend.mm` を落とし、`HID/Mac/`・`Graphics/Metal/`・
  `ExtAudioFileDecoder` は**残す**)
- フレームワークを `Cocoa` → `UIKit`(他 6 つは共通)
- ルート [CMakeLists.txt](../CMakeLists.txt) の API 検証:
  `Metal は macOS 専用です` を iOS でも許すよう緩め、**iOS では Metal のみ**に制限
  (`ANDROID AND NOT Vulkan` の既存分岐と同じ形で書く)
- `Platform/iOS/` の骨格、`Game/Application/iOSMain.mm`(空 TU でよい)、
  `DebugOutputMac.cpp` → `DebugOutputApple.cpp` への改名
- [CMakePresets.json](../CMakePresets.json) に `ios-xcode` / `ios-simulator-xcode` / `ios-ninja`

**評価チェックリスト** — 2026-09-14 実施

- [x] `cmake --preset ios-simulator-xcode` が configure できる
- [x] `Game.app`(シミュレータ / arm64)が**リンクまで**通る
      (`platform IOSSIMULATOR` / `minos 16.4` / `sdk 26.5` を `vtool -show-build` で確認)
- [x] `Game.app`(実機 / arm64、署名なし)が**コンパイルまで**通る(`platform IOS` を確認)
- [x] **Mac 2 構成(Vulkan / Metal)の Release がビルドでき、実行してタイトル画面が出る**
      (Metal 構成でステージ走行まで確認。`AQ_PLATFORM_APPLE` の仕分けミスは出ていない)
- [ ] **Windows 3 構成(D3D11/D3D12/Vulkan)の Debug が従来どおりビルドできる — 未確認。**
      この環境では Windows をビルドできない。次に Windows を触るときに要確認。
      変更が Windows に及ぶのは次の 5 点で、いずれも**到達する分岐が変わらない**ことを
      コードで確認済み:
      (1) `PlatformDefs.h` — `<TargetConditionals.h>` の include は `__APPLE__` ガード付き。
          推定チェーンは `__ANDROID__` → `_WIN32` → `TARGET_OS_IPHONE` → `__APPLE__` の順で
          Windows の到達先は不変
      (2) `AqCommon.cmake` / `aqEngine/CMakeLists.txt` / ルート `CMakeLists.txt` —
          `IOS` は Windows の configure で常に偽。Windows が除外するファイル集合は
          `DebugOutputMac.cpp` → `DebugOutputApple.cpp` の改名を反映しただけで同一
      (3) `PlatformBudget.h` — `#elif` を 1 本挟んだだけで既存 4 プロファイルは不変
      (4) バックエンド選択ヘッダ 4 本 — `#elif defined(AQ_PLATFORM_IOS)` の追加のみ
      (5) `aq.h` の Metal ガード — `AQ_PLATFORM_MAC` ⊂ `AQ_PLATFORM_APPLE` なので Windows では同値
- [x] 警告が Mac 構成で増えていない(Metal Release のフルリビルドで **14 件 / P0 前と同一箇所**)

**P0 で分かったこと**

- `Tools/MixerTest` が iOS でも `MixerTest.app` として作られてしまうため、
  Android と同じ理由(端末上で直接叩けない)で `if(NOT ANDROID AND NOT IOS)` へ変えた。
- 除外パターンの集合を組み替えた(`AQ_ENGINE_APPLE_ONLY_PATTERNS` /
  `AQ_ENGINE_MAC_ONLY_PATTERNS` / `AQ_ENGINE_IOS_ONLY_PATTERNS`)。
  iOS は `\.mm$` の一括除外に掛けられない(`Graphics/Metal/` と `ExtAudioFileDecoder.mm` を
  使うため)のが理由。`AQ_ENGINE_MAC_ONLY_PATTERNS` は**「iOS からだけ落とす macOS 専用」**へ
  意味が変わっている点に注意。
- `Core/Application.cpp` の ImGui 分岐と `MOBILE_UI_SCALE` は **P3 へ送った**。
  iOS は現状 `#else`(UWP と同じ「プラットフォームバックエンド無し」)に落ちるが、
  P0 では実行しないので問題にならない。

### P1: シミュレータでクリア画面

UIKit エントリ + `CAMetalLayer` + フレーム駆動の委譲。

> **P1 では Engine を起動しない。**[Android移植設計.md の P1](Android移植設計.md) と同じ段取りを採る。
> `Engine::Initialize` はシェーダとテクスチャの読み込みを伴うが、アセットをバンドルへ入れるのは
> P2(`package_app.cmake` の iOS 分岐)なので、それまでゲーム本体は動かせない。
> P1 は**グラフィクスデバイスだけを立ててクリア色を提示**し、検証対象を
> 「UIKit → `CAMetalLayer` → Metal のドローアブル/提示」と「`CADisplayLink` によるフレーム駆動」に絞る。
> この足場は P2 で `Engine::Create → Initialize → RunGame → Finalize` のブートへ置き換える。
>
> **アセットが 1 つも要らないことは確認済み**: `MetalGraphicsDeviceImpl::Initialize` は
> ファイルを一切読まず、`CopyToBackBuffer` のフルスクリーン変換描画に使う MSL も
> `.mm` に文字列で埋め込まれていて `newLibraryWithSource:` でコンパイルされる。

- `iOSMain.mm`(`UIApplicationMain`)と `AqAppDelegate`(§3.2)。
  **`ShutdownMemory()` は `applicationWillTerminate:` に置く**(§0.6-(3))
- `PlatformiOS`(`UIWindow` / `UIViewController` / `AqMetalView` / `+layerClass`)。
  `GetContentRoot` はバンドルパス、`GetUserDataDirectory` は `NSDocumentDirectory`
- **`IPlatform::RunFrameLoop` の追加**(既定実装 = 現行 while ループ)と
  **`Engine::FrameStep` の切り出し**、`CADisplayLink`(§3.3)。
  P1 の足場も `Engine` と同じく `RunFrameLoop` 経由でフレームを回し、**P2 で本物に
  差し替わる機構をそのまま検証する**
- 機能クエリの起動ログ(GPU 名 / `supportsBCTextureCompression` / GPU family。§4.3)
- Info.plist(`UILaunchScreen` / 向き / `UIRequiredDeviceCapabilities` / `CFBundleIdentifier`)

> **`--msl-ios` は P2 へ送った**(初版では P1 に置いていた)。P1 はアセットを
> バンドルへ入れないため、生成しても**読めているかを確認する手段が無い**。
> 「各フェーズのチェックリストが確認可能な粒度になっている」(§10)を優先した。

**評価チェックリスト** — 2026-09-14 実施

- [x] シミュレータにインストールでき、起動する(iPhone 17 / iOS 26.5)
- [x] Metal のクリア色が画面に出る(`simctl io screenshot` で全面に指定色を確認)
- [x] `CADisplayLink` でフレームが回っている
      (`presented frame 1/2/3` が **16.2〜16.5 ms 間隔 = 60 FPS**)
- [x] `RunFrameLoop` の委譲が効いている(`UIApplicationMain` が戻らない構造で
      `[ios] CADisplayLink started` の後もフレームが回り続ける)
- [~] **`applicationWillTerminate:` で後始末まで進む — 検証できなかった。**
      `xcrun simctl terminate` では**このコールバックが呼ばれない**。詳細は下記「分かったこと」3
- [x] 起動ログに GPU 名 / `supportsBCTextureCompression` / GPU family が出る(§4.3)。
      **シミュレータ: `Apple iOS simulator GPU` / BC: no / `Apple2` / unified: no**
      (§0.2-3 の事前実測と一致。**P2 の BC 展開経路が必要であることが確定した**)
- [x] **Mac の回帰**: Metal / Vulkan とも Release がビルドでき(警告 14 / 17 件で P1 前と同数)、
      Metal 構成を実行してステージ走行まで確認(60.1 FPS)。
      機能クエリのログは Mac では `Apple M5` / BC: yes / `Apple9` / unified: yes
- [ ] **Windows 3 構成の回帰 — 未確認**(この環境ではビルドできない)。
      共通コードへの変更は `IPlatform::RunFrameLoop` の追加(既定実装 = 従来の while ループ)と
      `Engine::FrameStep` の切り出し(純粋なリファクタ。`continue` が `return` になっただけ)、
      および Metal のログ 2 行(Windows には届かない)のみ

**P1 で分かったこと / 設計からの差分**

1. **`StartupMark` が iOS では何も出していなかった。**Android P1 とまったく同じ罠。
   出力先が「CWD の `startup_timing.log`」と `OutputDebugStringA` だけで、iOS は CWD が
   `/`(read-only)なので `fopen` が黙って失敗し、**起動の到達点が一切見えない**。
   `aq.cpp` に `AQ_PLATFORM_IOS` の分岐を足し、`aq::debug::OutputString`(= stderr)へ流した。
   `xcrun simctl launch --console-pty` で読める。**これが無いと P1 の評価が 1 つも進まない。**
2. **Info.plist テンプレートの落とし穴が 2 つ。**
   - **コメント中に `${...}` の形を書くと `configure_file` が展開しようとして
     configure ごと失敗する**(`Invalid character ('*') in a variable name`)。
   - **Xcode は `TARGETED_DEVICE_FAMILY`(既定 `1` = iPhone のみ)から `UIDeviceFamily` を
     上書きする。**テンプレートに `1,2` と書いても効かないので、
     `XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"` を明示した。
3. **`applicationWillTerminate:` は当てにできない。**`simctl terminate` では呼ばれず、
   iOS 全般としてもアプリは「サスペンド → 予告なく kill」が通常で、このコールバックが
   走る保証がない。**つまり iOS では Win32 / Mac / Android と違い、終了時の後始末と
   リーク報告が実行されない前提で設計する必要がある。**
   `Engine::Finalize` / `ShutdownMemory` の置き場所としては他に選択肢が無いので構造は
   このままにするが、**リーク検出を iOS の品質ゲートに使わない**こと。P5 で改めて扱う。
4. **向きの固定は効いている。**ウィンドウが **874x402(横向き)**で作られた。
   デバイス画面のスクリーンショットは縦(1206x2622)のままなので、
   **単色のクリアだけでは向きが判定できない**点に注意(ログの寸法で判断すること)。
5. **P1 はアセットを 1 つも要求しなかった。**`MetalGraphicsDeviceImpl::Initialize` は
   ファイルを読まず、`CopyToBackBuffer` のフルスクリーン変換描画に使う MSL も
   `.mm` に埋め込まれた文字列を `newLibraryWithSource:` でコンパイルしている。
   Android P1 と同じ段取りがそのまま成立した。

### P2: シミュレータで実シーン

アセットとシェーダをバンドルから読ませる。**BC 展開と Metal のパス解決がここの山場。**

- `package_app.cmake` の iOS 分岐(`<Bundle>/Game/Assets` レイアウト。§7.3)。
  **Vulkan 分岐はスキップ**。**署名の前に走らせる**
- **`--msl-ios` の生成ターゲットと `msl-ios/` 読み出し分岐**(§4.2。P1 から移動)
- P1 の足場(クリア画面)を `Engine::Create → Initialize → RunGame → Finalize` のブートへ置き換える
- **Metal シェーダの `FindProjectRoot` を `GetContentRoot()` 優先へ**(§7.1)。
  `MetalShader.mm` と `MetalRenderContextImpl.mm` を**同時に**直す
- **`Metal の GetSurfaceSize` 実装**(§4.5)
- `startup_timing.log` / `io.IniFilename` を `GetUserDataDirectory()` 配下へ(§7.2)
- **BC 非対応時の `DirectX::Decompress` 経路**(§4.3 案 a)
- エディタ保存 UI の無効化

**評価チェックリスト** — 2026-09-14 実施

- [x] タイトル画面が表示される(ロゴ / 背景 / サムネ / フォント / ImGui すべて)
- [x] ステージが描画される(路面・キャラ・地形・草・空・HUD・ミニマップ)。
      **入力がまだ無いため、タイトルからの遷移は一時パッチで自動化して確認した**
      (パッチはコミットしていない。通常の遷移確認は P3)
- [x] **BC テクスチャが正しく表示される**(展開経路が効いている)。
      キャラクタが Mac と同じテクスチャ付きで出る
- [x] `startup_timing.log` が書き込み可能領域(`Documents/`)に出る。`imgui.ini` も同様
- [x] Metal の Validation を有効にしてエラー 0
- [x] メモリ使用量を計測(**RSS 322 MB / Debug**)
- [x] **Mac の回帰**: Metal / Vulkan とも Release がビルドでき(警告 14 / 17 件で従来と同数)、
      **生バイナリでタイトル〜ステージ走行(59.1 FPS / コイン取得)**、
      **`aqBundleApp` 済みの `.app` を CWD 外から起動して 60.0 FPS** の両方を確認
- [x] iOS 実機構成(`ios-xcode`)のコンパイルが通る
- [ ] **Windows 3 構成の回帰 — 未確認**(この環境ではビルドできない)。
      共通コードへの変更は `Resource.cpp` の拡張子ケース候補追加(既存候補の**後**に足すだけ)、
      `GraphicsDevice` の BC フラグ(既定 `true` で従来と同値)、`ImageLoader` の展開
      (BC 対応環境では素通り)、`aq.cpp` / `Application.cpp` の iOS 専用分岐

**P2 で分かったこと / 設計からの差分** — **実行するまで分からなかったものが 4 件**

1. **★ フラットバンドルでは実行ファイル自身が `<Bundle>/Game` で、アセットの `Game/` と衝突する。**
   §7.3 は「`iOS: Game.app/Game/Assets/...`」と書いていたが、**この配置は成立しない**
   (`file(MAKE_DIRECTORY)` が `File exists` で落ちる)。バンドル直下は
   `Info.plist` / `PkgInfo` / `_CodeSignature` / 実行ファイルという **OS 側の名前空間**でもある。
   → **コンテンツを `Content/` 1 段に隔離**した。`GetContentRoot()` は
   `<Bundle>/Content` を返し、アセットは `<Bundle>/Content/Game/Assets/...` に入る。
2. **★ 大文字小文字を区別するファイルシステムでテクスチャが開けない。**
   tkm のマテリアルは参照テクスチャの拡張子を小文字 `.dds` へ機械的に置換する
   (`ReplaceExtension(..., ".dds")`)が、同梱アセットの実体は `utc_all2.DDS` と大文字。
   **Windows / macOS のボリュームは既定で区別しないため今まで表面化しなかった**が、
   **iOS(シミュレータ・実機とも)は区別する**ので、モデルだけ出てキャラクタが灰色になった。
   → `BuildResourcePathCandidates` に**拡張子の大小を入れ替えた候補を(完全一致の後に)足す**
   `PushExtensionCaseVariants` を入れた。絶対パスの早期 return 経路にも適用している
   (tkm のマテリアルは解決済み絶対パスを基点に組み立てられるため)。
   **Android の内部ストレージ(ext4)も区別するので、同じ不具合が潜在していたはず**。
3. **★ シミュレータ GPU はサンプラのボーダーカラーに非対応。**
   `MTLSamplerBorderColorOpaqueWhite is not supported on this device` で
   **Validation がアサートして即死**する。ボーダーカラーと `ClampToBorderColor` は
   **GPU family Apple7 / Mac2 以上**が要る(シミュレータは Apple2)。
   → `metal::IsSamplerBorderColorSupported()` を新設し、非対応なら
   `ClampToEdge` へ落とす(シャドウマップの外側の 1 テクセルが伸びるだけ)。
4. **★ シミュレータは read-write テクスチャに非対応で compute が通らない。**
   `Shader uses texture(...) as read-write, but hardware does not support
   read-write texture of this pixel format.` でアサート。ポストプロセスの compute は
   HDR(RGBA16Float)の RT を読み書きするので **`MTLReadWriteTextureTier2`** が要るが、
   シミュレータは **Tier 0(非対応)**。
   → tier を実測して足りなければ `SetComputeSupported(false)` にし、
   **ポストプロセス無しの経路へ落とす**(トーンマップが掛からない)。
   **実機(Apple7 以降)では Tier2 のはずなので、この分岐は P5 で実機確認する。**

その他:

- **BC 圧縮なのは `utc_*.DDS` の 3 枚だけだった。** §4.3 は「DDS アセットは 7 枚」と
  書いていたが、`Sky/SkyCube.dds` と `Terrain/*.DDS` は**非圧縮**(`B8G8R8A8` / `R8G8B8A8`)。
  BC 展開が効くのはキャラクタの 3 枚。
- **`LoadFromDDSFile` / `LoadFromTGAFile` は失敗してもログを出さない**(WIC / stb_image 経路と
  違う)。上記 2 の切り分けに時間が掛かった原因。**改善候補**として残す。
- Info.plist テンプレートのコメントに `${...}` の形を書くと `configure_file` が落ちる(P1 で既出)。

### P3: 入力

**新規は `iOSTouchBackend` 1 本だけ。**残りは Android が入れた共通実装へ配線する(§5.2)。

- `iOSTouchBackend`(`UITouch*` → スロット 10 個の `int32_t` 写像。**retain しない**)
- `TouchBackend.h` / `PadBackend.h` / `KeyboardMouseBackend.h` の iOS 分岐(§5.2 の表)。
  パッドは `CompositePadBackend`(`GameControllerPadBackend` + `VirtualPadBackend`)
- `Application.cpp` の ImGui 分岐と `MOBILE_UI_SCALE` を iOS へ広げる(§5.3)
- `HID/Mac/` → `HID/Apple/` への移動と改名(§5.1)
- `touchesCancelled` での全解放

**評価チェックリスト**
- [ ] タッチでタイトルからステージへ進める
- [ ] 仮想パッドでキャラが操作できる
- [ ] ホームに戻る等で指が張り付かない(`touchesCancelled`)
- [ ] 1 フレーム内の「押して離し」を取りこぼさない
- [ ] デバッグ UI が指で操作できる(×2.0 拡大が効いている)
- [ ] 仮想パッドを操作中に裏の UI を誤タップしない(`IsTouchConsumed` の調停)
- [ ] **Mac の回帰**(`HID/Mac/` の改名と ImGui 分岐の書き換え)

### P4: サウンド

- `CoreAudioSoundBackend.mm` の include(`<CoreAudio/CoreAudio.h>` → AudioToolbox + AVFAudio)と
  `kAudioUnitSubType_DefaultOutput` → **`kAudioUnitSubType_RemoteIO`**
- **AVAudioSession のカテゴリ設定と中断処理**(§6)を
  **`ISoundBackend::OnSuspend` / `OnResume` の実装として書く**(呼び出し経路は
  `Engine::SyncSoundActivity` → `SoundEngine` として既に通っている。§0.6)
- `SoundBackend.h` の分岐(`AQ_PLATFORM_APPLE` を使えば追加ゼロ)

**評価チェックリスト**
- [ ] BGM / SE がシミュレータで鳴る
- [ ] 3D 音響の定位が Mac と一致する
- [ ] 他アプリの再生や着信で中断 → 復帰しても音が壊れない・二重再生しない
- [ ] バックグラウンド → 復帰で音が戻る(`IsRenderable` が false を返せていること)
- [ ] **Mac の回帰**(`CoreAudioSoundBackend.mm` は Mac と共有)

### P5: ライフサイクルと実機

**ここで初めて実機が必要**(§0.5-2)。

- `OnSuspend` / `OnResume` と `CADisplayLink` の停止/再開(§3.4)
- 署名・プロビジョニングの設定(§2.3)
- 実機での通しプレイ

**評価チェックリスト**
- [ ] ホームに戻る → 復帰、を 10 回繰り返して落ちない
- [ ] バックグラウンド中に Metal のコマンドを出していない
- [ ] 実機にインストールでき、タイトルからステージクリアまで通しで動く
- [ ] 実機の `supportsBCTextureCompression` を記録し §4.3 / §8-1 を更新した
- [ ] 実機でパッド(物理コントローラ)が使える
- [ ] 終了時に `MemoryTracker: No leaks detected`(`ShutdownMemory` の位置が正しい)

### P6: 性能とパッケージング

- 解像度スケール(§0.5-4)、シャドウ解像度、`MTLStorageModeMemoryless`
- `framebufferOnly = YES` への復帰(§4.1)
- `.metallib` の事前ビルド(§4.6)
- `PlatformBudget` の iOS プロファイルへ実測値反映、ThreadPool のコア割り当て
- Release ビルドと `.ipa`

**評価チェックリスト**
- [ ] 目標フレームレートを実機で達成(目標値は着手時に決める)
- [ ] サーマルスロットリング後も破綻しない
- [ ] メモリ使用量が iOS のプロファイル内に収まる
- [ ] 起動時間を計測し、`.metallib` 事前ビルドの効果を記録した
- [ ] Release の `.ipa` が署名付きでインストールでき動作する

---

## 10. チェックポイント(設計全体)

- [ ] §0.5 の未決事項のうち**残る 4 点(#1 BC / #2 検証環境 / #4 解像度 / #5 向き)**に
      ユーザーの判断を得た(#3 と #6 は §0.6 で決着)
- [ ] `AQ_PLATFORM_IOS` が「ちょうど 1 つだけ定義」の制約を壊していない
- [ ] `AQ_PLATFORM_APPLE` の導入で `AQ_PLATFORM_MAC` の分岐が**増えて**いない
- [ ] iOS 固有の型(`UIView` / `UIWindow` / `UITouch` / `CADisplayLink`)が
      `Platform/iOS/` の外に現れない(Mac が `Platform/Mac/` に閉じているのと同じ)
- [ ] **共通コードへ入れる追加 IF は `IPlatform::RunFrameLoop` の 1 本だけ**で、
      既定実装により Windows / Mac / UWP / Android が挙動不変として成立する
- [ ] **Android が入れた共通実装を作り直していない**
      (`ITouchBackend` / `VirtualPadBackend` / `CompositePadBackend` /
      `TouchMouseBackend` / `ImGuiPointerInput` / `GetUserDataDirectory` /
      `IsRenderable` / `RecreateSurface`)
- [ ] iOS 対応のために Windows / Mac / Android の挙動を変えていない
      (Metal 共通の `GetSurfaceSize` と `FindProjectRoot` は Mac に効くので明示的に確認した)
- [ ] タッチ抽象を [Android移植設計.md](Android移植設計.md) と二重に定義していない
- [ ] サウンド / 入力 / Metal の内容を [Sound設計.md](Sound設計.md) /
      [02_HID設計.md](02_HID設計.md) / [MetalBackend設計.md](MetalBackend設計.md) と二重に書いていない
- [ ] 各フェーズのチェックリストがシミュレータまたは実機で確認可能な粒度になっている
- [ ] `.mm` の MRR(非 ARC)前提を iOS のコードでも守っている(各 `.mm` 冒頭の
      `#if __has_feature(objc_arc) #error`)

---

## 参考

- 本書の実測データ(§0.2)の取得方法
  - 構文チェック: `CMAKE_EXPORT_COMPILE_COMMANDS` で得た `compile_commands.json` の
    コンパイル引数から PCH 指定を外し、`-fsyntax-only` を付けて 195 TU を個別に実行
  - Metal の機能実測: シミュレータ向けに `MTLCreateSystemDefaultDevice` を叩く小さな
    実行ファイルを作り `xcrun simctl spawn` で実行
- [Metal feature set tables(Apple)](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)
  — GPU family ごとの対応表。BC 対応の端末判定に使う
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) — §0.3 で不採用とした経路。
  導入済 Vulkan SDK に `ios-arm64` スライスがある
