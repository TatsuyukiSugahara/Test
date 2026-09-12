# Android 移植 設計

> 対象コミット: d1e7f6a / 最終更新: 2026-09-12

## 現在の到達点

**P0(ビルド基盤)完了。** NDK r27c で `libGame.so`(arm64-v8a)がエラー 0 でリンクまで通る。
APK 化・実機起動は P1 以降。

| | 状態 |
|---|---|
| Android ビルド | `cmake --preset android-arm64` → `--build --preset android-arm64-debug` が通る。エラー 0 / 警告 23 |
| 成果物 | `build/android-arm64/lib/Debug/libGame.so`(ELF64 / AArch64 / DYN、Debug 87MB) |
| 依存 `.so` | liblog / libandroid / libvulkan / libm / libdl / libc の 6 本。libc++ は静的リンク |
| Windows 回帰 | D3D11 / D3D12 / Vulkan の Debug すべてエラー 0・警告 54 件(従来と同数)。起動〜終了コード 0 |
| Mac 回帰 | **未確認**(この環境では Mac をビルドできない)。`StartupLog` の一本化が `PlatformMac.mm` に及ぶので次に Mac を触るとき要確認 |
| 実機 | 未着手(P1) |

導入手順とハマりどころは [Tools/SetupCMake/README.md](../Tools/SetupCMake/README.md) §6 が正本。

対象: `aqEngine/` + `Game/`。既存の Vulkan バックエンドを Android(NDK)で動かし、実機で
AquaDash が起動〜プレイできる状態までを設計する。

姉妹文書:
- [Mac移植設計.md](Mac移植設計.md) / [Mac移植調査.md](Mac移植調査.md) — 非 Windows 化の土台。
  CMake・`AQ_PLATFORM_*`・`IPlatform`・入力/サウンド抽象・事前 `.spv` 生成は**すべてここで導入済**。
  本書はその差分として Android を足す。
- [VulkanBackend設計.md](VulkanBackend設計.md) — Vulkan バックエンドの一次資料。
- [Xbox移植設計.md](Xbox移植設計.md) — `IPlatform` 抽象の導入元。
- [Sound設計.md](Sound設計.md) §8 — Oboe バックエンドの一次資料(本書では重複させない)。

**本書のステータス: P0 完了。** §0.4 の判断は 2026-09-12 に確定した。次は P1。

---

## 0. 方針

### 0.1 結論(先に)

- **グラフィックスは新規実装ゼロ**で済む見込み。Vulkan バックエンドは P0〜P4 + imgui まで
  実機動作済で、Android 対応はサーフェス生成の 1 分岐(Mac の `VK_EXT_metal_surface` 追加と同型)。
- **作業の実体はプラットフォーム層・ビルド・入力・アセット配置**。Mac 移植と同じ構図だが、
  Mac には無かった **3 つの新規要素**がある:
  1. **アプリのライフサイクルでサーフェスが消える**(`onPause` でウィンドウ破棄)
  2. **タッチ入力**(現在 Keyboard / Mouse / Pad の 3 系統しか抽象が無い)
  3. **APK 内アセットは `fopen` で開けない**
- **Visual Studio の Android C++ ワークロードは使わない**(§0.2)。すでにある CMake 経路を
  NDK ツールチェーンへ向け、APK 化は Gradle が担う。

### 0.2 ビルド経路: Visual Studio の Android ワークロードは採らない

当初の想定は「VS の Android 対応を入れる」だったが、調査の結果**採用しない**。

| | 状況 |
|---|---|
| VS 2026 (18.0) | `Mobile development with C++`(iOS/Android)は**非サポート**。将来のアップデートで削除予定と公式にアナウンス済(Android NDK 自体の提供は継続) |
| VS 2022 | ワークロードは残っているが、中身は Ant/Gradle 時代の `.androidproj` テンプレ。CMake + 現行 NDK + Vulkan の運用と噛み合わない |
| 当環境 | 主に使っているのは VS 18 Community(`C:\Program Files\Microsoft Visual Studio\18\Community`) |

したがって:

- **ビルドは CMake + NDK ツールチェーン**(`$NDK/build/cmake/android.toolchain.cmake`)。
  [CMakeLists.txt](../CMakeLists.txt) 一式は Mac 移植で整備済なので、その 3 分岐化で足りる。
- **パッケージング(APK)/デプロイ/実機デバッグは Gradle + Android Studio**。
- **VS を使いたい場合**は「フォルダを開く(CMake)」で `CMakePresets.json` の `android-*` プリセットを
  選べば、**ビルドと IntelliSense までは VS 上で回せる**。実機への配布とネイティブデバッガは
  `adb` / Android Studio 側に置く。この分担が現実的な落としどころ。

> 補足: 「VS で完結する」体験は諦めることになる。ここは §0.4 の未決事項 1 としてユーザー判断を仰ぐ。

### 0.3 グラフィックス: 道は 1 本(Vulkan)

Mac のような「道A / 道B」の分岐は無い。Android のネイティブ 3D API は Vulkan(と GLES)で、
既存の Vulkan バックエンドがそのまま第一候補。GLES へ落とす選択肢は**採らない**
(Deferred + compute(Bloom/トーンマップ)+ 配列シャドウマップを GLES3 で書き直す作業量が、
得られる互換性に見合わない)。

### 0.4 決定事項(2026-09-12)

