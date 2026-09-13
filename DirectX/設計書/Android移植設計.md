# Android 移植 設計

> 対象コミット: d1e7f6a / 最終更新: 2026-09-12

## 現在の到達点

**P2(実機で実シーン)完了 — ただしステージ描画の確認は入力待ち。**
Pixel 7a / Android 16 で **AquaDash のタイトル画面がフル表示**され、60 FPS で回る。
ロゴ・背景・ステージサムネ・フォントすべて正しく出る。
「PRESS SPACE / A BUTTON」から先へ進むには入力が要るため、**ステージ描画の確認は P4 へ移した**。

| | 状態 |
|---|---|
| Android ビルド | `cmake --preset android-arm64` → `--build --preset android-arm64-debug` が通る。エラー 0 |
| 成果物 | `build/android-arm64/lib/Debug/libGame.so`(ELF64 / AArch64 / DYN)。LOAD セグメントは 16KB(0x4000)境界 |
| APK | `build/android-arm64/AquaDash-debug.apk`(97.85MB / v3 署名 / アセット 192 ファイル同梱) |
| 実機 | **Google Pixel 7a(lynx)/ Android 16(API 36)/ arm64-v8a / Mali**。Vulkan は **1.4** |
| アセット展開 | 初回のみ **192 ファイル / 101,394,210 バイトを 413 ms**。2 回目以降はスタンプ比較で即スキップ |
| 起動 | `android_main` → ウィンドウ 2282x1080 取得 27ms → Vulkan デバイス 69ms → **タイトル表示 0.52s** |
| 画面 | タイトル画面が正しく描画(ロゴ / TitleBG.png / Stage01_thumb.png / フォントアトラス / ImGui) |
| 性能 | **60.1 FPS(16.64 ms)** — VSync 上限に張り付き |
| 終了 | BACK キーで `TERM_WINDOW` → `DESTROY` → `MemoryTracker: No leaks detected`。クラッシュなし |
| 入力(P4) | **実機確認済**。仮想 A でタイトル→ステージへ進み、スティックで 298 km/h まで加速・コイン取得を確認 |
| ライフサイクル(P3) | **実機確認済**。サスペンド/復帰 20 回・4 方向回転・寸法変更すべて無事。バックグラウンド中の CPU は 0 tick |
| ステージ描画 | **実機確認済**(P2 から保留の項目)。路面・キャラ・地形・草 180 万本・影・HUD・ミニマップすべて表示 |
| ステージ性能 | **26〜27 FPS(約 37 ms)**。タイトルは 60 FPS。タイラー GPU 向けの最適化は P6 の課題 |
| 終了時リーク | **解消済**。リークではなく報告位置の問題だった(別コミットで修正。現在は `No leaks detected`) |
| サウンド(P5) | **実機確認済**。AAudio で BGM / SE が鳴り、背面では `state:paused` になる。48kHz / float32 / 2ch |
| Windows 回帰 | D3D11 / D3D12 / Vulkan の Debug すべてエラー 0・警告 54 件(従来と同数)。P2 時点では実行もタイトル表示〜終了コード 0 |
| Mac 回帰 | **未確認**(この環境では Mac をビルドできない)。`StartupLog` 一本化と `ImageLoader` の変更が Mac に及ぶので次に Mac を触るとき要確認 |
| validation | **未確認**。検証レイヤの `.so` がどこにも無く同梱できない(§4.6) |

導入手順とハマりどころは [Tools/SetupCMake/README.md](../Tools/SetupCMake/README.md) §6、
APK 化と実機投入は [Tools/PackageApk/README.md](../Tools/PackageApk/README.md) が正本。

対象: `aqEngine/` + `Game/`。既存の Vulkan バックエンドを Android(NDK)で動かし、実機で
AquaDash が起動〜プレイできる状態までを設計する。

姉妹文書:
- [Mac移植設計.md](Mac移植設計.md) / [Mac移植調査.md](Mac移植調査.md) — 非 Windows 化の土台。
  CMake・`AQ_PLATFORM_*`・`IPlatform`・入力/サウンド抽象・事前 `.spv` 生成は**すべてここで導入済**。
  本書はその差分として Android を足す。
- [VulkanBackend設計.md](VulkanBackend設計.md) — Vulkan バックエンドの一次資料。
- [Xbox移植設計.md](Xbox移植設計.md) — `IPlatform` 抽象の導入元。
- [Sound設計.md](Sound設計.md) §8 — Oboe バックエンドの一次資料(本書では重複させない)。

**本書のステータス: P3 / P4 / P5 まで完了(2026-09-13)。次は P6(性能とパッケージング)。**
P5b は実装完了だが、**イヤホン抜き差しの確認だけ手が要るため未消化**(§P5b)。
§0.4 の判断は 2026-09-12 に確定した。`preTransform` 対応は P2 で先に入った(§4.3)。
実機で**ステージのプレイまで到達**した。残る既知の不具合は終了時のリーク 70 件(P3 とは無関係の既存不具合)。

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

### 2.3 ターゲット種別と APK 化 — Gradle は使わない(P1 で変更)

Android アプリの実行主体は Java/Kotlin 側の Activity で、ネイティブは `.so` として読み込まれる。
ターゲット種別は Android のみ共有ライブラリ(`libGame.so`)。ここは当初設計どおり。

**APK 化は Gradle ではなく Android SDK の build-tools を直接叩く**方式に変えた。理由:

- この開発機に **Gradle が入っていない**。Gradle 本体と Android Gradle Plugin・その依存を
  Maven から取得する必要があり、ネットワークに依存したビルドになる。
- アプリは **Java コードを 1 行も持たない**(`NativeActivity` は framework のクラス)。
  マニフェストに `android:hasCode="false"` を付ければ `classes.dex` すら要らないので、
  Gradle が担う仕事は「aapt2 + zipalign + apksigner を順に呼ぶ」だけになる。
  そのために Gradle を導入するのは釣り合わない。
