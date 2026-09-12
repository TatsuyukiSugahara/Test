# iOS 移植 設計

> 対象コミット: 17bdb6d / 最終更新: 2026-09-12

対象: `aqEngine/` + `Game/`。既存の **Metal バックエンド**を iOS で動かし、実機(または
シミュレータ)で AquaDash が起動〜プレイできる状態までを設計する。

姉妹文書:
- [Mac移植設計.md](Mac移植設計.md) / [Mac移植調査.md](Mac移植調査.md) — 非 Windows 化の土台。
  CMake・`AQ_PLATFORM_*`・`IPlatform`・入力/サウンド抽象・事前シェーダ生成は**すべてここで導入済**。
- [MetalBackend設計.md](MetalBackend設計.md) — Metal バックエンドの一次資料。本書はその iOS 差分だけを書く。
- [Android移植設計.md](Android移植設計.md) — **タッチ抽象(§5.2)とライフサイクル IF(§3.3)の一次資料**。
  同じ設計を二重に書かないため、本書はリンクで参照して iOS 実装の差分のみ記述する。
- [Sound設計.md](Sound設計.md) / [02_HID設計.md](02_HID設計.md) — サウンド・入力抽象の一次資料。

**本書のステータス: 設計フェーズ。ユーザー未承認・実装未着手。** §0.5 の未決事項が
決まるまで P0 に着手しない。

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
     (Mac は `chdir` で凌いでいる) — §7
- **タッチ入力とライフサイクルは Android 移植と同じ課題**。抽象(`ITouchBackend` /
  `IsRenderable`)は 1 つだけ定義し、両移植で共有する(§5.2 / §3.4)。

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
| `Sound/Decoder/ExtAudioFileDecoder.mm` / `Sound/CoreAudio/CoreAudioSoundVoice.cpp` | AudioToolbox 経路はそのまま使える |

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

### 0.5 未決事項(着手前にユーザー判断が要る)

| # | 論点 | 選択肢 | 影響 |
|---|---|---|---|
| 1 | **BC 圧縮テクスチャ**(§4.3) | (a) **実行時に BC を展開**して RGBA8 で使う【推奨】 / (b) アセットを ASTC へ再エンコード / (c) BC 対応端末(`supportsBCTextureCompression == YES`)に限定 | (a) は同梱済 DirectXTex の BC ソフトコーデックで完結しアセット無改変。VRAM は増える。(c) はシミュレータで動かせなくなる |
| 2 | **検証環境**(実機 / アカウント) | (a) **シミュレータで P1〜P4 まで進め、実機は P5**【推奨】 / (b) 最初から実機 | 手元に iOS 実機は無い(§0.2-4)。実機には Apple ID(無料なら 7 日間のプロビジョニング)以上が必要。性能・サーマル・BC 対応の判定は**実機でしかできない** |
| 3 | **タッチ操作の方針** | (a) **オンスクリーン仮想パッド**(`IPadBackend` として供給)【推奨】 / (b) 物理コントローラ必須 / (c) タッチ専用操作を新規設計 | [Android移植設計.md §5.2](Android移植設計.md) と**同一の論点**。ここで決めた方式は両移植で共有する |
| 4 | **解像度スケール** | (a) **論理解像度(`contentsScale = 1.0`)で開始し P6 で調整**【推奨】 / (b) 最初からネイティブ解像度 | (b) は 3× で G-Buffer とシャドウが 9 倍になり、§4.4 の見積り 130〜140MB が破綻する |
| 5 | **画面の向き** | (a) **横向き固定**【推奨】 / (b) 回転対応 | (b) は drawableSize 変化への追従が必要だが、**Metal バックエンドにリサイズ経路が存在しない**(§4.5)。(a) なら本移植の範囲外にできる |
| 6 | **デバッグ UI(ImGui)** | (a) **P3 で最小限のタッチ対応**【推奨】 / (b) iOS では無効化(`AQ_IMGUI` を切る) | [MacImGui.mm](../aqEngine/Platform/Mac/MacImGui.mm) は 720 行で、`NSCursor` / `NSPasteboard` / Carbon キー表が置き換え対象。指での ImGui 操作は本質的に厳しい |

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
| サウンド | `SoftwareMixer` / `CoreAudioSoundVoice` / `ExtAudioFileDecoder` | **無改修(実測)**。出力ユニットだけ差し替え(§6) |
| シェーダ生成 | [Tools/ShaderCompile/compile_msl.cmake](../Tools/ShaderCompile/compile_msl.cmake) | `--msl-ios` と出力先の追加のみ。`AQ_MSL_OUT_DIR` は既にパラメータ化済(§4.2) |
| 数学 / 画像 / 物理 | ThirdParty 同梱の DirectXMath・DirectXTex(非 Windows 経路)・WinCompat・stb_image・Bullet | **実測で全 TU 通過**。ARM64 なので Mac と同条件 |
| ログ出力 | [Platform/Common/DebugOutputMac.cpp](../aqEngine/Platform/Common/DebugOutputMac.cpp) | `fputs(stderr)` なので iOS でもそのまま動く。**Mac/iOS 共通として改名する**(§3.1) |