| # | 論点 | 決定 | 帰結 |
|---|---|---|---|
| 1 | ビルド経路 | **CMake + Gradle**。VS の Android ワークロードは使わない | VS は「フォルダを開く(CMake)」でビルド/IntelliSense まで。デプロイと実機デバッグは adb / Android Studio |
| 2 | 最低 Android / Vulkan | **Android 13+ / Vulkan 1.3**(`ANDROID_PLATFORM=android-33`) | Vulkan バックエンドにバージョン分岐を**入れない**。1.1/1.2 端末向けの拡張フォールバックは §8-1 のまま別途 |
| 3 | タッチ操作 | **入力ソースを問わない仮想パッドを `aqEngine/HID` に置く**。物理コントローラでもタッチでも同じ `IPadBackend` として `ActionMap` に見せる | ゲーム側・`ActionMap` の変更ゼロ。iOS 移植とも共有する(§5.3) |
| 4 | 検証端末 | **実機あり** | P1 以降は実機で検証する。機種と Android バージョンは P1 着手時に記録し、§4.2 のフィーチャ確認結果を本書へ追記する |

---

## 1. 現状の棚卸し

### 1.1 Mac 移植で既に整った資産(Android がそのまま乗る土台)

| 資産 | 実体 | Android での扱い |
|---|---|---|
| プラットフォームマクロ | [Platform/Common/PlatformDefs.h](../aqEngine/Platform/Common/PlatformDefs.h) | `AQ_PLATFORM_ANDROID` を 4 つ目として追加 |
| プラットフォーム抽象 | [Platform/IPlatform.h](../aqEngine/Platform/IPlatform.h) | `PlatformAndroid` を実装として追加。**ただし IF 拡張が要る**(§3.3) |
| ビルド | [CMakeLists.txt](../CMakeLists.txt) / [CMakePresets.json](../CMakePresets.json) / [cmake/AqCommon.cmake](../cmake/AqCommon.cmake) | 3 分岐化 + `android-*` プリセット追加 |
| シェーダ | [Tools/ShaderCompile/compile_spv.cmake](../Tools/ShaderCompile/compile_spv.cmake) と [VulkanShader.cpp](../aqEngine/Graphics/Vulkan/VulkanShader.cpp) の事前 `.spv` 経路 | **そのまま使える**(実行時 DXC が無い点は Mac と同条件) |
| 入力抽象 | `IKeyboardBackend` / `IMouseBackend` / `IPadBackend` + Null 実装 | キーボード/マウスは Null を既定に。**タッチの抽象だけ無い**(§5) |
| サウンド抽象 | `ISoundBackend` / `ISoundVoice` + [SoundBackend.h](../aqEngine/Sound/SoundBackend.h) | `__ANDROID__` → Oboe の**分岐だけ既に書いてある**(現在は `#error`) |
| 数学 / 画像 | ThirdParty 同梱の DirectXMath・DirectXTex(非 Windows 経路)・WinCompat(`sal.h`)・stb_image | 追加作業なしで通る見込み(ARM64 NEON) |
| 物理 | [ThirdParty/CMakeLists.txt](../ThirdParty/CMakeLists.txt) の非 Windows 分岐で Bullet をソースビルド | そのまま |

Mac 固有 `#ifdef` はリポジトリ全体で **49 箇所**。抽象の穴は少なく、Android 分岐も同規模で収まる見込み。

### 1.2 Android で新たに要るもの(概観)

| 領域 | 新規 | 既存改修 |
|---|---|---|
| ビルド | Gradle プロジェクト、`android-*` プリセット | CMake 3 分岐化、ターゲット種別(exe→so) |
| プラットフォーム | `Platform/Android/`(4〜5 ファイル)、`AndroidMain.cpp` | `PlatformDefs.h`、`IPlatform`(ライフサイクル) |
| グラフィックス | なし | `CreateSurface` 分岐、スワップチェーン再生成、回転 |
| 入力 | タッチ抽象 + Android 実装、`AndroidPadBackend` | `ActionMap` / `InputBinding` |
| サウンド | `OboeSoundBackend`(CoreAudio 実装が雛形) | `SoundBackend.h` の `#error` を外す |
| リソース | AAssetManager 経路 or 展開 | `Resource.cpp` / `VulkanShader.cpp` のパス前提 |

---

## 2. ビルドシステム

### 2.1 マクロ

[PlatformDefs.h](../aqEngine/Platform/Common/PlatformDefs.h) に `AQ_PLATFORM_ANDROID` を追加する。

- 自動推定は `__ANDROID__` から。**`_WIN32` / `__APPLE__` の判定より先**に置く。
- 派生マクロの扱い:
  - `AQ_PLATFORM_DESKTOP` に**入れない**(Win32 / Mac のみ)。
  - `AQ_PLATFORM_WINDOWS_FAMILY` に**入れない**。
  - 必要なら `AQ_PLATFORM_MOBILE` を新設するが、当面は使用箇所が無いので**作らない**
    (使う場面が出てから足す)。
- 「ちょうど 1 つだけ定義」の静的検査に `AQ_PLATFORM_ANDROID` を加える。

### 2.2 CMake の 3 分岐化 ★最初の作業

現状は Windows と「それ以外(= Mac 前提)」の 2 分岐で、Android を足すとそのまま壊れる。