- CMake ビルドは Gradle からも `externalNativeBuild` でそのまま呼べる形のままなので、
  **後から Gradle を足すのは加算的**(Android Studio でネイティブデバッグしたくなった
  時点で導入すればよい)。

```
DirectX/Android/
└─ AndroidManifest.xml           NativeActivity / android.app.lib_name = Game

DirectX/Tools/PackageApk/
├─ package_apk.ps1               aapt2 link → lib/<abi>/*.so 追加 → zipalign → apksigner
└─ README.md
```

置き場所を `DirectX/` 配下にするのは、既存の `.sln` / vcxproj と干渉させないため。

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

| ファイル | 責務 | 状態 |
|---|---|---|
| `aqEngine/Platform/Android/PlatformAndroid.h/.cpp` | `IPlatform` 実装。`ANativeWindow*` の保持、`ALooper` によるイベントポンプ、`APP_CMD_*` の受け取り、`GetContentRoot` / `GetUserDataDirectory` | P1 で実装 |
| `aqEngine/Platform/Common/DebugOutputAndroid.cpp` | `__android_log_write` による `DebugOutput` 実装(タグ `AquaDash`) | P0 で実装 |
| `Game/Application/AndroidMain.cpp` | `android_main`。`Main.cpp` / `MacMain.mm` と同型のブートストラップ | P1 で実装 |
| native_app_glue(NDK 同梱ソース) | `ANativeActivity_onCreate` の提供と `android_main` の呼び出し。CMake で独立した静的ライブラリにする | P1 で配線 |

**`AndroidApp` 層は作らなかった。** 当初は「glue のコールバックを受けて `PlatformAndroid` へ
橋渡しする層」を分ける設計だったが、実際に必要だったのは
`app->userData` に `this` を積んで C 関数から転送するサンク 1 つだけで、
層を増やすほどの中身が無かった。Android の型は `PlatformAndroid.cpp` に閉じており、
ヘッダには `struct android_app;` / `struct ANativeWindow;` の前方宣言しか出していない。

**`ANativeActivity_onCreate` のリンク落ちに注意。** native_app_glue は静的ライブラリで、
この関数をアプリ側の誰も参照しないためリンカがアーカイブメンバごと捨ててしまう。
捨てられると NativeActivity がエントリを見つけられず起動時に落ちる。
`Game` のリンクオプションに `-u ANativeActivity_onCreate` を入れて取り込ませている。

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

**`NativeActivity` を採る(P1 で決定)。** 当初は `GameActivity` を推していたが、
`GameActivity` は `androidx.games:games-activity` という **Maven 依存 = Gradle 必須**で、
この開発機には Gradle が入っておらずネットワーク取得にも依存したくない(§2.3)。
`NativeActivity` + native_app_glue は **NDK に同梱のソースだけで完結**する。

タッチ・ゲームパッドは `AInputEvent` から自前で取れるため、§5.2 の仮想パッド設計
(`ITouchBackend` → `VirtualPadBackend` → `CompositePadBackend`)には `GameActivity` は要らない。
`GameActivity` の利点が残るのはテキスト入力と機種差の正規化(Paddleboat)で、
P4 でそこが痛くなったら移行を検討する(エントリは `android_main` のままなので、
移行時の差分は glue の差し替えに収まる)。

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

P1 の実機確認結果(Pixel 7a / Android 16 / Mali。`adb shell cmd gpu vkjson` と実起動):

- [x] `apiVersion` = **4210688 (0x00404000) = Vulkan 1.4**。要求している 1.3 に余裕で足りる
- [x] `dynamicRendering` = 1 / `synchronization2` = 1 / `scalarBlockLayout` = 1(いずれも vkjson で確認)
- [x] `samplerAnisotropy` — デバイス生成時に有効化しており、生成が成功しているので対応
- [x] `R16G16B16A16_SFLOAT` の `COLOR_ATTACHMENT` + `STORAGE` — HDR メイン RT の生成が成功
- [x] `vkCmdBlitImage2` — `CopyToBackBuffer` が実際に走ってフレームが提示されている
- [x] スワップチェーンのサーフェスフォーマット — 選択が通り提示成功
- [ ] 負のビューポート高さ(Y-flip)— クリアのみの経路では踏まない。P2 で実シーンを描いて確認
- [ ] `D32_SFLOAT` の depth attachment + サンプル — 同上(P2)

**副産物**: `dynamicRenderingLocalRead` = 1 も報告された。タイラー GPU で
サブパス相当のオンチップ読み出しができるため、P6 の帯域対策の候補になる。

### 4.3 スワップチェーン再生成と画面回転 ★既存実装の欠落

現在 [VulkanGraphicsDeviceImpl.cpp](../aqEngine/Graphics/Vulkan/VulkanGraphicsDeviceImpl.cpp) は
`vkAcquireNextImageKHR` / `vkQueuePresentKHR` の**戻り値を確認していない**。
デスクトップではウィンドウサイズ固定運用のため表面化していないが、Android では回転・復帰・
マルチウィンドウで日常的に `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` が返る。

- 戻り値を判定し、`OUT_OF_DATE` / `SUBOPTIMAL` でスワップチェーンを作り直す経路を新設する(P3)。
- Android は起動直後のサーフェスサイズと実サイズがずれることがあるため、
  レンダー解像度は `currentExtent` から取り直す(P3)。

**プリトランスフォーム(回転)は P2 で対応済み。** 当初は「`currentTransform` をそのまま
`preTransform` に渡し、90/270 度は射影行列で吸収する」計画だったが、**この方法では直らない**
ことが実機で分かった。実測では Pixel 7a の横向きで `currentTransform = 0x2`
(`ROTATE_90`)で、これをそのまま渡すと「アプリが回転済みの絵を描く」契約になり、
**画面全体が 90 度回って表示される**(2D UI と ImGui はスクリーン座標で描くため、
射影行列を回しても直らない。UI 層全部に手を入れる話になる)。