Mac 固有 `#ifdef` はリポジトリ全体で 49 箇所(Mac 移植設計 §1.1 の計測)。**iOS はその大半を
Mac と共有できる**ため、`AQ_PLATFORM_APPLE` の導入で分岐の増加を抑えられる(§2.1)。

### 1.2 iOS で新たに要るもの(概観)

| 領域 | 新規 | 既存改修 |
|---|---|---|
| ビルド | `ios-*` プリセット、Info.plist、署名設定 | `PlatformDefs.h`、`AqCommon.cmake`、`aqEngine/CMakeLists.txt` の分岐、ルート `CMakeLists.txt` の API 検証 |
| プラットフォーム | `Platform/iOS/`(3〜4 ファイル)、`Game/Application/iOSMain.mm` | **`IPlatform` にフレーム駆動の委譲と書き込み先**(§3.3 / §3.6) |
| グラフィックス | なし | MSL の `--msl-ios` 再生成、**BC 非対応時の展開経路**、機能クエリ |
| 入力 | `ITouchBackend` + iOS 実装、`iOSImGui` | `KeyboardMouseBackend.h` / `PadBackend.h` の分岐 1 つ |
| サウンド | なし(出力ユニットの差し替えのみ) | `CoreAudioSoundBackend.mm` の include と subtype、AVAudioSession |
| リソース | なし | 書き込み先の付け替え、バンドルレイアウト、`package_app.cmake` の iOS 分岐 |

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
| `aqEngine/Platform/iOS/PlatformiOS.h` / `.mm` | `IPlatform` 実装。`CreateMainWindow` で `UIWindow` + ルート `UIViewController` + `AqMetalView` を生成し `CAMetalLayer*` を返す。`PumpEvents` は**終了フラグを見るだけ**(イベント配送は UIKit が行う)。`RunFrameLoop` で `CADisplayLink` を張る。`GetContentRoot` / `GetWritableRoot`。ObjC 型はヘッダに漏らさない(`iOSWindowObjects*` の前方宣言のみ。[PlatformMac.h:12-14](../aqEngine/Platform/Mac/PlatformMac.h) と同じ作法) |
| `aqEngine/Platform/iOS/AqMetalViewIOS.mm`(`PlatformiOS.mm` に同居でもよい) | `UIView` 派生。`+layerClass` を `CAMetalLayer` に override。`touchesBegan/Moved/Ended/Cancelled` を受けて `AppleInputSink`(§5.1)と `iOSImGui`(§5.3)へ転送。`layoutSubviews` で `drawableSize` を更新 |
| `aqEngine/Platform/iOS/iOSAppDelegate.mm` | `UIApplicationDelegate`。エンジンのブートストラップと終了、ライフサイクル通知(§3.2 / §3.4) |
| `aqEngine/Platform/iOS/iOSImGui.h` / `.mm` | ImGui プラットフォームバックエンド(§5.3) |
| `Game/Application/iOSMain.mm` | `int main()` → `UIApplicationMain`。`#if defined(AQ_PLATFORM_IOS)` ガード。既存 3 本のエントリ(`Main.cpp` / `UWPMain.cpp` / `MacMain.mm`)に続く 4 本目 |

改修対象:

| ファイル | 変更 |
|---|---|
| [Platform/IPlatform.h](../aqEngine/Platform/IPlatform.h) | `RunFrameLoop`(§3.3)と `GetWritableRoot`(§3.6)を追加 |
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

[Engine::RunGame()](../aqEngine/Engine.cpp) は現在こうなっている:

```cpp
void Engine::RunGame() {
    while (platform_->PumpEvents()) { Update(); }
}
```

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

`Engine::RunGame()` は `platform_->RunFrameLoop([this]{ Update(); });` の 1 行になる。
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
- `IsRenderable()` を足すかは Android 移植と共通の判断。iOS 単独では
  「`CADisplayLink` を止める」で足りるため**本移植では足さない**(Android が先に入るなら
  そちらの定義に従う)。
- サウンドの中断(電話着信等)は §6。

### 3.5 解像度とスケール

[Engine::InitializeWindow](../aqEngine/Engine.cpp) は `InitializeParameter` の値を
`screenWidth_` / `screenHeight_` に入れた**後**に `CreateMainWindow` を呼ぶ。つまり
**エンジンは「ウィンドウサイズは呼び出し側が知っている」前提**で、`CreateMainWindow` から
実サイズを受け取る経路が無い。

iOS で画面サイズは OS が決めるので、**AppDelegate が `UIScreen` を見て
`InitializeParameter` を埋める**(§3.2 の手順 2)。これで**エンジン側の改修はゼロ**。
`CreateMainWindow` は `desc` を検証に使うだけにする。

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

そこで `IPlatform` に追加する:

```cpp
/// 書き込み可能なディレクトリ。nullptr ならカレントディレクトリ(従来の挙動)。
virtual const char* GetWritableRoot() const { return nullptr; }
```

Win32 / Mac は既定の `nullptr` のまま(**挙動不変**)。iOS は `NSDocumentDirectory`
(またはログは `NSCachesDirectory`)を返す。UWP は
[PlatformUWP.cpp:184-193](../aqEngine/Platform/PlatformUWP.cpp) が既に `LocalState` へ
逃がす実装を持っているので、将来そちらへ寄せる余地もある(本移植では触らない)。

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

### 4.5 リサイズ経路の不在(本移植では回避する)

`Graphics/Metal/` に**リサイズ経路が存在しない**。メイン RT / G-Buffer / シャドウは
初期サイズで固定され、drawable だけ大きくなると `CopyToBackBuffer` が寸法不一致を検出して
フルスクリーン三角形で拡大コピーする。

iOS で回転・Safe Area・マルチウィンドウを許すと `drawableSize` が必ず変わるため、
**§0.5-5 で横向き固定を選び、この問題を本移植の範囲外に置く**。
Android 移植([Android移植設計.md §4.3](Android移植設計.md))はスワップチェーン再生成を
共通コードの改修として扱っており、**そちらが入れば iOS の回転対応も乗る**。

### 4.6 シェーダの起動時コンパイル(P6 の候補)

[MetalShader.mm](../aqEngine/Graphics/Metal/MetalShader.mm) は **59 本の MSL を毎起動
`newLibraryWithSource:` でコンパイル**する。macOS では Metal Toolchain が無い環境だったため
この方式を採ったが、**iOS 向けには Xcode に Metal Toolchain が同梱されている**ので
`.metallib` の事前ビルドが可能になる。起動時間に効くので P6 の候補に置く。
**P1〜P5 では現行方式を維持**する(変更点を増やさない)。

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

### 5.2 タッチ

`ITouchBackend`(タッチ点の配列: id / 座標 / 状態)の**抽象定義は
[Android移植設計.md §5.2](Android移植設計.md) を一次資料とする**(同じ設計を二重に書かない)。
仮想パッド方式(案 a)を採れば `IPadBackend` として供給できるためゲーム側と `ActionMap` の
変更はゼロ、という結論も共通。

iOS 固有の差分:

| 論点 | 内容 |
|---|---|
| 投入元 | `AqMetalView` の `touchesBegan/Moved/Ended/Cancelled:withEvent:`。座標は `[touch locationInView:]`(**左上原点なので Y 反転しない**) |
| `touchesCancelled` | Android の `ACTION_CANCEL` 相当。**忘れると指が張り付く**。`CocoaInputSink::OnFocusLost()` と同じ「全解放」処理へ繋ぐ |
| ホバーが無い | マウス前提の UI は「押した瞬間に座標が分かる」設計になっていない。§5.3 参照 |
| Apple Pencil / 3D Touch | 範囲外 |

### 5.3 ImGui(デバッグ UI)