| ファイル | 現状 | 変更 |
|---|---|---|
| [cmake/AqCommon.cmake](../cmake/AqCommon.cmake) `aq_apply_platform_definitions` | `WIN32` / `APPLE` / `else() → FATAL_ERROR` | `ANDROID` 分岐で `AQ_PLATFORM_ANDROID` を定義 |
| [aqEngine/CMakeLists.txt](../aqEngine/CMakeLists.txt) のソース除外 | `if(WIN32) … else() …` の 2 分岐。`else()` 側は Mac 前提 | 3 分岐化。Android では **`.mm` / `HID/Mac/` / `Sound/CoreAudio/` も除外**し、代わりに `Platform/Mac/` を落とす |
| 同・プラットフォームライブラリ | `if(WIN32)` / `elseif(APPLE)` | Android 分岐で `log` / `android` / `vulkan`(または動的ロード)/ `native_app_glue` をリンク |
| 同・Vulkan の SDK 解決 | `AQ_VULKAN_SDK` を要求 | **Android は NDK 同梱のヘッダ/`libvulkan.so` を使う**ので SDK 要求を外す |
| [Game/CMakeLists.txt](../Game/CMakeLists.txt) | `add_executable(Game WIN32 MACOSX_BUNDLE …)` | Android は **`add_library(Game SHARED …)`**(`libGame.so`) |
| ルート [CMakeLists.txt](../CMakeLists.txt) | `AQ_GRAPHICS_API` は非 Windows で Vulkan 強制済 | 変更なし。`aqCompileSpv` の配線は §4.4 参照 |

注意点:

- **`UNIX` は Android でも真**になる。分岐は必ず `if(ANDROID)` を**先に**書く。
- `WIN32` / `APPLE` はいずれも偽。`CMAKE_SYSTEM_NAME` は `Android`。
- `CMAKE_MSVC_RUNTIME_LIBRARY` 等の MSVC 専用設定は `if(MSVC)` で既にガード済。

### 2.3 ターゲット種別と Gradle

Android アプリの実行主体は Java/Kotlin 側の Activity で、ネイティブは `.so` として読み込まれる。

```
DirectX/Android/                 (新設)
├─ settings.gradle
├─ build.gradle
├─ gradle.properties
└─ app/
   ├─ build.gradle              externalNativeBuild { cmake { path ../../CMakeLists.txt } }
   └─ src/main/
      ├─ AndroidManifest.xml    android.app.NativeActivity or GameActivity
      └─ assets/ (or jniLibs/)  §7 参照
```

- `externalNativeBuild` から**既存の `DirectX/CMakeLists.txt` をそのまま呼ぶ**。CMake の定義を
  Gradle 側へ二重化しない。
- 出力は `libGame.so`。Manifest の `android.app.lib_name` にライブラリ名を指定する。
- 置き場所を `DirectX/Android/` にするのは、`DirectX/` 配下で完結させ既存の `.sln` / vcxproj と
  干渉させないため。

### 2.4 ツールチェーン / ABI / API レベル

| 項目 | 値(案) | 根拠 |
|---|---|---|
| NDK | r26 以降(開発機は **r27c**) | C++20 / libc++ / `std::filesystem` が安定して使える |
| `ANDROID_ABI` | `arm64-v8a`(必須)+ `x86_64`(任意) | 実機は arm64。`x86_64` はエミュレータ用で開発が回りやすくなるが、Vulkan は制限あり |
| `ANDROID_PLATFORM` | **`android-33`** | Vulkan 1.3 の下限(Android 13)。§0.4-2 の決定 |
| `ANDROID_STL` | **`c++_static`** | `.so` が 1 本だけなので静的で足りる。APK に `libc++_shared.so` を同梱しなくて済む |
| C++ 標準 | C++20 | 既存設定のまま |
| PCH | `aq.h` を `target_precompile_headers` で継続 | clang でも仕組みは同じ。中身の Windows ブロックは既に分離済 |

**NDK のパスに空白を入れてはいけない。** 空白入りだと Ninja がコンパイラを 8.3 短縮名で
呼び、clang が argv[0] から C++ ドライバと判定できず libc++ をリンクしないまま
`std::` / `__cxa_*` が未定義になる(コンパイルは全て通るので原因が分かりにくい)。
ルート `CMakeLists.txt` に検出を入れて FATAL_ERROR で止めている。回避手順は
[Tools/SetupCMake/README.md](../Tools/SetupCMake/README.md) §6.2。

### 2.5 ThirdParty の通し確認

いずれも Mac(ARM64 clang)で通っているので大きな懸念は無いが、P0 で個別に確認する。

| ライブラリ | 確認事項 |
|---|---|
| Bullet | ソースビルド。`BT_USE_DOUBLE_PRECISION` + `BT_THREADSAFE=1` のまま NEON で通るか |
| DirectXTex | 非 Windows 経路(DDS/TGA/HDR + BC ソフトコーデック)。`wsl/winadapter.h` が Android でも成立するか |
| DirectXMath | ARM64 NEON パス。`sal.h` は ThirdParty/WinCompat が供給 |
| stb_image | PNG デコード(現在アセットは PNG 27 枚) |
| ufbx / vma / spirv_reflect | 可搬。`spirv_reflect.c` は C ファイル扱いのまま |
| imgui | core はそのまま。`imgui_impl_android` を追加(§4.6) |
| Oboe | 新規取り込み(§6) |

---

## 3. プラットフォーム層

### 3.1 責務表