そこで **`supportedTransforms` に IDENTITY があれば IDENTITY を要求**し、回転は
コンポジタに任せる形にした。合成が 1 回増えるのでモバイルでは帯域を食うが、正しさを優先した。
事前回転して合成コストを無くすのは P6 の最適化候補(`dynamicRenderingLocalRead` も使える)。
デバッグのため `[vk] swapchain <w>x<h> currentTransform=0x? -> preTransform=0x?` をログに出している。

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

- **検証レイヤは NDK に同梱されていない**(P1 の実測)。NDK r27c / r23c・Android SDK・
  Windows 版 Vulkan SDK を全て再帰検索しても `libVkLayer_khronos_validation.so` は 1 つも無い
  (新しい NDK は同梱をやめており、Windows 版 Vulkan SDK には `.dll` しか入っていない)。
  入手は Vulkan-ValidationLayers の android-binaries リリースからになる(ネットワーク取得)。
  **そのため P1 の「validation エラー 0」は未確認のまま。**
- 置き場所は分かっている。実機の logcat に
  `vulkan: searching for layers in '<apk>!/lib/arm64-v8a'` が出ており、
  **APK の `lib/<abi>/` に置けばローダが拾う**。`package_apk.ps1` に
  `-ValidationLayerPath` を渡せば同梱できるので、`.so` を用意すれば有効化できる。
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
| `ITouchBackend`(新設) | 生のタッチ点の配列(id / 座標 / 押下)を供給する抽象。プラットフォーム実装が書き込み、上位は読むだけ。座標は **Mouse の `cursorX/Y` と揃えてクライアント左上原点のピクセル** |
| `VirtualPadBackend`(新設・`IPadBackend` 実装) | 画面上の仮想スティック/ボタンの当たり判定を経てパッド状態へ変換する。プラットフォーム非依存 |
| `CompositePadBackend`(新設・`IPadBackend` 実装) | `VirtualPadBackend` と物理パッド実装を束ね、1 つのパッドとして見せる。Android の `DefaultPadBackend` はこれ |

**合成規則は「ボタンは OR / 軸は絶対値の大きい方 / いずれか接続なら接続」**の無状態合成にした
(当初案の「先に入力があった側を採用」は状態を持つぶん挙動が読みにくく、
物理と仮想を同時に触ったときの結果が説明しづらいため)。

**`VirtualPadBackend` は `ITouchBackend` を直に叩かず、取り込み済みの `const TouchState*` を読む。**
タッチの取得は「取得までに一度でも押されたら押下として返す」ラッチを持ち(イベント駆動だと
1 フレーム内で押して離された入力を取りこぼすため。Mac の `CocoaInputSink` と同じ処置)、
**このラッチは取得で消費される**。`InputManager::Update` が毎フレーム 1 回だけ取り込み、
UI のタップ判定と仮想パッドが同じ値を読む形にすれば、二重フェッチが構造的に起きない。

**スティックを掴んだ指はボタン判定から除外する。** 掴んだ指は円の外へ出ても離すまで追従する
(縁で掴みが外れると全開に倒したところで操作が切れる)ため、通りがかったボタンを
誤爆させないようにしている。

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

方針は 2 案あり、**(a) 初回起動時に展開**を採った(P2 で実装)。

| 案 | 内容 | 長所 | 短所 |
|---|---|---|---|
| **(a) 初回起動時に展開**【採用】 | APK の `assets/` を `internalDataPath` へコピーし、`GetContentRoot` にそのパスを返す | **既存のファイル IO を一切変えなくてよい** | 起動時間とストレージを二重に消費 |
| (b) `AAssetManager` 経路 | ファイル読み込みを抽象化し、Android では `AAsset_*` で読む | ストレージ効率が良い | `Resource` / `ImageLoader` / `VulkanShader` のパス前提に手が入る |

(b) へ移すなら抽象化の単位は「パス → バイト列」の 1 関数に絞る(`ImageLoader` が
DirectXTex にパスを渡している箇所だけはメモリからのロード API に差し替えが要る)。

### 7.1 展開の仕組み(P2 実装)

**`AAssetManager` はディレクトリ列挙ができない**(`AAssetDir` はサブディレクトリを返さない)。
そのため「APK の中身を全部コピーする」ことが素直には書けない。**ファイル一覧を
パッケージング時に APK へ同梱する**ことで解決した。

| APK 内 | 役割 |
|---|---|
| `assets/Game/Assets/...` | アセット本体。**ソースツリー相対の構造をそのまま再現**する(下記の理由) |
| `assets/asset_index.txt` | 同梱ファイルの相対パス一覧(1 行 1 ファイル、`/` 区切り、UTF-8/LF) |
| `assets/asset_stamp.txt` | `<ファイル数> <合計バイト数>`。展開済み判定に使う |

- 展開先は `<internalDataPath>/content` で、`GetContentRoot()` がこれを返す。
  結果として `<content>/Game/Assets/...` が並ぶ。
  **この形にするのは既存のパス解決規則に合わせるため**: `Resource.cpp` と
  `VulkanShader.cpp` / `D3D12Shader.cpp` は `Assets/...` を `<root>/Game/Assets/...`、
  `Game/Assets/...` を `<root>/Game/Assets/...` と解決する(UWP も同じ規則で同梱している)。
- 展開済み判定は **APK 内の `asset_stamp.txt` と展開先に置いた同名ファイルの文字列比較**。
  APK を入れ替えると印が変わるので自動で作り直される。
- 印が違ったときは `remove_all` で展開先を消してから入れ直す(消えたファイルが残らないように)。
- **印は全ファイルのコピーが終わった後に書く**。途中で落ちたら印が無いので次回やり直しになる。
- 展開のトリガは `GetContentRoot()` の初回呼び出し 1 箇所に寄せてある(誰が最初に呼んでも成立する)。
  ただし数秒かかる処理を初期化の途中で黙って走らせると原因が分からなくなるため、
  `AndroidMain` は起動直後に明示的に 1 回呼んで印をログへ残す。