[MacImGui.mm](../aqEngine/Platform/Mac/MacImGui.mm) は 720 行の自前バックエンド
(`imgui_impl_osx` は同梱しない方針)。iOS 版 `iOSImGui` を新規に書く。置き換えの内訳:

| 機能 | Mac 実装 | iOS |
|---|---|---|
| マウス位置 | `AddMousePosEvent(x, height - y)` | `[touch locationInView:]`。**Y 反転なし**。`touchesBegan` で**位置とボタンを同時に**送る(ホバーが無いため、送らないと ImGui が押下位置を知らない)。`touchesEnded` 後に `AddMousePosEvent(-FLT_MAX, -FLT_MAX)` でホバー解除 |
| ボタン | `buttonNumber` で 0〜4 | タッチ 1 点を button 0 にマップ。多ボタン分岐は不要 |
| カーソル形状 | `NSCursor` 7 種 + hide/unhide | **iOS に `NSCursor` は無い。** `ImGuiBackendFlags_HasMouseCursors` を立てず、関数ごと削除 |
| クリップボード | `NSPasteboard` | `UIPasteboard.generalPasteboard.string`。**API 形はほぼ 1:1** |
| キー入力 | Carbon `kVK_*` の約 130 エントリの変換表 | ハードウェアキーボード接続時のみ `UIPress` / `UIKey.keyCode`(`UIKeyboardHIDUsage_*`)。**体系が違うので変換表は作り直し**。優先度は低い(§5.4) |
| テキスト入力 | `NSEvent.characters` | `io.WantTextInput` を見てソフトウェアキーボードを出し入れ(不可視 `UITextField` 等)。`AddInputCharactersUTF8` 自体は流用可 |
| 修飾キー | `FlagsChanged` + Command 離しの keyUp 欠落対策 | `UIKey.modifierFlags`。**macOS 固有の keyUp 欠落対策(`g_commandHeld` / `ReleaseAllKeys`)は不要** |
| スクロール | `scrollingDeltaX/Y` + 精密デルタ | 相当する概念が無い。ドラッグスクロールへ |
| `DisplayFramebufferScale` | (1,1) 固定 | §3.5 で `contentsScale = 1.0` を採るので **(1,1) のまま整合する** |

§0.5-6 で「iOS では ImGui を無効化」を選ぶ場合、この節ごと不要になる。

### 5.4 物理コントローラとハードウェアキーボード

- パッドは §5.1 のとおり無改修で動く見込み(実機のコントローラで P5 に確認)。
  `SetVibration` は **Mac でも未実装**なので iOS 固有の残タスクではない。
- ハードウェアキーボードは `UIPress` / `UIKey` で取れるが、**優先度を下げる**
  (iOS アプリとしてキーボード前提の操作設計にしないため)。当面は
  `NullKeyboardBackend` / `NullMouseBackend` を差す。

---

## 6. サウンド

一次資料は [Sound設計.md](Sound設計.md) と [Mac移植設計.md §5](Mac移植設計.md)。本節は差分のみ。

**実測で分かっていること**(§0.2): `SoftwareMixer` / `CoreAudioSoundVoice` /
`ExtAudioFileDecoder.mm` は**無改修で iOS SDK を通る**。落ちるのは
[CoreAudioSoundBackend.mm](../aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.mm) 1 本だけ。

| 項目 | 変更 |
|---|---|
| include | `<CoreAudio/CoreAudio.h>` は **iOS に存在しない**(アンブレラヘッダが無い)。`<AudioToolbox/AudioToolbox.h>` + `<AVFAudio/AVAudioSession.h>` へ |
| 出力ユニット | `kAudioUnitSubType_DefaultOutput` → **`kAudioUnitSubType_RemoteIO`**。iOS SDK にはどちらの定数も宣言があるが、`DefaultOutput` は iOS では動かない。コード中の[コメント(:163-164)](../aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.mm)に既にこの旨が書かれている |
| **AVAudioSession(新規)** | カテゴリ(`AVAudioSessionCategoryAmbient` か `Playback`)の設定と `setActive:`。**iOS だけの必須手順**で、やらないと音が出ない/他アプリと競合する |
| **中断処理(新規)** | 電話着信・他アプリの再生で `AVAudioSessionInterruptionNotification` が来る。Began でユニット停止、Ended で再開。**やらないと復帰後に無音になる** |
| デコーダ | `ExtAudioFileDecoder`(AudioToolbox)をそのまま。現在のアセットは **wav 5 本**なので `WavDecoder` で足りる |
| `SoundBackend.h` | `AQ_PLATFORM_IOS` を Mac と同じ `SOUND_BACKEND_COREAUDIO` 分岐へ(`AQ_PLATFORM_APPLE` を使えば分岐追加ゼロ。§2.1) |