| ファイル(新設) | 責務 |
|---|---|
| `aqEngine/Platform/Android/PlatformAndroid.h/.cpp` | `IPlatform` 実装。`ANativeWindow*` の保持、`ALooper` によるイベントポンプ、`GetContentRoot`、ライフサイクル状態の保持 |
| `aqEngine/Platform/Android/AndroidApp.h/.cpp` | `android_app`(native_app_glue / GameActivity)のコールバックを受け、`PlatformAndroid` へ橋渡しする層。Android の型を `PlatformAndroid` の外へ漏らさない |
| `aqEngine/Platform/Common/DebugOutputAndroid.cpp` | `__android_log_print` による `DebugOutput` 実装 |
| `Game/Application/AndroidMain.cpp` | `android_main`。`Main.cpp` / `MacMain.mm` と同型のブートストラップ |

改修対象:

| ファイル | 変更 |
|---|---|
| [Platform/IPlatform.h](../aqEngine/Platform/IPlatform.h) | ライフサイクル IF の拡張(§3.3) |
| [Platform/PlatformBudget.h](../aqEngine/Platform/PlatformBudget.h) | Android プロファイル(メモリ上限・ワーカ数) |
| [aq.cpp](../aqEngine/aq.cpp) の `StartupLog` / `StartupMark` / クラッシュハンドラ | Android 分岐(§3.4) |

### 3.2 エントリと `android_main`

`Main.cpp`(`WinMain`)/ `UWPMain.cpp` / `MacMain.mm`(`main`)に続く **4 本目のエントリ**。
既存 3 本と同じく 30〜50 行のブートストラップに収め、`AQ_PLATFORM_ANDROID` でガードして
他構成では空 TU にする(既存の作法と揃える)。

流れ:

1. `android_main(android_app*)` で開始
2. `PlatformAndroid` を生成し `android_app` を渡す
3. **`APP_CMD_INIT_WINDOW` を受け取るまでイベントを回して待つ**(ここが Win32/Mac と決定的に違う。
   ウィンドウが来る前に `Engine::Initialize` を呼べない)
4. `Engine::Create` → `CreateApplication<app::Application>` → `Initialize`(`platform` を注入)→ `RunGame`
5. `Finalize`

**`NativeActivity` か `GameActivity` か**は §0.4-3(タッチ方針)と併せて決める。
`GameActivity` はタッチ/キー/ゲームパッド(Paddleboat)/テキスト入力が統合されており、
入力実装が薄くなる代わりに Gradle 依存(`androidx.games:games-activity`)が増える。**推奨は GameActivity。**

### 3.3 ライフサイクル ★本移植で最大の新規設計

Win32 / Mac には無く、UWP でも部分的にしか無かった要件。

- `onPause` / `onStop` で **`ANativeWindow` が破棄される**(`APP_CMD_TERM_WINDOW`)。
  この時点で `VkSurfaceKHR` とスワップチェーンは無効になる。
- 復帰時(`APP_CMD_INIT_WINDOW`)に**別のウィンドウが渡る**。サーフェスから作り直す必要がある。
- バックグラウンド中はフレームを回してはいけない(present 先が無い)。

現在の [IPlatform](../aqEngine/Platform/IPlatform.h) は `OnSuspend()` / `OnResume()` を持つが、
**「描画対象が消える」ことを表現していない**。またグラフィックスデバイス側にも
サーフェス再生成の入口が無い。設計として次を足す:

| 追加 IF | 置き場所 | 意味 |
|---|---|---|
| `bool IsRenderable() const` | `IPlatform` | 描画可能か。false の間はメインループがフレームをスキップする |
| `void OnSurfaceLost()` / `void OnSurfaceCreated(NativeWindowHandle)` | `Engine`(またはレンダラ側の受け口) | サーフェス依存リソースの破棄/再生成の起点 |
| `bool RecreateSurface(NativeWindowHandle)` | `IGraphicsDeviceImpl` | Vulkan 実装のみ実体を持ち、他バックエンドは既定 no-op |

破棄/再生成の対象は **surface / swapchain / swapchain 画像ビュー / present セマフォ**に限定し、
デバイス・パイプライン・オフスクリーン RT・テクスチャは**保持する**(Android ではデバイスロストは
稀。デバイスごと作り直す設計にすると復帰が重くなる)。

> この節は Mac 移植には無かった項目で、**工数見積りの主要因**。P3 として独立フェーズを割り当てる。

### 3.4 ログ・診断・予算

- `DebugOutput` は `__android_log_print(ANDROID_LOG_INFO, "AquaDash", …)`。
- `StartupLog` / `StartupMark` の出力先は **logcat + アプリの内部ストレージ**
  (`ANativeActivity::internalDataPath`)。現在のカレントディレクトリ前提は使えない。
- [aq.cpp](../aqEngine/aq.cpp) のクラッシュスタックロガーは **VEH + dbghelp** で Windows 専用。
  Android では枠ごと `#ifdef` で落とす(必要になったら `libunwind` 版を別途)。
- `PlatformBudget` に Android プロファイルを追加。ワーカ数は `hardware_concurrency` の
  そのまま使用をやめる(big.LITTLE で小コアに割り当たると描画スレッドが詰まる)。

---

## 4. グラフィックス(Vulkan)

### 4.1 サーフェス生成 — P0 で実装済み

Mac 対応と完全に同型の 1 分岐。**当初 P1 に置いていたが、これが無いと
Vulkan バックエンドが Win32 分岐へ落ちてコンパイルできないため P0 で入れた。**