**`VulkanShader.cpp` の修正が必要だった。** `Resource.cpp` と `D3D12Shader.cpp` は
`Engine::GetContentRoot()` を見ていたが、`VulkanShader.cpp` の `FindProjectRoot()` だけは
カレントディレクトリからの上方探索しか持っておらず、Android ではシェーダが見つからない。
D3D12 側と同じ形(コンテンツ基点を優先 + スレッドセーフな static 初期化)に揃えた。
Mac の Vulkan 構成が動いていたのは `.app` が CWD を `Game/` に移していたため。

その他:

- セーブデータ・ログの書き込み先は `internalDataPath`。`PlatformAndroid::GetUserDataDirectory`
  が末尾セパレータ付きで返す(P1 で実装済み)。カレントディレクトリ前提の箇所を洗う。
- **Windows の `aapt2 -A` はサブディレクトリの区切りを `\` のまま zip エントリ名にする**
  (`assets/spv\Foo.spv` のようになる)。`AAssetManager` は `/` 区切りで引くため、
  `Assets/` をサブフォルダ込みで入れる P2 では **aapt2 の `-A` に頼らず自前で zip へ
  追加する**か、フラットに並べる必要がある(P1 のパッケージング実装で判明)。
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
7. **ディスプレイカットアウト**。Pixel 7a の横向きでは画面左端に 118px の黒帯が出る
   (2400px のうち ANativeWindow は 2282px)。フロントカメラのカットアウト領域を
   避けているためで不具合ではない。全画面に広げるならマニフェストのテーマへ
   `windowLayoutInDisplayCutoutMode="shortEdges"` を入れるが、UI がカメラ穴に
   隠れる可能性とのトレードオフ。見た目の詰めとして P6 で判断する。
8. **UWP の JSON 読み込み**。P2 で直した `JsonParser::ParseFile` の CWD 依存は
   Xbox(UWP)で既知だった「JSON 全滅で画面がグレー」と同じ原因。Xbox 実機での
   確認は [Xbox移植設計.md](Xbox移植設計.md) 側の作業として残っている。

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

- `DirectX/Android/AndroidManifest.xml`(NativeActivity / Vulkan 1.3 要求 / 横向き固定)
- `Tools/PackageApk/`(aapt2 → zipalign → apksigner の APK 化。Gradle は使わない)
- `Platform/Android/PlatformAndroid`(ウィンドウ待ち・イベントポンプ・ユーザーデータ基点)
- `Game/Application/AndroidMain.cpp`(`android_main`)
- native_app_glue の CMake 配線と `-u ANativeActivity_onCreate`
- 検証レイヤの同梱(Debug)
- (`DebugOutputAndroid.cpp` と Vulkan サーフェス分岐は P0 で済み)

**`AndroidMain.cpp` は P1 では Engine を起動しない。** `Engine::Initialize` はシェーダと
テクスチャの読み込みを伴い、APK 内の assets は通常のファイルパスでは開けないため、
アセット経路が入る P2 までゲーム本体は動かせない。P1 はグラフィクスデバイスだけを立てて
クリア色を提示し、**プラットフォーム層 → ANativeWindow → Vulkan サーフェス/スワップチェーン/提示**
が実機で通ることに検証対象を絞る。この足場は P2 で Engine ブートに置き換える
(`Main.cpp` / `MacMain.mm` と同型になる)。

**バックグラウンド復帰は P1 では未対応。** ウィンドウを失ったら描画を止めるだけで、
サーフェスの作り直しはしていない(P3)。ホームに戻す操作は P3 まで想定外。

**評価チェックリスト**
- [x] `libGame.so` に `ANativeActivity_onCreate` と `android_main` が GLOBAL で公開されている
- [x] APK が実機にインストールでき、起動する(Pixel 7a / Android 16。`adb install -r` → Success)
- [x] logcat に起動マークが出る(`adb logcat -s AquaDash`)
- [x] Vulkan のクリア色が画面に出る(`screencap` を 25 点サンプルして全面 RGB(25,89,153)。指定値と一致)
- [ ] validation エラー 0 ← **検証レイヤの `.so` が入手できず未確認**(§4.6)
- [x] §4.2 のフィーチャ確認結果を本書へ追記した
- [x] 終了処理がクリーン(`MemoryTracker: No leaks detected` / クラッシュなし)

**P1 で分かったこと / 設計からの差分**

1. **16KB ページ境界が必須**。Android 15 以降は `.so` の LOAD セグメントが 16KB 境界に
   揃っていないと互換性警告が出る(Android 16 実機で「ELF のアライメント チェックに失敗」
   ダイアログが表示された。動作自体はする)。NDK r27c は既定で 4KB(0x1000)なので、
   プリセットに `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` を追加して
   `-Wl,-z,max-page-size=16384` を付け、0x4000 に揃えた。
   **リンカフラグはビルドツリー初回 configure でキャッシュに焼かれる**ため、
   この変更を入れたときは `build/android-arm64` を作り直す必要がある。
2. **`StartupMark` が Android では何も出していなかった**。出力先がカレントディレクトリの
   ファイルと `OutputDebugStringA` だけで、Android は CWD が `/` で書けず標準出力も
   どこにも出ない。`aq::debug::OutputString`(= logcat)へ流すよう `aq.cpp` を直した。
   これが無いと起動の到達点が一切見えない。
3. **Gradle を使わない**(§2.3)。**`NativeActivity` を採用**(§3.2)。**`AndroidApp` 層は作らなかった**(§3.1)。
4. `uses-feature android.hardware.vulkan.version required="true"` を入れてあるので、
   Vulkan 1.3 非対応端末ではインストール段階で弾かれる。動かない報告が来たら最初にここを疑う。
5. APK にアイコンとリソースが無いため、ランチャーには既定アイコンで並ぶ(起動には影響しない)。

### P2: 実機で実シーン

アセットとシェーダを APK から読ませ、`AndroidMain` を本物の Engine ブートへ差し替えた。

- `Game/Assets` 一式(`.spv` 61 本を含む 192 ファイル)を APK へ同梱
- アセットの展開(§7.1)+ `GetContentRoot`
- `AndroidMain` を `Main.cpp` / `MacMain.mm` と同型の Engine ブートに
- 実機で露出した既存バグ 3 件の修正(下記)

**評価チェックリスト**
- [x] タイトル画面が表示される(ロゴ / 背景 / ステージサムネ / フォント / ImGui すべて正常)
- [x] 起動時間とアセット展開時間を計測して記録した(展開 413ms / タイトル表示 0.52s / 60.1 FPS)
- [ ] validation エラー 0 ← **検証レイヤの `.so` が入手できず未確認**(§4.6)
- [ ] ステージが描画される(路面・キャラ・地形・影・UI)← **P4 へ移した**。
      タイトルの「PRESS SPACE / A BUTTON」から先へ進むには入力が必要で、
      Android の入力は P4 で実装するため。デバッグ用の自動開始を仕込むより、
      P4 で実際に操作して確認するほうが確実と判断した

**P2 で分かったこと / 実機でしか出なかった既存バグ 3 件**

いずれも「Windows と Mac ではカレントディレクトリが `Game/` になっているため偶然動いていた」
という同根の問題。Android は CWD が `/` で、アプリが変更できない。

1. **`JsonParser::ParseFile` が CWD 相対だった。** 与えられたパスをそのまま `ifstream` に
   渡していたため、Android では JSON が 1 つも読めない。Prefab / Level / UI 画面定義 /
   AudioBank / Particle がすべて JSON なので、**UI が何も出ない**症状になる。
   `aq::res::ResolveExistingResourcePath` を通すよう修正した。
   **これは UWP(Xbox)で既知だった「JSON 全滅で画面がグレー」と同じ原因**で、
   そちらも同時に直るはず(Xbox 実機での確認は別途)。
2. **PNG / JPG ローダが Android に配線されていなかった。** `ImageLoader.cpp` の
   stb_image 経路が `AQ_PLATFORM_MAC` でガードされており、Android は `#else` で
   常に false を返していた。`!AQ_PLATFORM_WINDOWS_FAMILY` に広げた。