バックグラウンド遷移時の扱いは §3.4 と連動(`OnSuspend` で停止)。

---

## 7. リソース / ファイル IO ★サンドボックスの壁

### 7.1 読み込み: `GetContentRoot()` を見ない 3 系統

[Resource.cpp](../aqEngine/Resource/Resource.cpp) の `BuildResourcePathCandidates` は
`Engine::GetContentRoot()` を基点に候補を組むが、**第 1 候補は常に「元の相対パス(= CWD 相対)」**で、
さらに**この経路を通らないアセット読み込みが 3 系統ある**:

| 系統 | 実体 | 件数 |
|---|---|---|
| **シェーダ** | `FindProjectRoot` が **6 ファイルに重複実装**されており、うち Metal 経路 2 本([MetalShader.mm](../aqEngine/Graphics/Metal/MetalShader.mm) / [MetalRenderContextImpl.mm](../aqEngine/Graphics/Metal/MetalRenderContextImpl.mm))は `GetContentRoot()` を**一切見ない**。CWD から上へ `Game/Assets` を探す | `.metal` 59 本 + compute の `.spv`(threadgroup サイズ読み取り用) |
| **サウンドとオーディオバンク** | `OpenStream("Assets/Sound/…")` / `LoadBank("Assets/Audio/…")` が **CWD 相対のまま `fopen`**。バンク JSON の `basePath` も CWD 相対文字列を生成する | BGM / SE / バンク |
| **一部のメッシュ** | `LoadTkmMeshFile` / `LoadObjMesh` / PMD が `requestPath_` を直接 `fopen` | `.tkm` / `.obj` / `.pmd` |

Mac はこれを **`chdir`** で凌いでいる:
[MacMain.mm:61-66](../Game/Application/MacMain.mm) が CWD を `Contents/Resources/Game` へ移し、
(a) `"Assets/…"` の CWD 相対 fopen と (b) `FindProjectRoot` の `Game/Assets` 上方探索を
**同時に成立させている**(`Resources` へ移すと BGM が読めなくなることを実機で踏んでいる)。

| 案 | 内容 | 判断 |
|---|---|---|
| **(a) Mac と同じ `chdir` 方式**【推奨(まず P2 で採る)】 | 起動時に CWD をバンドル内の `<Bundle>/Game` へ移す | **採用。** iOS でもバンドル内への `chdir` は可能(read-only だが読めればよい)。**エンジンの読み込み経路を一切変えずに P2 が通る**。Mac と同じ形なので挙動差も出ない |
| (b) 3 系統を `GetContentRoot()` へ統一 | `FindProjectRoot` の 6 重複を 1 本にまとめ、サウンドとメッシュも候補解決経路へ寄せる | **本来やるべき掃除**だが、Windows / Mac / UWP 全部の回帰確認を伴う。iOS 移植とは分けて別途 `<Engine>` で行う(§8-2) |

### 7.2 書き込み

§3.6 の `GetWritableRoot()` で解決する。**バンドルが read-only なので、こちらは
「あとで」ではなく P2 の必須項目**(ログが黙って捨てられるだけならまだしも、
`imgui.ini` の保存失敗はデバッグ UI の状態が毎回リセットされる形で表面化する)。

### 7.3 バンドルレイアウト

**iOS のバンドルは `Contents/` 階層を持たない。** リソースはバンドル直下に置かれる。

```
macOS:  Game.app/Contents/Resources/Game/Assets/...
iOS:    Game.app/Game/Assets/...
```

[package_app.cmake](../Tools/PackageApp/package_app.cmake) に iOS 分岐を足す:

- コピー先を `<Bundle>/Game/Assets` にする(`Game/` 1 段を再現するのは
  `BuildResourcePathCandidates` が `"Assets/…"` を `<root>/Game/Assets/…` に組むため。
  UWP の `install/Game/Assets/…` と同じ理由)。
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
2. **`FindProjectRoot` の 6 重複と CWD 依存の掃除**(§7.1 案 b)。iOS 移植とは分けて
   別途 `<Engine>` で行う。**本移植で `chdir` 方式を採ると、この負債が 3 プラットフォーム目に
   広がる**ことは自覚しておく。