| 箇所 | 変更 |
|---|---|
| [VulkanCommon.h](../aqEngine/Graphics/Vulkan/VulkanCommon.h) | `VK_USE_PLATFORM_ANDROID_KHR` を定義 |
| [VulkanGraphicsDeviceImpl.cpp](../aqEngine/Graphics/Vulkan/VulkanGraphicsDeviceImpl.cpp) `CreateSurface` | `vkCreateAndroidSurfaceKHR`(`ANativeWindow*`)分岐 |
| 同・インスタンス拡張 | `VK_KHR_ANDROID_SURFACE_EXTENSION_NAME` を追加(portability 系は不要) |
| `NativeWindowHandle` | `ANativeWindow*` を `void*` として格納(型変更なし) |

### 4.2 Vulkan バージョンとフィーチャ ★最大のリスク

現在の実装は **Vulkan 1.3 core**(dynamic rendering / synchronization2 / scalarBlockLayout)を前提にしている。

- Android の Vulkan 1.3 は **API 33(Android 13)以降**。Android 公式のベースライン推奨は依然 **1.1**
  (Android Baseline Profile 2022 の最低 API バージョンが 1.1)。
- 1.3 を要求すると対応端末が絞られる。1.1/1.2 端末も拾うなら `VK_KHR_dynamic_rendering` /
  `VK_KHR_synchronization2` / `VK_EXT_scalar_block_layout` の**拡張フォールバック分岐**が要る。

→ **§0.4-2 の判断待ち。推奨は Android 13+ / Vulkan 1.3 に割り切る**(改修が最小で、
性能検証にも新しめの端末を使うことになるため)。

P1 で実機の対応状況を確認する項目:

- [ ] `VkPhysicalDeviceProperties::apiVersion`
- [ ] `dynamicRendering` / `synchronization2` / `scalarBlockLayout`
- [ ] `samplerAnisotropy`
- [ ] 負のビューポート高さ(`maintenance1` = 1.1 core なので可のはず)
- [ ] `R16G16B16A16_SFLOAT` の `COLOR_ATTACHMENT` + `STORAGE`(HDR メイン RT と Bloom compute が要求)
- [ ] `D32_SFLOAT` の depth attachment + サンプル
- [ ] `vkCmdBlitImage2`(`VK_KHR_copy_commands2` / 1.3 core。`CopyToBackBuffer` が使用)
- [ ] スワップチェーンのサーフェスフォーマット(BGRA 前提だと通らない端末がある。選択ロジックの確認)

### 4.3 スワップチェーン再生成と画面回転 ★既存実装の欠落

現在 [VulkanGraphicsDeviceImpl.cpp](../aqEngine/Graphics/Vulkan/VulkanGraphicsDeviceImpl.cpp) は
`vkAcquireNextImageKHR` / `vkQueuePresentKHR` の**戻り値を確認していない**。
デスクトップではウィンドウサイズ固定運用のため表面化していないが、Android では回転・復帰・
マルチウィンドウで日常的に `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` が返る。

- 戻り値を判定し、`OUT_OF_DATE` / `SUBOPTIMAL` でスワップチェーンを作り直す経路を新設する。
- **プリトランスフォーム**: `VkSurfaceCapabilitiesKHR::currentTransform` を無視すると、
  コンポジタが毎フレーム回転合成を挟み帯域を食う。最低限 `currentTransform` をそのまま
  `preTransform` に指定し、90/270 度時は射影行列側で吸収する。
- Android は起動直後のサーフェスサイズと実サイズがずれることがあるため、
  レンダー解像度は `currentExtent` から取り直す。

> この改修は **Windows / Mac にも効く**(ウィンドウリサイズ対応)。Android 専用 `#ifdef` にはしない。

### 4.4 シェーダ

- Android に実行時 DXC は無い。**事前ビルドした `.spv` を APK へ同梱する**
  (Mac で実証済の経路。[VulkanShader.cpp](../aqEngine/Graphics/Vulkan/VulkanShader.cpp) は対応済)。
- `.spv` の生成は Windows か Mac 側で行う。`aqCompileSpv` を Android ビルドから直接呼ぶと
  ビルドマシンに Vulkan SDK(dxc)を要求してしまうため、**生成物をアセットとして受け取る形**にする
  (生成手順は既存のまま。Gradle 側は生成済み `spv/` をコピーするだけ)。
- SPIR-V が要求する capability(scalar block layout 等)が端末ドライバで通るかは §4.2 で確認する。

### 4.5 性能(タイラー GPU)

現行のフレーム構成は **Deferred(MRT 4 枚 + 深度)→ 影 → HDR オフスクリーン(RGBA16F)→
Bloom/トーンマップ compute → blit** で、モバイル GPU にはメモリ帯域が重い。P6 の課題とし、
まずは絵を出すことを優先する。想定する打ち手:

- レンダー解像度スケール(`renderWidth/Height` は既に `InitializeParameter` で分離されている)
- Bloom の縮小段数削減 / トーンマップのみに落とす簡易パス
- MRT のフォーマット見直し(RGBA16F → RGB10A2 等)
- 将来的にはタイラー向けに subpass 相当(dynamic rendering の `LOAD_OP_DONT_CARE` 徹底)

### 4.6 検証レイヤ / imgui

- 検証レイヤは NDK 同梱の `libVkLayer_khronos_validation.so` を `jniLibs` へ入れると有効化できる。
  Debug 構成のみ同梱。出力は logcat。
- imgui は **描画は既存の自前 [VulkanImGui](../aqEngine/Graphics/Vulkan/VulkanImGui.h) をそのまま使える**。
  入力側に `imgui_impl_android` を追加する(Mac の P4b と同じ構図)。
  [Core/Application.cpp](../aqEngine/Core/Application.cpp) の init / NewFrame / Shutdown に分岐を足す。

---