3. **stb_image へ解決前のパスを渡していた。** 1 と同じ理由で Android では開けない。
   DirectXTex 側は解決済みパスを渡していたので DDS だけが読めていた
   (空のキューブマップは出るのにタイトル画像とフォントが出ない、という症状)。
   併せて **失敗時のログが無かったので追加した**(WIC 側にはあった)。
   テクスチャ無しで進むと「文字がベタ塊になる」だけが症状として出て原因が追えない。

**副作用: 起動が速くなった。** 修正前はタイトル表示までの `[boot] wait` が 15 秒
(896 フレーム)かかっていた。これはテクスチャのプリロード待ちがタイムアウトしていた
ためで、テクスチャが読めるようになったら **0.07 秒(2 フレーム)**になった。

**アセット展開は速い。** 101MB / 192 ファイルで 413ms(内部ストレージが速い)。
起動時間への影響は初回のみで、体感できるほどではなかった。

### P3: ライフサイクルと回転 — 実装完了・実機未確認

Android 固有の最重要フェーズ。

| 追加/変更したもの | 置き場所 |
|---|---|
| `IsRenderable()` / `ConsumeSurfaceChanged()` | `Platform/IPlatform.h`(既定は「常に描ける・差し替え無し」) |
| `RecreateSurface()` / `GetSurfaceSize()` | `Graphics/IGraphicsDeviceImpl.h`(既定 no-op)+ `GraphicsDevice` の委譲 |
| Vulkan のサーフェス/スワップチェーン再生成 | `VulkanGraphicsDeviceImpl::RecreateSurface` / `DestroySwapchainResources` |
| 取得・提示の戻り値処理 | `AcquireNextImage()` と `Present()` |
| フレームスキップと再生成の起点 | `Engine::RunGame` / `EnsureSurfaceUpToDate` / `SyncScreenSize` |
| 窓の差し替え通知 | `PlatformAndroid` の `surfaceChanged_`(`INIT_WINDOW` / `WINDOW_RESIZED` / `CONFIG_CHANGED` で立つラッチ) |
| CPU・GPU 完了待ちの共通入口 | `IApplication::WaitForRenderIdle()`(既定 no-op。`Application` が override) |

**設計から動いた点**

1. **`SUBOPTIMAL` では作り直さない(寸法が変わったときだけ)。** §4.3 のとおり
   `preTransform` に IDENTITY を要求しているため、Android では回転していなくても
   `vkAcquireNextImageKHR` / `vkQueuePresentKHR` が **常に `VK_SUBOPTIMAL_KHR` を返し続ける**。
   素直に「SUBOPTIMAL なら再生成」と書くと毎フレーム作り直して描画が進まない。
   `caps.currentExtent` と現在のスワップチェーン寸法を比べ、ずれているときだけ作り直す。
2. **`CreateSwapchain` は「新しい方が通ってから古い方を畳む」形にした。** `oldSwapchain` を
   引き継ぎ、生成に失敗しても直前のスワップチェーンが有効なまま残る。バックグラウンドで
   `currentExtent` が 0x0 になる瞬間に踏んでも破綻しない。
3. **画像が取れなくてもコマンドバッファは必ず開く。** `RenderContext` はあらゆる記録の先頭で
   `BeginFrameIfNeeded()` を呼ぶので、ここで早期 return すると未記録状態のバッファへ `vkCmd` を
   積むことになる。取得に失敗したフレームは `imageAcquired_ = false` として
   **swapchain への遷移・ブリット・present だけを飛ばし**、submit とフェンスの signal は行う
   (submit を飛ばすとフェンスが未シグナルのまま残り、次フレームの待ちで止まる)。