3. **`AQ_PLATFORM_APPLE` の導入範囲**(§2.1)。既存 `AQ_PLATFORM_MAC` 49 箇所のうち
   どれを共通化するかは 1 箇所ずつの判断。P0 の主作業。
4. **メモリ予算の具体値**(§3.6 / §4.4)。実機の機種が決まるまで暫定値。
   シャドウ 2048²×4 = 64MB をどこまで落とすかは実機の見た目と併せて判断。
5. **ImGui をどこまでやるか**(§0.5-6)。指での操作性が実用にならないなら、
   Release で切る/常時切るの判断を P3 で下す。
6. **Windows / Mac の回帰**。`IPlatform` への追加 IF 2 本(§3.3 / §3.6)は共通コードに
   手を入れるため、**各フェーズで Windows 3 構成(D3D11/D3D12/Vulkan)と Mac 2 構成
   (Vulkan/Metal)の回帰確認を行う**。どちらも既定実装で挙動不変になる設計だが、確認は要る。
7. **`ITouchBackend` の導入フェーズ**(§5.2)。Android 移植と iOS 移植のどちらが先に
   進むかで、抽象の導入者が変わる。**後から来た側が既存定義に従う。**

---

## 9. フェーズ計画

各フェーズは「実装内容 + 評価チェックリスト」で完結させ、1 フェーズ = 1 コミットとする。

### P0: ビルド基盤(iOS 実機・シミュレータ不要)

`AQ_PLATFORM_IOS` / `AQ_PLATFORM_APPLE` の新設と CMake の分岐。**リンクが通るところまで。**
プラットフォーム実装は骨格(`CreateMainWindow` が false を返す程度)で構わない。

- [PlatformDefs.h](../aqEngine/Platform/Common/PlatformDefs.h) に `AQ_PLATFORM_IOS`
  (`TARGET_OS_IPHONE` を `__APPLE__` より先に見る)と `AQ_PLATFORM_APPLE`
- 既存 `AQ_PLATFORM_MAC` 49 箇所の仕分け(§2.1)
- [AqCommon.cmake](../cmake/AqCommon.cmake) の `IOS` 分岐、[aqEngine/CMakeLists.txt](../aqEngine/CMakeLists.txt) の
  ソース除外 3 分岐化、フレームワークを `UIKit` へ
- ルート [CMakeLists.txt](../CMakeLists.txt) の API 検証(iOS は Metal のみ)
- `Platform/iOS/` の骨格、`Game/Application/iOSMain.mm`、`DebugOutputApple.cpp` への改名
- [CMakePresets.json](../CMakePresets.json) に `ios-xcode` / `ios-simulator-xcode` / `ios-ninja`

**評価チェックリスト**
- [ ] `cmake --preset ios-simulator-xcode` が configure できる
- [ ] `Game.app`(シミュレータ / arm64)が**リンクまで**通る
- [ ] `Game.app`(実機 / arm64、署名なし)が**コンパイルまで**通る
- [ ] Windows 3 構成(D3D11/D3D12/Vulkan)と Mac 2 構成(Vulkan/Metal)の Debug が従来どおりビルドできる
- [ ] 警告が Windows / Mac 構成で増えていない

### P1: シミュレータでクリア画面

UIKit エントリ + `CAMetalLayer` + フレーム駆動の委譲 + iOS 向け MSL。

- `iOSMain.mm`(`UIApplicationMain`)と `AqAppDelegate`(§3.2)
- `PlatformiOS`(`UIWindow` / `UIViewController` / `AqMetalView` / `+layerClass`)
- **`IPlatform::RunFrameLoop` の追加**(既定実装 = 現行 while ループ)と `CADisplayLink`(§3.3)
- `--msl-ios` の生成ターゲットと `msl-ios/` 読み出し分岐(§4.2)
- 入力は `NullKeyboardBackend` / `NullMouseBackend` を差す
- Info.plist(`UILaunchScreen` / 向き / `UIRequiredDeviceCapabilities`)