## 5. 入力 ★新規抽象が要る

### 5.1 現状

`Input` が持つのは **Keyboard / Mouse / Pad の 3 系統**([02_HID設計.md](02_HID設計.md))。
タッチという概念が抽象に存在しない。

### 5.2 タッチの扱い — 入力ソースを問わない仮想パッド(§0.4-3 の決定)

**方針: 上位から見て「パッドが 1 つある」状態に統一する。** タッチで操作していようが
物理コントローラを繋いでいようが、`ActionMap` とゲーム側は区別しない。
差は `aqEngine/HID` の内側に閉じる。

| 追加するもの | 役割 |
|---|---|
| `ITouchBackend`(新設) | 生のタッチ点の配列(id / 正規化座標 / 押下・移動・離上)を供給する抽象。プラットフォーム実装が書き込み、上位は読むだけ |
| `VirtualPadBackend`(新設・`IPadBackend` 実装) | `ITouchBackend` を読み、画面上の仮想スティック/ボタンの当たり判定を経てパッド状態(スティック 2 本 + ボタン)へ変換する |
| `CompositePadBackend`(新設・`IPadBackend` 実装) | `VirtualPadBackend` と物理パッド実装を束ね、**先に入力があった側**を採用して 1 つのパッドとして見せる。Android の `DefaultPadBackend` はこれ |

- **`ActionMap` / `InputBinding` の変更は不要**。既存のパッド用バインディングがそのまま効く。
- `ITouchBackend` は仮想パッドのためだけでなく **UI のタップ判定**にも要る(こちらは
  `UIScreenManager` 側から読む)。
- `IMouseBackend` にタッチを流し込む案は採らない。多点・ホバー無しという差異が
  UI 側へ漏れ、デスクトップと挙動が分かれるため。
- 仮想パッドの**描画**をどこに置くかは P4 着手時に決める(§8-4)。判定と描画は
  分離し、`VirtualPadBackend` は当たり判定のレイアウトだけを持つ。
- **iOS 移植と共有する**。`ITouchBackend` / `VirtualPadBackend` / `CompositePadBackend` は
  プラットフォーム非依存に書き、iOS は `ITouchBackend` 実装だけを足す
  ([iOS移植設計.md](iOS移植設計.md) から本節を一次資料として参照している)。

### 5.3 物理パッドとキーボード/マウス

- `AndroidPadBackend` を `IPadBackend` 実装として追加し、`CompositePadBackend` に束ねる。
  GameActivity 採用なら Paddleboat(`androidx.games:games-controller`)で機種差を吸収できる。
- キーボード/マウスは `NullKeyboardBackend` / `NullMouseBackend`(P0 で配線済み)。
  Android では物理キーボード/マウスを想定しない。
- 戻るキー / フォーカス喪失時の入力リセット / IME は P4 で扱う。

---

## 6. サウンド

一次資料は [Sound設計.md](Sound設計.md) §8。本書では差分だけ書く。

- [SoundBackend.h](../aqEngine/Sound/SoundBackend.h) は `__ANDROID__` → `SOUND_BACKEND_OBOE` の分岐を
  **既に持っており**、現在は `#error` で止まる。`OboeSoundBackend` を実装して `#error` を外す。
- 構成は **`SoftwareMixer` + Oboe(AAudio)出力ストリーム**。
  **Mac の [CoreAudioSoundBackend](../aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.h) が
  そのまま雛形になる**(どちらも「出力ストリーム 1 本 + レンダーコールバックで `SoftwareMixer::Render`」)。
- Oboe を ThirdParty へ取り込む(ソース or AAR)。
- `MFDecoder` / `VideoPlayer` は Windows 専用のため CMake で除外済(非 Windows 分岐)。
  **現在のアセットは wav 4 本のみ**なので、可搬な `WavDecoder` / `WavStreamDecoder` で足りる。
  圧縮音源が必要になった時点で stb_vorbis か MediaCodec を検討する(P5 の範囲外)。

---

## 7. リソース / ファイル IO ★APK の壁

**APK 内の `assets/` は通常のファイルパスでは開けない**(`fopen` / `std::filesystem` が使えない)。
現在アセットを開いているのは以下:

| 箇所 | 現状 |
|---|---|
| [Resource.cpp](../aqEngine/Resource/Resource.cpp) `FindProjectRoot` / `GetContentRoot` 経路 | カレントから上へ `Game/Assets` を探索。パッケージ配置時は `Engine::GetContentRoot()` を使う |
| [VulkanShader.cpp](../aqEngine/Graphics/Vulkan/VulkanShader.cpp) | 同型の `FindProjectRoot` + `std::fopen` |
| [ImageLoader.cpp](../aqEngine/Resource/ImageLoader.cpp) | DirectXTex / stb_image にパスを渡す |

方針は 2 案:

| 案 | 内容 | 長所 | 短所 |
|---|---|---|---|
| **(a) 初回起動時に展開**【推奨(まず P2 で採る)】 | APK の `assets/` を `internalDataPath` へコピーし、`GetContentRoot` にそのパスを返す | **既存のファイル IO を一切変えなくてよい**。P2 が最短で通る | 起動時間とストレージを二重に消費 |
| (b) `AAssetManager` 経路 | ファイル読み込みを抽象化し、Android では `AAsset_*` で読む | ストレージ効率が良い | `Resource` / `ImageLoader` / `VulkanShader` のパス前提に手が入る |