4. **サーフェス再生成の失敗は Engine 側で再試行する。** サーフェスを捨てる経路だけは 2 の保険が
   効かないため、`RecreateSurface` が false の間 `Engine` は `surfaceDirty_` を落とさず
   フレームを飛ばして次のループで作り直しに再挑戦する。
5. **スクリーンサイズを提示面へ追従させた(設計外)。** ImGui の `DisplaySize` と仮想パッドの
   当たり判定が `Engine::GetScreenWidth/Height` を見ており、回転すると初期化時の値とずれる。
   `GetSurfaceSize()` を足して毎フレーム同期する。**レンダー解像度(オフスクリーン RT)は据え置き**
   なので、回転すると縦横比が合わず引き伸ばされる。RT の作り直しは P6 の課題とする。
6. **`APP_CMD_WINDOW_RESIZED` / `APP_CMD_CONFIG_CHANGED` も再生成の起点にした。** 回転では
   ウィンドウが同じまま寸法だけ変わり `TERM/INIT_WINDOW` が来ないため。
   戻り値経由でも拾えるが、OS から知らされる方が確実。

**評価チェックリスト**(実機確認 2026-09-13 / Pixel 7a・Android 16)
- [x] Android ビルドがエラー 0(新規警告なし。残る 24 件はすべて既存)
- [x] Windows 3 構成(D3D11 / D3D12 / Vulkan)がエラー 0・警告 54 件で従来と同数
- [x] ホームに戻る → 復帰、を 10 回繰り返して落ちない(タイトルで 10 回 + ステージ中で 10 回 = 計 20 回。
      PID 不変、`TERM_WINDOW` → `INIT_WINDOW` → スワップチェーン再生成が 1:1 で走り、失敗 0)
- [x] 画面回転で落ちず、正しい向きで描画される(下記「回転の実機確認」)
- [x] バックグラウンド中に描画・present していない
      (`/proc/<pid>/stat` の utime+stime が **前面 6 秒 = 592 tick に対しバックグラウンド 6 秒 = 0 tick**。
      復帰後 579 tick。`PumpEvents` が完全にブロックしている)
- [ ] Windows でウィンドウリサイズしても描画が壊れない(共通コード改修の回帰)
- [ ] Windows 3 構成 + Mac の実行回帰確認

**回転の実機確認**。マニフェストは `screenOrientation="landscape"` で向き固定のため、
検証時だけ `fullUser` に差し替えて `settings put system user_rotation` で 4 方向を往復させた
(確認後にマニフェストは戻してある)。結果は縦横どちらも**正しい向き**で描画され 60 FPS 維持、
8 回転で再生成 15 回・失敗 0・PID 不変。ステージ表示中の回転も 4 回とも無事。
`wm size` による寸法変更でも同じ経路が走ることを確認した。

**ログが示した「両方の経路が要る」根拠**。回転時のログは次の順に出る:

```
[vk] swapchain 1080x2282 ...   ← メインスレッド。CONFIG_CHANGED で再生成したが caps はまだ旧寸法
[vk] swapchain 2282x1080 ...   ← レンダースレッド。SUBOPTIMAL + 寸法ずれ検出が新寸法を拾い直す
[vk] swapchain 2282x1080 ...   ← メインスレッドが追従して確定
```

OS 通知の時点では `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` がまだ回転前の寸法を返すことがあり、
**通知だけでは正しい寸法にならない**。戻り値処理(§4.3)が実際に効いている。

**副作用として分かったこと**

- **起動時と回転時にスワップチェーンを 2 回作っている**。起動では `Engine::Initialize` 中に溜まった
  `WINDOW_RESIZED` / `CONFIG_CHANGED` を最初の `PumpEvents` が配送するため、寸法が同じでも 1 回余分に
  作り直す(実測 +5 ms、起動 1 回きり)。回転でも上記のとおり 2 回走る。正しさには影響しないが、
  減らすなら「通知時にウィンドウ寸法が変わっていなければサーフェスまでは作り直さず、
  スワップチェーンだけ dirty にする」形にできる。
- **終了時に 70 件 / 21,872 バイトのリークが出る(P3 とは無関係の既存不具合)**。P3 の変更を
  `git stash` して HEAD のビルドで同じ手順を踏んでも**まったく同じ 70 件 / 21,872 バイト**が出るため、
  P2 の「No leaks detected」から P4 までの間に入り込んだ別件。ステージへ入らずタイトルのみで
  終了しても同数なので、ステージ/レベルの破棄経路でもない。切り分けは別途。

### P4: 入力 — 実装完了・実機未確認

| 追加したもの | 置き場所 |
|---|---|
| `ITouchBackend` / `TouchPoint` / `TouchState` | `HID/ITouchBackend.h`(プラットフォーム非依存) |
| `NullTouchBackend` / 選択ヘッダ | `HID/NullTouchBackend.h` / `HID/TouchBackend.h` |
| `VirtualPadBackend` | `HID/VirtualPadBackend.{h,cpp}`(非依存。iOS と共有する) |
| `CompositePadBackend` | `HID/CompositePadBackend.{h,cpp}`(非依存) |
| `AndroidInputSink` | `HID/Android/`。`PlatformAndroid` が投入し、バックエンドが読む(Mac の `CocoaInputSink` と同構造) |
| `AndroidTouchBackend` / `AndroidPadBackend` | `HID/Android/`。sink から取り出すだけの薄い実装 |
| 入力イベントの配線 | `PlatformAndroid` に `onInputEvent` のサンクとモーション/キーの振り分け |
| 生成の一本化 | `HID/PadBackend.h` の `CreateDefaultPadBackend(const TouchState*)`。呼び出し側に `#if` を持ち込まない |
| タッチの取り込み | `InputManager` が毎フレーム 1 回。`GetTouchState()` で UI からも読める |

- **タイトルから先へ進めるのは `UIInputSystem::IsSubmit()` が Pad 0 の A を見ているため。**
  仮想 A ボタンがそのまま submit になるので、UI 層の改修は要らなかった。