**評価チェックリスト**
- [ ] シミュレータにインストールでき、起動する
- [ ] Metal のクリア色が画面に出る
- [ ] `CADisplayLink` でフレームが回っている(フレーム番号のログで確認)
- [ ] 起動ログに GPU 名 / `supportsBCTextureCompression` / GPU family が出る(§4.3)
- [ ] Windows / Mac の回帰確認(`RunFrameLoop` は共通コード改修)

### P2: シミュレータで実シーン

アセットとシェーダをバンドルから読ませる。**BC 展開と書き込み先がここの山場。**

- `package_app.cmake` の iOS 分岐(`<Bundle>/Game/Assets` レイアウト。§7.3)
- 起動時の `chdir`(§7.1 案 a)と `GetContentRoot`
- **`IPlatform::GetWritableRoot` の追加**と `startup_timing.log` / `io.IniFilename` の付け替え(§3.6)
- **BC 非対応時の `DirectX::Decompress` 経路**(§4.3 案 a)
- エディタ保存 UI の無効化

**評価チェックリスト**
- [ ] タイトル画面が表示される
- [ ] ステージが描画される(路面・キャラ・地形・影・UI)
- [ ] **BC テクスチャ(DDS 7 枚)が正しく表示される**(展開経路が効いている)
- [ ] `startup_timing.log` が書き込み可能領域に出る
- [ ] Metal の Validation を有効にしてエラー 0(既定オフなので明示的に有効化して確認)
- [ ] メモリ使用量を計測して §4.4 の見積りと突き合わせた

### P3: 入力

- `ITouchBackend`(定義は [Android移植設計.md §5.2](Android移植設計.md) に従う)と iOS 実装
- `HID/Mac/` → `HID/Apple/` への移動と改名(§5.1)
- 仮想パッド(§0.5-3 で決めた方式)
- `iOSImGui`(§5.3。§0.5-6 で無効化を選んだ場合はスキップ)
- `touchesCancelled` の全解放

**評価チェックリスト**
- [ ] タッチでタイトルからステージへ進める
- [ ] 仮想パッドでキャラが操作できる
- [ ] ホームに戻る等で指が張り付かない(`touchesCancelled`)
- [ ] デバッグ UI が指で操作できる(または意図どおり無効になっている)

### P4: サウンド

- `CoreAudioSoundBackend.mm` の include と `kAudioUnitSubType_RemoteIO`
- **AVAudioSession のカテゴリ設定と中断処理**(§6)

**評価チェックリスト**
- [ ] BGM / SE がシミュレータで鳴る
- [ ] 3D 音響の定位が Mac と一致する
- [ ] 他アプリの再生や着信で中断 → 復帰しても音が壊れない・二重再生しない
- [ ] バックグラウンド → 復帰で音が戻る

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

### P6: 性能とパッケージング

- 解像度スケール(§0.5-4)、シャドウ解像度、`MTLStorageModeMemoryless`
- `framebufferOnly = YES` への復帰(§4.1)
- `.metallib` の事前ビルド(§4.6)
- `PlatformBudget` の実測値反映、ThreadPool のコア割り当て
- Release ビルドと `.ipa`

**評価チェックリスト**
- [ ] 目標フレームレートを実機で達成(目標値は着手時に決める)
- [ ] サーマルスロットリング後も破綻しない
- [ ] メモリ使用量が iOS のプロファイル内に収まる
- [ ] 起動時間を計測し、`.metallib` 事前ビルドの効果を記録した
- [ ] Release の `.ipa` が署名付きでインストールでき動作する

---

## 10. チェックポイント(設計全体)

- [ ] §0.5 の未決事項 6 点についてユーザーの判断を得た
- [ ] `AQ_PLATFORM_IOS` が「ちょうど 1 つだけ定義」の制約を壊していない
- [ ] `AQ_PLATFORM_APPLE` の導入で `AQ_PLATFORM_MAC` の分岐が**増えて**いない
- [ ] iOS 固有の型(`UIView` / `UIWindow` / `UITouch` / `CADisplayLink`)が
      `Platform/iOS/` の外に現れない(Mac が `Platform/Mac/` に閉じているのと同じ)
- [ ] `IPlatform` への追加 IF 2 本(`RunFrameLoop` / `GetWritableRoot`)が、
      Windows / Mac / UWP の既定実装で**挙動不変**として成立する
- [ ] iOS 対応のために Windows / Mac の挙動を変えていない
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