**(a) で先に絵を出し、必要になったら (b) へ移す**。(b) へ移す場合も、抽象化の単位は
「パス → バイト列」の 1 関数に絞る(`ImageLoader` が DirectXTex にパスを渡している箇所だけは
メモリからのロード API に差し替えが要る)。

その他:

- セーブデータ・ログの書き込み先は `internalDataPath`。カレントディレクトリ前提の箇所を洗う。
- APK サイズが上限に触れるようなら Play Asset Delivery。当面は非対象。
- `ImageLoader` の `wchar_t` パス経路(`ToWidePath`)が Android clang で通るか P0 で確認する
  (Mac で通っているので流用できる見込み)。

---

## 8. オープン課題

1. **Vulkan 1.3 の対応端末**(§4.2)。実機が決まり次第 `apiVersion` を確認する。1.1/1.2 しか無い場合の
   拡張フォールバックは**別途設計**とし、本書のフェーズ計画には含めない。
2. **サーフェス再生成の粒度**(§3.3)。デバイスを保持したまま surface/swapchain だけ作り直す前提で
   設計しているが、実機で `VK_ERROR_DEVICE_LOST` が出る端末があればデバイス再生成まで広げる必要がある。
3. **`aqCompileSpv` の実行タイミング**(§4.4)。Windows/Mac で生成した `.spv` を Android ビルドへ
   どう受け渡すか(コミットするか、CI 成果物にするか)は運用の判断。
4. **タッチ UI の描画をどこに置くか**(§5.2 案 a)。`UIScreenManager` の上に置くか、
   デバッグ UI と同じ層に置くか。P4 着手時に決める。
5. **エミュレータでどこまで判定できるか**。Vulkan の機能・性能とも制限があり、
   P1 の「絵が出た」以上の判定には使えない見込み。
6. **Windows / Mac の回帰**。§4.3(スワップチェーン再生成)は全プラットフォーム共通コードに
   手を入れるため、各フェーズで Windows 3 構成(D3D11/D3D12/Vulkan)の回帰確認を行う。

---

## 9. フェーズ計画

各フェーズは「実装内容 + 評価チェックリスト」で完結させ、1 フェーズ = 1 コミットとする。

### P0: ビルド基盤(Windows / Mac 上で完結)

`AQ_PLATFORM_ANDROID` の新設と CMake の 3 分岐化。**Android 実機は不要**で、
NDK ツールチェーンで**コンパイル/リンクが通る**ところまで。プラットフォーム実装は
Null(何もしない `IPlatform`)で構わない。

- `PlatformDefs.h` に `AQ_PLATFORM_ANDROID`
- `aq_apply_platform_definitions` の Android 分岐
- `aqEngine/CMakeLists.txt` の除外を 3 分岐化
- `Game/CMakeLists.txt` の `add_library(Game SHARED)` 分岐
- `CMakePresets.json` に `android-arm64-debug` / `-release`
- ThirdParty の通し確認(§2.5)

**評価チェックリスト**
- [x] `cmake --preset android-arm64` が configure できる
- [x] `libGame.so`(arm64-v8a)がリンクまで通る — ELF64 / AArch64 / DYN を `llvm-readelf` で確認
- [x] Windows 3 構成(D3D11 / D3D12 / Vulkan)の Debug が従来どおりビルドできる(回帰なし)
- [x] 警告が Windows 構成で増えていない — 3 構成とも 54 件(従来と同数)。Android Debug は 23 件
- [x] Windows で起動〜終了コード 0・`startup_timing.log` 出力を確認(`StartupLog` 一本化の確認)

**P0 で分かったこと / 設計からの差分**

1. **NDK のパスの空白が致命的**(§2.4)。Ninja の 8.3 短縮名で clang が C ドライバとして
   リンクし libc++ が入らない。コンパイルは全て通るのでリンクエラーだけが出る。
   ルート `CMakeLists.txt` に検出を追加した。
2. **`PAGE_SIZE` がマクロ衝突**。bionic の `<bits/page_size.h>` が同名マクロ(4096)を
   持ち、`RenderCommandList` の `static constexpr size_t PAGE_SIZE` が壊れた。
   `COMMAND_PAGE_SIZE` へ改名(全 3 プラットフォーム共通の改名)。
3. **`StartupLog` を `aq.cpp` へ一本化**。UWP 以外は「`StartupMark` へ流すだけ」の同一実装が
   `PlatformWin32.cpp` / `PlatformMac.mm` に重複していた。Android 用に 3 つ目を書くより
   既定実装を 1 箇所に置き、UWP だけが上書きする形にした。
4. **§4.1(Vulkan サーフェス分岐)を P0 に前倒し**。無いとコンパイルが通らない。
5. **`DebugOutputAndroid.cpp` を P0 で追加**(P1 の項目だったが、無いとリンクできない)。
6. **プラットフォーム選択ヘッダのゲートを Android 対応**: `KeyboardMouseBackend.h` /
   `PadBackend.h` / `SoundBackend.h` / `CompressedDecoder.h` / `PlatformBudget.h` は
   未知プラットフォームで `#error` になるため、それぞれ Null 実装 / Android プロファイルを
   足した(`SoundBackend.h` の Android は Oboe 未実装なので `SOUND_BACKEND_NULL`)。
7. **`DebugOutputAndroid.cpp` は `Engine.vcxproj` に登録しない**。`DebugOutputMac.cpp` と
   同じ扱いで、非 Windows のプラットフォーム実装は CMake 経路だけが拾う。

### P1: 実機でクリア画面

Gradle プロジェクト + `PlatformAndroid` + Android サーフェス。