- **`imgui_impl_android` は入れていない。** ImGui の描画は動いており(P2 で確認)、
  指で ImGui を操作する必要が出たときに追加する。
- **仮想パッドの描画は暫定。** `Application.cpp` の ImGui オーバーレイに当たり判定と同じ
  円と A/B/START のラベル、触れている点を描いている。位置が見えないと操作できないため
  入れたもので、実機で操作感を詰めたら UI 層(UIObject)の正式な見た目へ置き換える。

**P4 で分かったこと**

1. **`MAX_TOUCH_COUNT` は使えない名前だった。** Windows SDK の `winuser.h` が
   `#define MAX_TOUCH_COUNT 256` を持っており、`TouchState` のメンバ宣言が
   マクロ展開で壊れて Windows ビルドが 13 エラーになった(`MAX_POINT_COUNT` へ改名)。
   Android の `PAGE_SIZE` と同じ「システムのマクロと同名」パターンだが、**向きが逆**で
   「Android 対応のために足した新規コードが Windows を壊す」形。
2. **タッチのラッチは二重フェッチに弱い。** 取得で消費されるため、同一フレームに 2 回
   取ると 2 回目が空になる。`InputManager` が 1 回だけ取り、UI と仮想パッドが
   同じ `TouchState` を読む形にして構造的に防いだ(§5.2)。
3. **ハットスイッチ対応を追加した**(設計外)。十字キーを `AKEYCODE_DPAD_*` ではなく
   `AXIS_HAT_X/Y` で送る機種があるため。キー由来のレベルとは別に持ち、取得時に OR する。
4. **キーイベントは装置ソースで絞る**(`GAMEPAD|JOYSTICK|DPAD`)。物理キーボードの矢印キーが
   十字キー扱いになってパッド接続と誤判定されるのを避けるため。
5. **`AKEYCODE_BACK` は写像しない。** BACK キーで終了できる状態を壊さないため、
   キーイベントを消費せず glue へ流す。
6. **振動は no-op。** NDK に振動 API が無く、`Vibrator` / `VibratorManager` は Java 側のみ
   (JNI が必要)。必要になった時点で追加する。
7. **`/t:Rebuild` は VS Code を起動していると失敗する**(環境の話)。cppwinrt の生成ヘッダを
   C/C++ 拡張がロックし `MSB3061` になる。増分ビルドなら通る。

**評価チェックリスト**(実機確認 2026-09-13 / Pixel 7a・Android 16)
- [x] Android ビルドがエラー 0(新規警告なし。残る 1 件は既存の `OceanDebugPanel.h`)
- [x] Windows 3 構成(D3D11 / D3D12 / Vulkan)がエラー 0・警告 54 件で従来と同数
- [x] タッチでタイトルからステージへ進める(仮想 A ボタンが `UIInputSystem::IsSubmit()` に繋がっている)
- [x] **ステージが描画される**(路面・キャラ・地形・草 180 万本・影・空・HUD・ミニマップ。P2 から保留だった項目)
- [x] ステージ描画時のフレームレートを実機で計測した → **26〜27 FPS(約 37 ms)**。タイトルは 60 FPS 張り付き。
      ステージのロードは 10.9 秒(草の散布 1,800,000 本で 3.4 秒)
- [x] 仮想パッドでキャラが操作できる(左スティックを倒して 298 km/h まで加速、コイン取得も確認)
- [ ] 仮想パッドのレイアウト(位置・大きさ)が実機の持ち方で無理なく届く ← `adb` 経由の合成タッチで
      動作は確認したが、**持ち方の評価は人の手で触らないと判断できない**ので未消化
- [ ] 物理コントローラが接続時に使える
- [ ] 仮想パッドと物理コントローラを同時に触っても破綻しない(合成規則の確認)
- [ ] デバッグ UI が指で操作できる(`imgui_impl_android` が必要かの判断もここで)

### P5: サウンド

**Oboe ではなく AAudio を直接使う（2026-09-13 に決定）。** 一次資料は
[Sound設計.md](Sound設計.md) §8。理由は「`minSdkVersion=33` なので Oboe の主目的である
OpenSL ES フォールバックが不要」「AAudio は NDK 同梱で追加依存ゼロ」「Gradle を使わない
方針と噛み合う」の 3 点。

**新規の DSP 実装はゼロ。** Mac 移植 P4a で入った `SoftwareMixer`（プラットフォーム非依存）が
固定 voice プール / SPSC コマンドキュー / リサンプル / 出力行列 / バスゲイン / 出力クロックを
すべて持っているので、Android 側は「デバイスを 1 本開いて `Render` を呼ぶ殻」だけで済む。

#### P5a: 音が出るまで — 完了(2026-09-13)

| 作業 | 対象 |
|---|---|
| AAudio バックエンド | `Sound/AAudio/AAudioSoundBackend.{h,cpp}` を新設（`CoreAudioSoundBackend` と同型） |
| ボイスアダプタの共用化 | `Sound/Mixer/MixerSoundVoice.{h,cpp}` を新設し、`CoreAudioSoundVoice` を置き換える |
| バックエンド選択 | `SoundBackend.h` の Android 分岐を `SOUND_BACKEND_NULL` → `SOUND_BACKEND_AAUDIO` |
| ビルド配線 | CMake の除外パターンに `/Sound/AAudio/` を足し、Android ターゲットにのみ `aaudio` をリンク |
| **CWD 依存バグ（4 件目）** | `WavDecoder.cpp` / `WavStreamDecoder.cpp` がパスをそのまま `fopen` している。`aq::res::ResolveExistingResourcePath` を通す |

**CWD 依存は P5a の必須項目。** Android の CWD は `/` なので、直さないと
`[Sound] OpenStream: 開けませんでした: Assets/Sound/AquaDashBGM.wav` のまま何も鳴らない
（P2 で直した JSON / PNG と同じ形の 4 件目。§8-8）。

**評価チェックリスト（P5a）** — 実機確認 2026-09-13 / Pixel 7a・Android 16
- [x] Android ビルドがエラー 0・新規警告なし（17 件はすべて既存）
- [x] Windows 3 構成（D3D11 / D3D12 / Vulkan）が従来と同数（エラー 0・警告 54 件）
- [x] BGM が実機で鳴る
- [x] SE（コイン取得・ジャンプ等）が実機で鳴る
- [x] オーディオスレッドで確保・ロックをしていない（data callback は `SoftwareMixer::Render` と
      `std::atomic::fetch_add` しか呼ばない）

**OS 側から取れた裏付け**

| 見たもの | 結果 |
|---|---|
| `[sound] AAudio 出力を開始` | **48000Hz / float32 / 2ch** — 要求どおりのフォーマットで開けた（作り直し経路は通らず） |
| `dumpsys audio` | `type:AAudio ... state:started`、出力先 `deviceIds:[3]`（内蔵スピーカー） |
| `dumpsys media.audio_flinger` | `numTracks=1 writeErrors=0 underruns=0 overruns=0`、FIFO アンダーランなし |

**P5a で分かったこと**

1. **`SoftwareMixer` は本当に無改修で載った。** Mac 移植 P4a の成果がそのまま効き、
   Android 側に書いたのは「ストリームを開く殻」だけ。新規 DSP コードはゼロ。
2. **CWD 依存バグの 4 件目を踏んでいた。** `WavDecoder` / `WavStreamDecoder` が
   パスをそのまま `fopen` しており、直すまで BGM は
   `OpenStream: 開けませんでした` で一切鳴らなかった。**バックエンドを実装しても、
   これを直さないと無音のまま**なので切り分けの順番に注意（§8-8 と同じ形）。
3. **ボイスアダプタはプラットフォーム非依存だった。** `CoreAudioSoundVoice` は
   `SoftwareMixer` への委譲しか持っていなかったので、`Sound/Mixer/MixerSoundVoice` へ
   格上げして CoreAudio / AAudio で共用する形にした（iOS 移植もこれをそのまま使える）。
4. **初期化の途中で失敗するとミキサが初期化されたまま残っていた**（CoreAudio から
   引き継いだ不整合）。`Finalize()` の `if (initialized_)` を外して無条件に畳むようにした。
   `SoftwareMixer::Finalize()` は冪等なので二重呼び出しも安全。
5. **`GetOutputClock` の遅延は簡易見積り**（`bufferSizeInFrames + framesPerBurst` ÷ 出力レート）。
   `AAudioStream_getTimestamp()` による厳密化は P5b。A/V 同期（動画再生）を使うときに効く。

#### P5b: 挙動を整える — 実装完了(2026-09-13)

| 作業 | 対象 |
|---|---|
| サスペンド/復帰 | `ISoundBackend` に `OnSuspend()` / `OnResume()`(既定 no-op)を追加。`Engine::SyncSoundActivity` が `IPlatform::IsRenderable()` の変化点だけで叩く |
| デバイス切替 | `AAudioStreamBuilder_setErrorCallback` で切断を受け、`Update()`(サウンドスレッド)でストリームを作り直す |
| 遅延の実測化 | `AAudioStream_getTimestamp` が取れるときは「書いた総数 − 実際に鳴った位置」から算出。取れなければ初期化時の見積りへフォールバック |
| アンダーラン | ミキサ側の枯渇 + `AAudioStream_getXRunCount()` を合算して報告 |

**評価チェックリスト(P5b)** — 実機確認 2026-09-13 / Pixel 7a・Android 16
- [x] Android ビルドがエラー 0・新規警告なし / Windows 3 構成が従来と同数
- [x] BGM / SE が実機で鳴る(P5a から継続。退行なし)
- [x] **バックグラウンド中に音が鳴り続けない** — `dumpsys audio` が
      前面 `state:started` → ホームで `state:paused` → 復帰で `started`。背面の CPU は 5 秒で 0 tick。
      10 往復して状態遷移は毎回正しく、PID 不変・サウンドのエラーログなし
- [~] サスペンド→復帰で音が壊れない・二重再生しない — **状態遷移は確認済みだが、聞こえ方は未確認**
- [ ] イヤホンの抜き差しで落ちず、出力先が切り替わる — **未確認**。
      `cmd audio` にデバイス接続を擬似する機能が無く adb から起こせないため、実際に抜き差しして
      logcat に `[sound] 出力ストリームが切れたので作り直す` が出るかを見る必要がある
- [—] 3D 音響の定位が Windows と一致する — **対象なし**。AquaDash は `SoundEngine::Play`(2D)と
      BGM ストリームしか使っておらず、`CreateSource` による 3D 音源も `AudioSourceComponent` を
      持つアセットも無い。3D の計算は `Mixer3D` → 出力行列 → `SoftwareMixer` という
      プラットフォーム非依存の経路で macOS と共通なので、Android 固有のリスクは低い。
      **ゲームが 3D 音源を使い始めたときに見直す。**

**P5b で分かったこと**

1. **一時停止では積んだ音を捨てる必要がある。** `requestPause` だけだと、復帰の瞬間に
   背面へ回る直前の音が鳴り直して二重に聞こえる。`requestFlush` は `PAUSED` 状態でしか
   通らないので、`waitForStateChange` で遷移の完了を待ってから呼ぶ。
2. **エラーコールバックのスレッドから `close` してはいけない**(AAudio の決まり)。
   フラグだけ立てて、サウンドスレッドの `Update()` で作り直す。ミキサには触らないので
   再生中のボイスはそのまま続きから鳴る。
3. **画面が消えると `TERM_WINDOW` が来る = 背面扱いになる。** 実機確認中に前面のはずが
   `state:paused` になって退行を疑ったが、端末の画面が落ちていただけだった。
   **サウンドやライフサイクルを adb で測るときは `svc power stayon true` で画面を点けておく。**

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