- `DirectX/Android/` の Gradle 一式、`AndroidManifest.xml`
- `Platform/Android/`(`PlatformAndroid` / `AndroidApp`)
- `Game/Application/AndroidMain.cpp`(`android_main`)
- 検証レイヤの同梱(Debug)
- (`DebugOutputAndroid.cpp` と Vulkan サーフェス分岐は P0 で済み)

**評価チェックリスト**
- [ ] APK が実機にインストールでき、起動する
- [ ] logcat に起動マークが出る
- [ ] Vulkan のクリア色が画面に出る
- [ ] validation エラー 0
- [ ] §4.2 のフィーチャ確認結果を本書へ追記した

### P2: 実機で実シーン

アセットとシェーダを APK から読ませる。

- `.spv` の APK 同梱
- アセットの展開(§7 案 a)+ `GetContentRoot`
- `Resource` / `ImageLoader` の実機動作確認

**評価チェックリスト**
- [ ] タイトル画面が表示される
- [ ] ステージが描画される(路面・キャラ・地形・影・UI)
- [ ] validation エラー 0
- [ ] 起動時間とアセット展開時間を計測して記録した

### P3: ライフサイクルと回転

Android 固有の最重要フェーズ。

- `IPlatform` のライフサイクル IF 拡張(`IsRenderable` / サーフェス通知)
- `IGraphicsDeviceImpl::RecreateSurface`(Vulkan 実装、他は no-op)
- `vkAcquireNextImageKHR` / `vkQueuePresentKHR` の戻り値処理と再生成
- `preTransform` の考慮

**評価チェックリスト**
- [ ] ホームに戻る → 復帰、を 10 回繰り返して落ちない
- [ ] 画面回転で落ちず、正しい向きで描画される
- [ ] バックグラウンド中に描画・present していない(logcat / プロファイラで確認)
- [ ] Windows でウィンドウリサイズしても描画が壊れない(共通コード改修の回帰)
- [ ] Windows 3 構成 + Mac の回帰確認

### P4: 入力

- `ITouchBackend` の新設と Android 実装
- 仮想パッド(§5.2 案 a)または決定した方式
- `AndroidPadBackend`
- `imgui_impl_android`

**評価チェックリスト**
- [ ] タッチでタイトルからステージへ進める
- [ ] 仮想パッドでキャラが操作できる
- [ ] 物理コントローラが接続時に使える(実機がある場合)
- [ ] デバッグ UI が指で操作できる

### P5: サウンド

- `OboeSoundBackend` 実装、Oboe の取り込み
- `SoundBackend.h` の `#error` 除去

**評価チェックリスト**
- [ ] BGM / SE が実機で鳴る
- [ ] 3D 音響の定位が Windows と一致する
- [ ] サスペンド→復帰で音が壊れない・二重再生しない
- [ ] グリッチ(バッファアンダーラン)が発生していない

### P6: 性能とパッケージング

- 解像度スケール、Bloom/トーンマップの軽量パス
- フレームペーシング(Swappy)の検討
- ThreadPool のコア割り当て、メモリ予算
- リリースビルド / 署名 / ABI 分割

**評価チェックリスト**
- [ ] 目標フレームレートを実機で達成(目標値は着手時に決める)
- [ ] サーマルスロットリング後も破綻しない
- [ ] メモリ使用量が Android のプロファイル内に収まる
- [ ] Release APK が署名付きでインストールでき動作する

---

## 10. チェックポイント(設計全体)

- [x] §0.4 の未決事項 4 点についてユーザーの判断を得た(2026-09-12)
- [x] `AQ_PLATFORM_ANDROID` が「ちょうど 1 つ」の制約を壊していない — 4 プラットフォームの
      排他チェックを更新し、Windows 3 構成 + Android がビルドできることで確認
- [ ] Android 固有の型(`ANativeWindow` / `AAsset` / `android_app`)が `Platform/Android/` と
      `Graphics/Vulkan/` の外に現れない — P0 時点では `Graphics/Vulkan` の
      `ANativeWindow*` キャスト 1 箇所のみ。P1 以降も維持する
- [ ] `IPlatform` / `IGraphicsDeviceImpl` への追加 IF が、Windows / Mac の既定実装で no-op として成立する
- [x] Android 対応のために Windows / Mac の挙動を変えていない — Windows 3 構成は
      エラー 0 / 警告 54(従来と同数)・起動と終了コード 0。**Mac は未ビルド**
      (`StartupLog` 一本化が `PlatformMac.mm` に及ぶため次に Mac を触るとき要確認)
- [x] 同じ内容を [Sound設計.md](Sound設計.md) / [Mac移植設計.md](Mac移植設計.md) と二重に書いていない
- [ ] 各フェーズのチェックリストが実機で確認可能な粒度になっている

---

## 参考

- [C++ features deprecated or removed from Visual Studio](https://learn.microsoft.com/en-us/cpp/porting/features-deprecated-in-visual-studio?view=msvc-170)
  — VS 2026 で `Mobile development with C++` が非サポートになる旨
- [Cross-platform mobile development with C++](https://learn.microsoft.com/en-us/cpp/cross-platform/visual-cpp-for-cross-platform-mobile-development?view=msvc-170)
- [Native and proprietary engines / Vulkan(Android Developers)](https://developer.android.com/games/develop/vulkan/native-engine-support)
  — Vulkan 1.1 ベースライン推奨、Vulkan 1.3 は Android 13(API 33)以降、Android Baseline Profile
