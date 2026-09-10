# Mac(Metal)移植 設計

> 対象コミット: f13f621 / 最終更新: 2026-09-10

対象: `aqEngine/` + `Game/`。macOS(Apple Silicon)で Metal 描画・プレイ可能にする。
一次資料の分担:
- **本書** … 方針決定・新規/変更ファイルの責務・フェーズ計画・チェックリスト。
- [Mac移植調査.md](Mac移植調査.md) … Windows/MSVC 依存の棚卸し(数値・該当ファイル一覧)、MoltenVK/DXC の対応状況、道A/道B の比較。本書では繰り返さない。
- [Xbox移植設計.md](Xbox移植設計.md) … `IPlatform` 抽象の導入経緯。本書はその 3 番目の実装を足す。
- [VulkanBackend設計.md](VulkanBackend設計.md) … Vulkan バックエンド本体。本書は Mac 向け差分だけを書く。
- ネイティブ Metal バックエンドの詳細は、着手時に `MetalBackend設計.md` を別途起こす(D3D12/Vulkan と同じ運用)。本書 §7 は方針のみ。

---

## 0. 方針(決定事項)

| 項目 | 決定 | 理由 |
|---|---|---|
| Metal への到達経路 | **道A(Vulkan + MoltenVK)を先行**し、道B(ネイティブ Metal)は P6 以降 | Vulkan は実機動作済で Win32 依存がサーフェス生成と DXC だけ。足回り(ビルド/入力/音/Platform)を道A で先に片付け、道B はグラフィックスに集中できる |
| ビルド | **CMake を新設**。当面は既存 `.vcxproj` と併存し、Windows の主経路は vcxproj のまま | Windows 開発を止めない。CMake→VS 生成が既存ビルドと等価になった時点で統一を判断(§8-1) |
| 可搬性検証 | **Windows 上の clang-cl 構成を P0 で作る** | Mac を触らずにコンパイラ差(MSVC→Clang)を潰す。Mac 作業をプラットフォーム API に限定できる |
| プラットフォームマクロ | `AQ_PLATFORM_WIN32` / `AQ_PLATFORM_UWP` / `AQ_PLATFORM_MAC` を**ちょうど 1 つ**定義。未定義時の既定は WIN32 | 現状「Win32 = `!AQ_PLATFORM_UWP`」(45 箇所)は 3 プラットフォーム目で破綻する。`ENGINE_GRAPHICS_*` と同じ検証方式に揃える |
| キーボード/マウス | **`IKeyboardBackend` / `IMouseBackend` を新設**し DirectInput を実装側へ移す | Pad は `IPadBackend` で分離済。ここだけ Bridge が抜けている(architecture.md §1「同じ構造を他サブシステムにも展開する」) |
| サウンド | `ISoundBackend` 実装 **`CoreAudioSoundBackend`** を追加。ボイスは**ソフトウェアミキサ**(プラットフォーム非依存)上の論理ボイス、出力だけ CoreAudio | `ISoundVoice.h` が既に「Oboe 側はミキサ内論理ボイスで同義を満たす」と想定。ミキサは将来 Oboe でも再利用できる |
| シェーダ | Mac では **ビルド時に `.spv` を生成**(Vulkan SDK for macOS 同梱の `dxc`)。Vulkan バックエンドに `.spv` 読み込み経路を追加。Windows は実行時 DXC を維持 | Mac 実行時の `libdxcompiler.dylib`+`wrl` 依存を消す。`.fx` は無改変 |
| 画像(PNG/JPG) | Mac では `stb_image`。Windows は DirectXTex/WIC を維持 | WIC は Windows 専用。Windows の挙動を変えない(統一は §8-2) |
| 数学 | DirectXMath を継続。ただし **ヘッダを `ThirdParty/DirectXMath` に同梱** | ヘッダオンリで clang/ARM64 NEON 対応なのでコード置換は不要。一方 Mac には Windows SDK が無く入手元が無いため同梱する(§6) |
| 動画 | Mac では `VideoPlayer` を無効(Null) | Media Foundation 依存。AVFoundation 実装は本計画の範囲外 |
| グラフィックス API の選択 | **MSBuild プロパティ `AqGraphicsApi` に一本化**(`Game/GraphicsApi.props`)。`aq.h` のコメントアウト書き換えによる切替は廃止 | `#pragma comment(lib)` 全撤去に伴い、API 別 lib をプロジェクト側で条件分岐させる必要がある。定義と lib の単一ソース化を兼ね、CMake 側の `AQ_GRAPHICS_API` と同型になる |
| Objective-C++ の範囲 | `.mm` は `Platform/Mac/`・`HID/Mac/`・`Sound/CoreAudio/` に閉じる。エンジン本体は `.cpp` のまま | 既存スタイル維持。道B は metal-cpp で `.cpp` に書く |

---

## 1. 全体像

```
[エントリ]  Main.cpp(WinMain)  /  UWPMain.cpp  /  MacMain.mm(main + NSApplication)   ← 追加
      │  IPlatform 実装を注入(PlatformWin32 / PlatformUWP / PlatformMac)           ← 追加
      ▼
   Engine ── IPlatform* ── CreateMainWindow → NativeWindowHandle(void*)
      │                      Win32: HWND / UWP: CoreWindow / Mac: CAMetalLayer*
      ├─ GraphicsDevice ── IGraphicsDeviceImpl ── Vulkan(MoltenVK 上)  ← CreateSurface だけ Mac 分岐
      ├─ InputManager ─┬─ IKeyboardBackend / IMouseBackend ← 新設(DirectInput / Cocoa / Null)
      │                └─ IPadBackend(Win32=XInput+DualSense 合成 / WinRT / GameController)
      ├─ SoundEngine ── ISoundBackend(XAudio2 / CoreAudio+SoftwareMixer)
      └─ Resource ── 画像ローダ(DirectXTex+WIC / DirectXTex+stb_image)
```

「API 選択(`ENGINE_GRAPHICS_*`)」と「プラットフォーム選択(`AQ_PLATFORM_*`)」は直交のまま。Mac × Vulkan(道A)、Mac × Metal(道B)、Win32 × Vulkan が全部成立する。

---

## 2. プラットフォーム層

### 2.1 マクロと共通ヘッダ

| ファイル | 責務 | 変更 |
|---|---|---|
| `aqEngine/Platform/Common/PlatformDefs.h` **(新規)** | `AQ_PLATFORM_*` の既定値決定(未定義 かつ `_WIN32` → `AQ_PLATFORM_WIN32`)と「ちょうど 1 つ」検証。`AQ_PLATFORM_DESKTOP`(WIN32 or MAC)等の派生マクロ | `aq.h` 先頭で include |
| `aqEngine/aq.h` | PCH。Windows 専用ブロック(`windows.h`/D3D/`dinput.h`/DirectXTex の WIC 分岐/`#pragma warning`)を `#if defined(AQ_PLATFORM_WIN32) \|\| defined(AQ_PLATFORM_UWP)` で囲む。**`#pragma comment(lib)` を全撤去**(vcxproj の `AdditionalDependencies` と CMake へ移す) | 変更 |
| `aqEngine/Utility.h` | `windows.h`/`tchar.h` を外す。`ZeroMemory`→`memset`、`vsprintf_s`→`vsnprintf`、`OutputDebugStringA`→`aq::debug::OutputString`(下記)、`EngineAssertMsg` を `_wassert` 非依存に | 変更 |
| `aqEngine/Platform/Common/DebugOutput.h` **(新規)** + `DebugOutputWin32.cpp` / `DebugOutputMac.cpp` | `aq::debug::OutputString(const char*)`。Win32/UWP = `OutputDebugStringA`、Mac = `fputs(stderr)` | 新規 |
| `aqEngine/Platform/Common/AlignedAlloc.h` **(新規)** | `aq::memory::AlignedAlloc(size, align)` / `AlignedFree`。Win32 = `_aligned_malloc`、Mac = `posix_memalign`。`Memory/HeapAllocator.h`・`StackAllocator.h` 等 13 箇所を置換 | 新規 |
| `aqEngine/Util/GameTimer.{h,cpp}` | `LARGE_INTEGER`/QPC → `std::chrono::steady_clock`。FPS 制限のスピンは「`sleep_for` で粗く待ち → 残り 1ms はスピン」 | 変更 |
| `aqEngine/Util/Profiler.{h,cpp}` | QPC → `steady_clock`、`GetCurrentThreadId` → `std::hash<std::thread::id>` | 変更 |
| `aqEngine/Engine.{h,cpp}` | `GetHWND()` を `AQ_PLATFORM_WIN32` 限定に降格し、汎用 `GetNativeWindowHandle()`(`NativeWindowHandle`)を追加。`CoInitializeEx`/`CoUninitialize` を Windows ブロックへ | 変更 |
| 各 `*_s` 呼び出し(約 33 箇所) | `snprintf`/`fopen`/`strncpy` 等へ機械置換。`fopen_s` は `aq::util::OpenFile`(薄いラッパ)に寄せてもよい | 変更 |
| `Game/GraphicsApi.props` **(新規)** | MSBuild プロパティ `AqGraphicsApi`(既定 `D3D12`。`D3D11`/`D3D12`/`Vulkan`)から `ENGINE_GRAPHICS_*` の定義と API 別 lib(`d3d12`/`dxgi`/`d3dcompiler`/`vulkan-1`/`dxcompiler`)を `Condition` 付きで導出する。Engine/DirectX/GameUWP の 3 vcxproj が Import。**Vulkan 分岐は Condition 付きなので SDK 未導入環境でもリンクが壊れない** | 新規 |
| `Game/Application/Main.cpp` | `#if !defined(AQ_PLATFORM_UWP)` → `#if defined(AQ_PLATFORM_WIN32)` | 変更 |

プラットフォーム非依存の共通ヘッダは `aqEngine/Platform/Common/` に置く(OS 別実装 `PlatformWin32.*` / `PlatformUWP.*` / `Platform/Mac/` と視覚的に分ける)。VS のフィルターも `Platform\Common` を新設する。`DebugOutputMac.cpp` は vcxproj には登録せず、CMake からのみ拾う。

`!defined(AQ_PLATFORM_UWP)` の 45 箇所は「内容が Win32 専用」なら `defined(AQ_PLATFORM_WIN32)` へ、「デスクトップ共通」なら `defined(AQ_PLATFORM_DESKTOP)` へ書き換える。判断は 1 箇所ずつ行い、機械置換しない。

### 2.2 Mac プラットフォーム実装

| ファイル | 責務 |
|---|---|
| `Game/Application/MacMain.mm` **(新規)** | `int main()`。`NSApplication` 生成 → `PlatformMac` をスタックに置き → `Engine::Create` → `CreateApplication<app::Application>` → `InitializeParameter`(1280×720)→ `Initialize`/`RunGame`/`Finalize`。`Main.cpp` と同じ 30 行構成。全体を `#if defined(AQ_PLATFORM_MAC)` で囲む |
| `aqEngine/Platform/Mac/PlatformMac.h` / `.mm` **(新規)** | `IPlatform` 実装。`CreateMainWindow`: `NSWindow` + `wantsLayer=YES` の `NSView`(`CAMetalLayer` 付き、`contentsScale` = backing scale)を生成し、`NativeWindowHandle.handle = CAMetalLayer*` を返す。`PumpEvents`: `nextEventMatchingMask:untilDate:nil` を空になるまで回し、キー/マウスイベントを `CocoaInputSink`(§3.2)へ転送。Close 通知で false。`GetContentRoot`: 環境変数 `AQ_CONTENT_ROOT` → `.app/Contents/Resources` → `nullptr`(既存の `FindProjectRoot` 探索にフォールバック)の順 |
| `aqEngine/Platform/PlatformBudget.h` | `AQ_PLATFORM_MAC` プロファイル追加(Win32 と同値: 無制限・論理コア数) |

`Engine::InitializeGraphicsAPI` は `window_.handle` をそのまま `IGraphicsDeviceImpl::Initialize` に渡す。Vulkan 側が Mac では `CAMetalLayer*` として解釈する(§4)。

---

## 3. 入力

### 3.1 抽象IF(新設)

| ファイル | 責務 |
|---|---|
| `aqEngine/HID/IKeyboardBackend.h` **(新規)** | `enum class KeyBoardType`(**中立値**。現行 UWP 分岐の並びを正とし、DIK 値依存を廃止)、`struct KeyboardState { uint8_t keys[KEY_COUNT]; }`、`class IKeyboardBackend { virtual bool Initialize(NativeWindowHandle) ; virtual void Poll(KeyboardState& out); }` |
| `aqEngine/HID/IMouseBackend.h` **(新規)** | `struct MouseState { int32_t dx, dy, wheel; uint8_t buttons[8]; float cursorX, cursorY; }`(`DIMOUSESTATE2` の中立版。現行 `MouseStateNeutral` を昇格)、`class IMouseBackend { Initialize / Poll(MouseState&) }` |
| `aqEngine/HID/KeyboardMouseBackend.h` **(新規)** | `PadBackend.h` と同じ選択ヘッダ。WIN32 = `DirectInputKeyboardBackend`/`DirectInputMouseBackend`、UWP = `NullKeyboardBackend`/`NullMouseBackend`、MAC = `CocoaKeyboardBackend`/`CocoaMouseBackend` |
| `aqEngine/HID/Input.h` / `.cpp` | `KeyBoard`/`Mouse` から `LPDIRECTINPUT*`/`HRESULT`/`<dinput.h>`/`<Xinput.h>` を除去し、`IKeyboardBackend*`/`IMouseBackend*` を持つ。`Update` は `Poll` → `old_/now_` 入替 → 長押しタイマ、の現行ロジックのみ。`InputManager::Setup` は `HRESULT`→`bool`。`GetCursorPos` はバックエンドの `cursorX/Y` を返す |
| `aqEngine/HID/Win32/DirectInputKeyboardBackend.{h,cpp}` / `DirectInputMouseBackend.{h,cpp}` **(新規・移設)**<br>※ 既存の `Win32PadBackend` / `XInputPadBackend` / `DualSensePadBackend` は `HID/` 直下のまま。`HID/Win32/` への集約は別途 `<Build>` で行う | 現 `Input.cpp` の DirectInput 部分。`DirectInput8Create`/`SetCooperativeLevel(HWND)`/`GetDeviceState` と、**DIK → `KeyBoardType` の変換表**。`::GetCursorPos`+`ScreenToClient` もここ |
| `aqEngine/HID/NullKeyboardBackend.h` / `NullMouseBackend.h` **(新規)** | 全ゼロを返す。UWP と、Mac の P2〜P3 で使う |

### 3.2 Mac 実装

| ファイル | 責務 |
|---|---|
| `aqEngine/HID/Mac/CocoaInputSink.{h,mm}` **(新規)** | `PlatformMac::PumpEvents` が `NSEvent`(keyDown/keyUp/flagsChanged/mouseMoved/mouseDown/Up/scrollWheel)を投げ込む状態バッファ。**Mac 専用コード同士の接合点**なので `IPlatform` には露出させない。`keyCode`(Carbon 仮想キーコード)→ `KeyBoardType` 変換表を持つ |
| `aqEngine/HID/Mac/CocoaKeyboardBackend.{h,mm}` / `CocoaMouseBackend.{h,mm}` **(新規)** | `Poll` で `CocoaInputSink` の内容を `KeyboardState`/`MouseState` へコピー。マウス座標は `NSView` 座標系(左下原点)を左上原点へ反転して返す |
| `aqEngine/HID/Mac/GameControllerPadBackend.{h,mm}` **(新規)** | `IPadBackend` 実装。`GCController.controllers` を index 順に対応、`extendedGamepad` から `PadState` へ正規化。振動は `GCDeviceHaptics`(CHHapticEngine)、非対応機は no-op。**DualSense は GameController.framework がネイティブ対応**しており、`SetTriggerResistance` は `GCDualSenseAdaptiveTrigger`(`setModeFeedbackWithStartPosition:resistiveStrength:`)で実装できる(Win32 の HID 直読み `DualSensePadBackend` は不要) |
| `aqEngine/HID/PadBackend.h` | `#elif defined(AQ_PLATFORM_MAC)` → `GameControllerPadBackend`。既存の `!defined(AQ_PLATFORM_UWP)` → `defined(AQ_PLATFORM_WIN32)`(`Win32PadBackend` = XInput + DualSense 合成) |
| `aqEngine/HID/DualSensePadBackend.cpp` / `Win32PadBackend.h` | Windows 専用(`setupapi`/`hidsdi`)。`#if defined(AQ_PLATFORM_WIN32)` へ。`#pragma comment(lib, "setupapi.lib"/"hid.lib")` は P0 でビルド設定側へ移す |

---

## 4. グラフィックス(道A: Vulkan on MoltenVK)

| ファイル | 変更 |
|---|---|
| `aqEngine/Graphics/Vulkan/VulkanCommon.h` | `VK_USE_PLATFORM_WIN32_KHR` を `AQ_PLATFORM_WIN32` 限定に。MAC では `VK_USE_PLATFORM_METAL_EXT` + **`VK_ENABLE_BETA_EXTENSIONS`**(これが無いと portability subset の識別子が一切引けない)。`#pragma warning` は `AQ_PLATFORM_WINDOWS_FAMILY` で囲む(clang は解さない) |
| `VulkanGraphicsDeviceImpl.cpp` `CreateInstance` | インスタンス拡張: WIN32 = `VK_KHR_win32_surface`、MAC = `VK_EXT_metal_surface` + `VK_KHR_portability_enumeration`、flags に `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` |
| 同 `CreateSurface(void*)` | MAC では `vkCreateMetalSurfaceEXT`(`pLayer = static_cast<CAMetalLayer*>(handle)`)。引数名 `hwnd` → `nativeWindow` |
| 同 `CreateLogicalDevice`(設計書の旧称 `CreateDevice`) | MAC ではデバイス拡張に `VK_KHR_portability_subset` を追加。`VkPhysicalDevicePortabilitySubsetFeaturesKHR` を照会し、非対応項目(`triangleFans`, `imageViewFormatSwizzle`, `separateStencilMaskRef` 等)を `StartupLog` に出す。**照会した構造体はそのまま `VkDeviceCreateInfo::pNext` に繋いで対応分を有効化する**(繋がないと `mutableComparisonSamplers` が無効のままになり P3 の影描画(`SampleCmp`)が壊れる)。照会結果は書き換えないこと(非対応項目を要求すると `vkCreateDevice` が `VK_ERROR_FEATURE_NOT_PRESENT` で落ちる) |
| 同 スワップチェーン | `VK_PRESENT_MODE_FIFO_KHR` を第一候補に(MoltenVK は MAILBOX を返さない場合がある)。`minImageCount` は capabilities 準拠(既存どおり) |
| `VulkanShader.{h,cpp}` | **`.spv` 読み込み経路を追加**: `CreateShader(path, entry, type)` はまず `<shaderDir>/spv/<stem>.<entry>.<type>.spv` を探し、あれば `vkCreateShaderModule` + SPIRV-Reflect(既存)。無ければ従来の DXC 実行時コンパイル(`#if defined(AQ_PLATFORM_WIN32)` 内)。`<wrl/client.h>`/`MultiByteToWideChar` は DXC 分岐内に閉じる |
| `Tools/ShaderCompile/compile_spv.cmake` **(新規)** | `.fx` × エントリ一覧 → `dxc -spirv -fspv-entrypoint-name=main -fvk-use-dx-layout -E <entry> -T <vs\|ps\|cs>_6_0 -fvk-b-shift 0 all -fvk-t-shift 16 all -fvk-s-shift 32 all -fvk-u-shift 48 all -I <shaderDir>`(+ `_DEBUG` 時のみ `-Zi -Qembed_debug`)。**`-fvk-use-dx-layout` は必須**(cbuffer を D3D パッキングにする。抜けると CPU 構造体とレイアウトがズレる)。引数は `Tools/ShaderCompile/dxc_args.txt` 1 ファイルに集約し、`VulkanShader.cpp` は `#include` で埋め込み、`compile_spv.cmake` は行単位で読む(実行時のファイル依存を増やさない)。エントリ一覧は `Game/Assets/Shader/shader_entries.txt`(新規、`<file> <entry> <stage>` 行)。**`.fx` は 40 本・エントリ 59 個**(うち 11 個はコードから未参照) |

抽象IF(`IGraphicsDeviceImpl`/`IRenderContextImpl`)・呼び出し側・`.fx` の変更は **0**。

---

## 5. サウンド

| ファイル | 責務 |
|---|---|
| `aqEngine/Sound/Mixer/SoftwareMixer.{h,cpp}` **(新規・可搬)** | 論理ボイスの集合を出力フォーマット(48kHz float, 2ch 想定)へミックスするプラットフォーム非依存ミキサ。ボイスごとに: 投入バッファキュー(コピー)/`RefSoundClip` 参照(ゼロコピー)/ループ領域/線形リサンプル(`SetFrequencyRatio`)/出力行列(`SetOutputMatrix`)/ボリューム/消費フレーム数。バス音量とマスタ音量。`Render(float* out, uint32_t frames)` を出力スレッドから呼ぶ。ロックは投入側と Render 側で SPSC リング + `std::atomic`(architecture.md §5) |
| `aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.{h,mm}` **(新規)** | `ISoundBackend` 実装。`AudioUnit`(`kAudioUnitSubType_DefaultOutput`)を 1 つ開き、render callback で `SoftwareMixer::Render`。`GetOutputClock` は render callback の `AudioTimeStamp.mHostTime` + 累積フレームから算出。`CreateVoice` は `SoftwareMixer` に論理ボイスを追加して `CoreAudioSoundVoice` を返す |
| `aqEngine/Sound/CoreAudio/CoreAudioSoundVoice.{h,cpp}` **(新規)** | `ISoundVoice` 実装。全メソッドを `SoftwareMixer` の論理ボイス操作に委譲する薄いアダプタ |
| `aqEngine/Sound/Decoder/ExtAudioFileDecoder.{h,mm}` **(新規)** | `ISoundDecoder` 実装(AudioToolbox `ExtAudioFile`)。mp3/aac/m4a を PCM へ。`MFDecoder` と同じ静的 `DecodeFileFully` も提供 |
| `aqEngine/Sound/SoundBackend.h` | `#elif defined(AQ_PLATFORM_MAC)` → P2 では `SOUND_BACKEND_NULL`(`NullSoundBackend` = 無音)。P4 で `SOUND_BACKEND_COREAUDIO` へ差し替える。実体と名前が食い違わないよう段階を分ける |
| `aqEngine/Sound/Decoder/CompressedDecoder.h` **(新規)** | 「wav 以外」用デコーダの選択ヘッダ。WIN32/UWP = `MFDecoder`、MAC = `ExtAudioFileDecoder`。`SoundClip.cpp:50`・`SoundEngine.cpp:33` の `MFDecoder` 直参照をこれ経由に |
| `aqEngine/Sound/Video/VideoPlayer.{h,cpp}` | 本体を `#if defined(AQ_PLATFORM_WIN32) \|\| defined(AQ_PLATFORM_UWP)` で囲み、MAC は `Open` が false を返す Null 動作 |

`SoundEngine`/`Mixer3D`/`SoundStream`/`AudioDirector` は無変更。

---

## 6. リソース・ThirdParty・imgui

| ファイル | 変更 |
|---|---|
| `ThirdParty/stb/stb_image.h` **(新規・同梱)** | PNG/JPG デコード |
| `aqEngine/Resource/ImageLoader.{h,cpp}` **(新規)** | `LoadImageFile(path) → ScratchImage`(DirectXTex 型は維持)。拡張子で DDS/TGA は DirectXTex、PNG/JPG は WIN32/UWP なら `LoadFromWICFile`、MAC なら `stb_image` → `Image` 構造体へ詰めて `ScratchImage::InitializeFromImage`。`Resource.cpp:1607-1625`・`Terrain/HeightmapChunk.cpp:50-133` の直呼びをこれに集約。引数は `std::string`(UTF-8)で受け、呼び出し元の `mbstowcs_s` による `wchar_t` パス変換(`Resource.cpp:1605`・`HeightmapChunk.cpp:47`)を本ローダ内へ吸収する |
| `ThirdParty/DirectXTex` | 非 Windows 経路(`DirectXTexDDS/TGA/HDR/Convert/Resize/Mipmaps/BC*`)を CMake で選択。`DirectXTexWIC.cpp`・`BCDirectCompute`・D3D11/12 系 cpp は Windows のみ。`sal.h` は `ThirdParty/WinCompat/sal.h`(自前。P2 で追加)、その他の SAL / Windows 型は `ThirdParty/DirectX-Headers/include/wsl/` が供給する。`DirectXTexFlipRotate.cpp` は `_WIN32` ガードが無く全体が WIC 実装なので非 Windows では除外(P2 で判明。エンジンから未使用) |
| `ThirdParty/DirectXMath` **(新規・同梱)** | 3.21b(jun2026)/ MIT。`Inc/` 一式 10 ファイル。コードは無改変。**`aq.h` の include を書き換えるだけでは足りない**: `Math/Vector.h` と DirectXTex のヘッダが無修飾の `<DirectXMath.h>` を書くため、`ThirdParty/DirectXMath/Inc` 自体をインクルードパスに載せて SDK 版を隠す必要がある(vcxproj は `/external:I`、CMake は INTERFACE ターゲット)。Windows SDK 版は 3.19(UWP は 3.18) |
| `ThirdParty/WinCompat/sal.h` **(新規・自前。P2 で追加)** | 同梱 DirectXMath の `DirectXMath.h` が無条件に `#include "sal.h"` する受け皿。**上流の DirectXMath / DirectX-Headers のどちらも `sal.h` を同梱していない**ため自前で用意する(P2 で判明。上流の contents API で確認済み)。中身は DirectXMath / DirectXTex が実際に使う注釈だけを空マクロにしたもので、`basetsd.h` と定義が重なっても双方 `#ifndef` + 空定義なので衝突しない。**非 Windows のときだけ**インクルードパスに載せる(Windows SDK の `sal.h` を隠さないため) |
| `ThirdParty/DirectX-Headers` **(新規・同梱)** | v1.619.5 / MIT。**現行版に `sal.h` というファイルは無く**、SAL 注釈は `include/wsl/stubs/basetsd.h` が定義する。DirectXTex の非 Windows 経路が要求するのは `directx/dxgiformat.h` + `wsl/winadapter.h` + `wsl/wrladapter.h` + `directx/d3d12.h` とその推移依存(`dxgicommon.h`/`d3dcommon.h`/`d3d12sdklayers.h`/`wsl/stubs/*`)。`d3dx12_*` や `dxguids` 等は不要なので入れない。**インクルードパスへ載せるのは非 Windows のときだけ**(`wsl/stubs/` が Windows SDK の同名ヘッダを隠すため) |
| `ThirdParty/BulletPhysics` | Mac は `add_subdirectory(src)`(`BT_USE_DOUBLE_PRECISION`/`BT_THREADSAFE=1`)。Windows は既存 prebuilt `.lib` 維持 |
| `ThirdParty/imgui/imgui_impl_osx.{h,mm}` **(新規・同梱)** | imgui 本体(1.92 WIP)と同じ版のものを取得 |
| `aqEngine/aq.h` | **ObjC++ TU(`__OBJC__`)では DirectXTex を include しない**(P2 で追加)。非 Windows の DirectXTex は `wsl/winadapter.h` 経由でスタブ `basetsd.h` を読み、そこが `BOOL` を uint32_t に typedef し `interface` を struct に #define するため、Cocoa の `typedef bool BOOL` と衝突して `@interface` が全滅する。`aq.h` は PCH として全 TU に強制インクルードされるので、ここで切る以外に手が無い。§10 の「`.mm` は Platform/Mac・HID/Mac・Sound/CoreAudio に閉じる」の帰結として、`.mm` は画像デコードに触らない |
| `aqEngine/Core/Application.cpp` | `ImGui_ImplWin32_*` を `#if defined(AQ_PLATFORM_WIN32)`、MAC は `ImGui_ImplOSX_Init(NSView*)`(`CAMetalLayer` の `delegate`/`superview` から取得するため `PlatformMac` に `GetNSView()` を持たせ、`static_cast<PlatformMac*>` は Mac ブロック内でのみ行う)。描画は既存 `VulkanImGui`(自前) |
| `aqEngine/Rendering/ImGuiRenderCommand.cpp` | `imgui_impl_dx11.h` include を D3D11 ブロック内へ |

---

## 7. 道B(ネイティブ Metal)の方針 ― 詳細は着手時に `MetalBackend設計.md`

- `aqEngine/Graphics/Metal/` に `MetalGraphicsDeviceImpl` / `MetalRenderContextImpl` / `MetalPipelineCache` / `MetalBuffers` / `MetalResources` / `MetalRenderTarget` / `MetalDepthMap` / `MetalShader` / `MetalImGui`(Vulkan フォルダと同名規則、**metal-cpp** で `.cpp`)。
- `ENGINE_GRAPHICS_METAL` を `aq.h`/`Engine.cpp`/`Core/Application.cpp`/`Graphics/RenderContext.cpp`/`Rendering/ImGuiRenderCommand.cpp` の 5 箇所に追加。
- シェーダ: `.spv`(§4 で生成済)→ SPIRV-Cross → MSL → `xcrun metal` → `.metallib` をビルド時に生成。binding 写像は Vulkan の shift 規約(b:0/t:16/s:32/u:48)を `[[buffer(n)]]`/`[[texture(n)]]` にそのまま流す。
- 抽象IF追加 0 本を目標(D3D12/Vulkan 実績)。

---

## 8. オープン課題

1. **vcxproj と CMake の統一時期**: P0 で CMake→VS 生成が等価と確認できたら vcxproj を生成物にするか、手書き維持か。`vs-project-files` スキルの扱いも連動。
2. **PNG/JPG ローダの統一**: Windows も `stb_image` にして分岐を消すか。sRGB 判定・`GenerateMipMaps` の結果差を比較してから決める。
3. **`SoftwareMixer` の性能**: 同時ボイス数上限と線形リサンプルの品質。XAudio2 の結果と A/B。
4. **`GetOutputClock` の精度**: A/V 同期(`SoundStream`)が要求する精度を `mHostTime` 基準で満たせるか。
5. **imgui_impl_osx と `PumpEvents` の競合**: imgui の OSX impl は `NSView` にイベントモニタを張る。`PlatformMac` の `sendEvent` と二重処理にならないよう順序を決める。
6. **KosmicKrisp**: macOS 26 + Apple Silicon 限定の完全準拠 Vulkan。MoltenVK で portability subset の制限に当たった場合の代替として評価。
7. **clang が出す警告の扱い**(P0 の clang-cl 検証で判明。ビルドは通るので P0 の完了条件からは外した):
   - `-Wdelete-abstract-non-virtual-dtor` 2 件 — `aq::IApplication`(`Engine.cpp:115`)と `app::actor::IState`(`StateMachine.cpp:117`)を、仮想デストラクタ無しの抽象基底ポインタ経由で `delete` している。**派生のデストラクタが走らない未定義動作**なので P1 で潰す
   - `-Wnontrivial-memcall` 6 件 — `MaterialCBData` / `Matrix4x4` への `memcpy`。実体はトリビアルに扱える見込みだが要確認
   - `-Wreorder-ctor` 1 件(`Graphics/Camera.cpp:10`)、`-Winconsistent-missing-override` 5 件、`-Wmicrosoft-exception-spec` 16 件<br>`-Wdelete-abstract-non-virtual-dtor` の 2 件は解消済み(コミット e5eecf2)
8. **`CompressedDecoder.h` の Mac 分岐が未定義**(P1 で判明): 設計では `ExtAudioFileDecoder` を P4 で足すことになっているが、
   **P2 は「Mac でビルド・リンクが通る」ことが到達点**なので、P2 の時点で `SoundClip.cpp` / `SoundEngine.cpp` が
   コンパイルできない。P2 で Mac 分岐に Null デコーダを置き、P4 で `ExtAudioFileDecoder` に差し替える。
9. **`ISoundVoice::SetOutputMatrix` のコメントと実装が逆**(P1 で判明): ヘッダは「入力ch × 出力ch, row-major」だが、
   `XAudio2SoundVoice` は XAudio2 の並び(出力ch × 入力ch)でそのまま渡している。`SoftwareMixer` は Windows と
   音が一致する方(XAudio2 の並び)に合わせた。唯一の呼び出し元(`SoundSource.cpp:138`、src=1/dst=2)は
   どちらの解釈でも同値なので実害はないが、**コメントの修正が要る**。
10. **UWP のハイトマップ挙動が変わる**(P1 で判明): `HeightmapChunk` の UWP 分岐は「DirectXTex 未リンク」を理由に
   無条件 `return false` していたが、現在 UWP は NuGet の `directxtex_uwp` を使っており記述が古い。
   `ImageLoader` への集約で分岐を畳んだため、**Xbox で地形が「読めない」→「読める」に変わる**(ユーザー承認済み)。
   Xbox 実機での確認は次に Xbox を触るときに行う。
11. ~~**MoltenVK が Vulkan 1.3 を advertise するか**(P2 の最大リスク)~~ → **解決(P2 実機)**。MoltenVK
   (Vulkan SDK 1.4.357.1 / macOS 26.6.2 / Apple Silicon)は `apiVersion = VK_API_VERSION_1_3` と
   `VkPhysicalDeviceVulkan13Features` をそのまま受け付け、`vkCreateDevice` は成功した。1.2 + 個別拡張への
   分解は不要。`VK_KHR_portability_subset` で非対応が報告されたのは `pointPolygons` /
   `tessellationIsolines` / `tessellationPointMode` の 3 件のみで、いずれも本エンジンは未使用。
12. **`SoftwareMixer` と XAudio2 の差異**(P4 への申し送り): リサンプルは線形補間のみ / `GetConsumedFrames` が
   先読み分 +2 進む(`SoundStream` の A/V 同期に影響しうる) / ピッチ比の上限なし / エフェクト・フィルタ・submix なし。
13. ~~**HiDPI**: `CAMetalLayer.drawableSize` と `InitializeParameter` の描画解像度の関係~~ → **P2 で決定**。
   Retina(backingScaleFactor = 2)では drawableSize が 2560x1440 になる一方、Engine のレンダーターゲットと
   深度は `InitializeParameter` の 1280x720 のままなので、スワップチェーンと深度アタッチメントが同じ
   `vkCmdBeginRendering` に並んで `VUID-VkRenderingInfo-pNext-06079/06080` の**エラー**になる(実機で再現)。
   **`CAMetalLayer.contentsScale = 1` に固定してドロウアブルを論理サイズ(1280x720)と一致させる**方針を採る。
   Windows と同じ 1 枚を描き、Retina への引き伸ばしは Core Animation に任せる(その分ぼやける)。
   Engine に解像度の概念を増やさずに済み、P3 の「Windows Vulkan 構成と同じ見た目」とも噛み合う。
   ネイティブ解像度で描く案(screenWidth/Height をドロウアブルに合わせ、renderWidth/Height は据え置いて
   最終パスで拡大)は、UI のヒットテスト座標系と ImGui の `DisplayFramebufferScale` まで巻き込むため
   **P4 以降**で扱う。実装は `PlatformMac.mm` の `UpdateLayerBacking`。
14. **`-G Xcode` が未検証**(P2 で判明): 検証環境が Xcode Command Line Tools のみでフル Xcode.app が無く、
   Xcode ジェネレータを起動できない。`macos-xcode` プリセットは残してあるが**動作未確認**。
   Ninja 経路は通っているので、Xcode 側を必須にするか落とすかを決める必要がある。
15. **ストレージイメージのフォーマット不一致警告 10 件**(P2 で判明。Mac 固有ではない): SPIR-V が
   `Rgba32f` を宣言している `RWTexture2D<float4>` に対して、実際のビューが `R16G16B16A16_SFLOAT` /
   `R8G8B8A8_UNORM` で束ねられている(Bloom の `g_Bright` / `g_Output` ほか)。**仕様上は
   ロード/ストアの結果が未定義**。DXC は HLSL の `RWTexture2D<float4>` を既定で `Rgba32f` として出すため、
   `[[vk::image_format("rgba16f")]]` を付けるか、ビュー側のフォーマットを揃える必要がある。
   Vulkan バックエンド共通の問題なので Windows Vulkan 構成でも同じはず。**`.fx` 無改変**の方針に触れるため、
   対処方針は別途決める。
16. **`.app` の `GetContentRoot` が P5 まで機能しない**(P2 で判明): `.app` から起動すると
   `GetContentRoot()` が `Contents/Resources` を返し、`FindProjectRoot()` はそこで確定してソースツリーの
   上方探索を行わない。しかし P5 まで `Resources` に Assets は入らないため、**実際に読めているのは
   「相対パス候補が CWD = `Game/` で解決している」から**にすぎない。`Game/` 以外を CWD にすると
   アセットを読めない(`.app` / 素の実行ファイルの双方で確認)。P5 で Assets を `Resources` へ
   同梱すれば解消する。それまでは CWD を `Game/` にして起動する。

---

## 9. フェーズ計画

各フェーズは独立してコミットし、評価チェックリストが埋まってから次へ進む。P0/P1 は **Windows だけで完結**する。

### P0: ビルド基盤とマクロ整理(Windows)

実装:
- `CMakeLists.txt`(root)/ `aqEngine/CMakeLists.txt` / `Game/CMakeLists.txt` / `ThirdParty/CMakeLists.txt`。ソースは `file(GLOB_RECURSE … CONFIGURE_DEPENDS)` + プラットフォーム別ディレクトリ除外(既存の「バックエンド cpp は `#ifdef` で空 TU」方針と整合)。PCH=`aq.h`、C++20、`/utf-8`、既存 4 構成相当。
- `Tools/SetupCMake/`(README: CMake/Ninja 導入、VS 生成手順、clang-cl 構成の生成手順)。
- `PlatformDefs.h` 新設、`AQ_PLATFORM_WIN32` 導入、`!defined(AQ_PLATFORM_UWP)` 45 箇所の見直し。
- `aq.h`/`Utility.h` 分割、`#pragma comment(lib)` **14 箇所を全撤去**。API 別 lib は `Game/GraphicsApi.props`(新規)へ、API 非依存 lib(`dinput8`/`dxguid`/`xinput`/`xaudio2`/`mf*`/`propsys`/`setupapi`/`hid`)は各 vcxproj の `AdditionalDependencies` へ移す。UWP 構成には Win32 専用 lib を入れない。
- `Engine::GetNativeWindowHandle()` 追加、`GetHWND` を WIN32 限定に。
- `DebugOutput.h`/`AlignedAlloc.h` 新設と置換。`GameTimer`/`Profiler` の chrono 化。`*_s` 置換。

評価:
- [x] 既存 `DirectX.sln`(MSVC)で Debug/Release/DebugXbox がビルドでき、AquaDash が従来どおり動く(D3D12 で確認)
      - Debug|x64 = 0 エラー / 54 警告、Release|x64 = 0 エラー / 52 警告、DebugXbox|x64 = 0 エラー(**VS2019 の MSBuild が必要**。VS18 の MSBuild だと `Microsoft.Windows.UI.Xaml.Cpp.targets` の解決に失敗する。P0 前からの既存事情)
      - Vulkan 構成は SDK 未導入のため未検証(P2 に持ち越し)
- [x] CMake → VS 生成(`--preset windows-vs2026`)で configure・ビルドが通り `build/windows-vs2026/bin/Debug/Game.exe` が生成される
- [x] CMake → clang-cl 構成でエンジンと Game が**コンパイル・リンク**できる(実行は任意)
      - clang 20.1.8 / Ninja Multi-Config で `Game.exe` の生成まで到達。潰した問題は 3 件:
        1. `Math/Vector.h` の `XMVECTOR.m128_f32[0]` 6 箇所 → `XMVectorGetX`。`m128_f32` は MSVC 固有の共用体メンバで **Mac でも落ちる**
        2. `ECS/Chunk.h` の `AlignedDeleter`。ネストクラスの NSDMI は外側クラスの完全クラス文脈に属するため、`begin_` 宣言時点で clang は既定構築不可と判断し `unique_ptr` の既定コンストラクタが消える(MSVC は判定を遅延して通す)。NSDMI をコンストラクタに置換
        3. CMake 4.x は MSVC 系でも既定フラグに `/EHsc` を入れない → `cmake/AqCommon.cmake` で明示(vcxproj の `ExceptionHandling=Sync` と揃えた)
- [x] `AQ_PLATFORM_*` を 2 つ定義すると `#error`、未定義なら WIN32 になる(cl で 3 ケース検証: 未定義→WIN32+DESKTOP+WINDOWS_FAMILY / WIN32+MAC→`#error` / UWP のみ→WINDOWS_FAMILY のみ)
- [x] `#pragma comment(lib` はエンジン/ゲームで 0 件(`aq.h` に残るのは説明コメント 2 行のみ。ThirdParty/imgui は対象外)。`!defined(AQ_PLATFORM_UWP)` は 0 件。`!defined(AQ_PLATFORM_WIN32)` は `PlatformDefs.h` の既定値判定と `HID/Input.h:100`(DirectInput 非対応プラットフォーム共通の中立マウス状態)の 2 箇所のみで、いずれも意図どおり
- [x] `AqGraphicsApi` を `D3D11`/`D3D12` に切り替えると、`aq.h` を編集せずに構成が切り替わり両方ビルド・起動する(`/p:AqGraphicsApi=D3D11` で 0 エラー、HUD が `D3D11 554.7 FPS` 表示。`Vulkan` は SDK 導入環境でのみ検証)
- [ ] `GameTimer` の FPS 制限(60)で実測 FPS が従来と同等、デルタタイムの分布に変化なし
      - **未検証**: AquaDash は FPS 制限を使っておらず(VSync オフで 908 FPS)、`sleep_for`+スピンの経路が実行されない。VSync オン時は P0 前後とも 144.0 FPS(6.94ms)で一致。スピン幅は従来 2ms → 設計書指定の 1ms に変更済みで、制限を使う場面が出たら再評価する
- [x] 警告数が P0 前と同数以下(main をビルドして比較。Release|x64 は前後とも C4244×102 + C4267×2 で**完全一致**。`_CRT_SECURE_NO_WARNINGS` を 3 vcxproj の全構成に追加して `fopen`/`mbstowcs` 置換による C4996 を抑止)

> 実機確認の注記: キーボード/パッドでのプレイ確認は自動化できなかった(`SendInput` / `keybd_event` をスキャンコード付きで送ってもタイトルが進まない)。**main をビルドして同条件で比較したところ挙動が完全に一致**したため P0 の回帰ではなく自動化側の制約。`HID/Input.{h,cpp}` の差分もガード行とコメントのみで、`AQ_PLATFORM_WIN32` 定義下ではプリプロセス結果が従来と同一。実操作での確認は手動で行う。

### P1: 入力/サウンド/画像の Bridge 化(Windows)

実装:
- §3.1 の `IKeyboardBackend`/`IMouseBackend`/選択ヘッダ/Null 実装、DirectInput の移設、`Input.h` の DirectInput 型除去。
- §5 の `SoftwareMixer`(可搬部分のみ。CoreAudio 出力はまだ)+ 単体テスト相当のツール(`Tools/MixerTest`: WAV を読ませてミックス結果を WAV に書く)。`CompressedDecoder.h` 選択ヘッダ。`VideoPlayer` の Windows ブロック化。
- §6 の `ImageLoader` 新設(Windows は WIC のまま)。`stb_image` 同梱(Mac 分岐は空で置く)。
- `Core/Application.cpp` の `ImGui_ImplWin32_*` に `AQ_PLATFORM_WIN32` ガードを被せる(§6)。
- ~~§4 の `.spv` 読み込み経路~~ → **P2 へ移動**。Vulkan SDK が未導入だと `AqGraphicsApi=Vulkan` の
  コンパイル自体が通らず、書いても検証できないため。Mac 側でも Vulkan SDK for macOS が必要になるので、
  P2 で Windows/macOS 両方まとめて導入・検証する。

評価:
- [ ] キーボード/マウス/パッドの全操作(AquaDash のタイトル〜ステージ)が P0 と同一に動く
      - **手動確認待ち**。合成入力は DirectInput に届かないため自動化不可(P0 と同じ事情)
- [x] `Input.h` に `dinput.h`/`Xinput.h`/`HRESULT`/`LPDIRECTINPUT*` が現れない
      - `Input.h`/`Input.cpp` とも該当シンボル 0 件。`AQ_PLATFORM_*` 分岐も消え、正味 -139 行
- [x] UWP 構成が Null 入力でビルドできる(DebugXbox|x64 = 0 エラー。実機起動は Xbox 実機作業時に確認)
- [x] `Tools/MixerTest` で 2 ボイス(片方ピッチ 1.5、出力行列で左右反転)のミックス結果が期待波形になる
      - `--selftest` が解析解と一致(maxError L=R=0.000e+00 / underrun=0 / retireOverflow=0)
- [x] `ImageLoader` 経由で PNG/JPG/DDS/TGA のテクスチャが従来どおり表示される
      - タイトル画面でステージサムネイル(PNG)・海/地形(DDS/TGA)・フォントアトラスが従来どおり描画
- [x] MSVC / clang-cl 双方で Debug/Release がビルドでき、AquaDash が P0 と同じ見た目で動く
      - MSVC Release|x64 = 0 エラー(警告は C4244 x102 + C4267 x2 で P0 から変化なし)、clang-cl Debug = 0 エラー

### P2: Mac でウィンドウとクリア画面(Mac 実機)

実装:
- `MacMain.mm`、`PlatformMac.{h,mm}`、`PlatformBudget` の MAC プロファイル。
- CMake の Xcode/Ninja 生成。ThirdParty(Bullet/DirectXTex 非 Windows/ufbx/spirv_reflect/vma)の Mac ビルド。
- Vulkan §4 の Mac 分岐(Metal surface / portability)。Vulkan SDK の導入手順を `Tools/SetupVulkan/README.md` に追記(Windows / macOS 両方)。
- **§4 の `.spv` 読み込み経路と `compile_spv.cmake`/`shader_entries.txt`(P1 から移動)**。まず Windows Vulkan 構成で `.spv` 有無の両方を検証してから Mac へ持っていく。
- 入力 = Null、サウンド = `CoreAudioSoundBackend` の骨格(`Initialize` 成功・無音)、`ImageLoader` の `stb_image` 分岐。

評価:
- [x] Mac(Apple Silicon)で `-G Ninja` からビルド・リンクが通る(macOS 26.6.2 / M 系 / AppleClang 21.0.0)
      - `cmake --preset macos-ninja` の configure は**初回から無修正で成功**。ビルドで潰した問題は 6 件:
        1. **Bullet のインクルードパスが空**。`src/CMakeLists.txt` は `SUBDIRS` を並べるだけで、
           `INCLUDE_DIRECTORIES(${BULLET_PHYSICS_SOURCE_DIR}/src)` は読まないルート側にある。
           `ThirdParty/CMakeLists.txt` で 6 ターゲット全てに付与。`BT_USE_DOUBLE_PRECISION` も
           3 ターゲットにしか付いていなかったため同時に 6 つへ広げた(btScalar のサイズが
           ターゲット間でずれる ODR 違反になる)
        2. **`sal.h` が無い**。同梱 DirectXMath が無条件に `#include "sal.h"` する一方、上流の
           DirectXMath / DirectX-Headers のどちらも同梱していない(§6 の「DirectX-Headers が供給する」は
           誤り)。`ThirdParty/WinCompat/sal.h` を新設し、非 Windows のみインクルードパスに載せた
        3. **`DirectXTexFlipRotate.cpp` が非 Windows で通らない**。他の WIC 利用ファイルと違い
           `_WIN32` ガードが 1 つも無く全体が WIC 実装。エンジンから未使用なので除外した
        4. **`EnginePrintf` の末尾カンマ**。`Printf(fmt, __VA_ARGS__)` は書式文字列だけで呼ぶと
           `Printf("...", )` になる。MSVC / clang-cl は独自拡張で通すが標準準拠の clang は落ちる。
           丸ごと転送する形(`Printf(__VA_ARGS__)`)に変更(`__VA_OPT__` は MSVC の従来
           プリプロセッサが未対応なので使わない)
        5. **`strncpy_s` + `_TRUNCATE` の残り 5 箇所**(`UIAnimationEditor` / `TextStyleEditorPanel`)。
           P0 の `*_s` 置換から漏れていた。`std::snprintf(buf, sizeof(buf), "%s", …)` に置換
        6. **`ENGINE_GRAPHICS_Vulkan` と `ENGINE_GRAPHICS_VULKAN` の食い違い**。CMake が
           `ENGINE_GRAPHICS_${AQ_GRAPHICS_API}` をそのまま定義していた。D3D11 / D3D12 は元から
           大文字なので**この取り違えは Vulkan 構成でしか表面化しない**(D3D12 の未定義シンボルに
           化けてリンクエラーになる)。`string(TOUPPER …)` を挟んだ
      - **ObjC++ TU の衝突**も 1 件。`aq.h`(PCH)→ DirectXTex → `wsl/winadapter.h` →
        スタブ `basetsd.h` が `BOOL` を uint32_t に typedef し `interface` を struct に #define するため、
        Cocoa の `typedef bool BOOL` と衝突し `@interface` が全滅する。`aq.h` で
        `__OBJC__` のときだけ DirectXTex を持ち込まないようにした(§10 の「`.mm` は
        Platform/Mac・HID/Mac・Sound/CoreAudio に閉じる」の帰結として、画像デコードには触らない)
      - **`-G Xcode` は未検証**。この環境は Command Line Tools のみでフル Xcode.app が無く、
        Xcode ジェネレータが使えない(§8-14)
      - クリーンビルドの警告は **35 件**。当初 939 件だったが、901 件は `vk_mem_alloc.h` からの
        `-Wnullability-completeness` だったため、`vma` の INTERFACE インクルードを `SYSTEM` に
        変更して黙らせた(Engine.vcxproj が ThirdParty を `/external:I` で渡しているのと同じ意図)。
        残り 35 件は §8-7 に挙げた clang 警告と同種で、自前コード側にある
- [x] Vulkan(MoltenVK)で描画され、Vulkan validation layer で**エラー 0**
      - クリア画面どころか **AquaDash のタイトル画面まで到達**(海/UI/フォント/PNG サムネイルが表示)。
        `.spv` 事前生成のみで全シェーダが生成でき、`BuildInputLayout()`(SPIRV-Reflect)も通った
      - validation の**警告は 10 件残る**。すべて同一 VUID(ストレージイメージのフォーマット不一致。
        SPIR-V が `Rgba32f` を宣言しているのに実際のビューは `R16G16B16A16_SFLOAT` 等)。
        Vulkan バックエンド共通の問題で Mac 固有ではない → §8-15
      - HiDPI は当初 `VUID-VkRenderingInfo-pNext-06079/06080` の**エラー**として出た(スワップチェーン
        2560x1440 対 深度 1280x720)。§8-13 の決定で解消
- [x] 閉じるボタンで `PumpEvents` が false を返し、`Finalize` まで到達してプロセスが正常終了する
      - 終了コード 0 / validation エラー 0(起動から終了まで通して)。補助アクセスを許可して
        `osascript` で閉じるボタンをクリックする形で自動確認した
      - ウィンドウを閉じる経路自体は最初から動いていたが、**終了処理が 3 つの理由でクラッシュしていた**。
        いずれも Mac 固有ではなく、**Windows にも同じ地雷がある**(D3D は VMA のような
        未解放アサートを持たないため表面化していなかっただけ):
        1. **GPU の完了を待たずにリソースを破棄していた**。`RenderThread::WaitForCompletion` は
           CPU 側(コマンド積み)の完了しか見ないため、最後のフレームが GPU で走ったまま
           ImGui / UIContext / ResourceManager が壊しにいき、validation が
           「currently in use by VkCommandBuffer」を 12 件並べた。`Application::Finalize` で
           レンダースレッド停止直後に `vkDeviceWaitIdle` を挟んだ(Vulkan 構成のみ)
        2. **`void*` に対する `delete` でデストラクタが走っていなかった**。`ResourceBase::data_` は
           `void*` で、`MeshResource` / `PMDResource` / `GPUResource` / `ShaderResource` の 4 つが
           `delete data_;` と書いていた。**メモリは解放されるがデストラクタは呼ばれない**ため、
           `TextureData::~TextureData` が動かず SRV(= VkImage + VMA アロケーション)が
           丸ごと漏れる。同じファイルの新しいリソース型(SkeletalMesh / Animation /
           ParticleSystem / SoundClip)は既に `delete static_cast<T*>(data_)` と書いてあり、
           **この 4 つだけ取り残されていた**ので揃えた
        3. **関数ローカル static のキャッシュがデバイスより長生きしていた**。
           `FontAssetCache`(フォントアトラスのテクスチャ)と `GpuClusterCuller`(compute
           シェーダ 2 本)はプロセス終了まで生き残るため、`vkDestroyDevice` の後に解放される。
           前者は `UIContext::Finalize` で `Clear()`、後者は `Finalize()` を新設して
           `Application::Finalize` から呼ぶようにした
      - **Windows 側の回帰確認が要る**。2 と 3 はプラットフォーム非依存の変更で、
        「今まで呼ばれていなかったデストラクタが呼ばれるようになる」ため、
        解放後のポインタを使っている箇所があれば Windows で表面化しうる
- [x] `StartupLog` に portability subset の非対応項目が出力される
      - `[vulkan] VK_KHR_portability_subset enabled` に続き `unsupported: pointPolygons` /
        `tessellationIsolines` / `tessellationPointMode` の 3 件。いずれも本エンジンは未使用
- [ ] Windows Vulkan 構成で `.spv` あり/なし双方で全シーンが描画され、見た目が一致する(P1 から移動)
      - **Windows 側の作業**。Mac では実行時 DXC 経路が無いため「なし」側を作れない。
        Mac 実機では `.spv` だけで全シェーダが生成できることを確認済み(上記)
- [x] Retina 環境での `drawableSize` と描画解像度の扱いを §8-13 に記録した

### P3: Mac で実シーン(Mac 実機)

実装:
- `.spv` をビルド時生成(§4)。アセットの `GetContentRoot` 経由読み込み。
- Windows 版と同じ AquaDash を起動。

評価:
- [ ] タイトル〜ステージまで Windows Vulkan 構成と同じ見た目(海・キャラ・影・Bloom・UI・デカール・草)
- [ ] validation layer エラー 0。フレーム時間を Windows(同等 GPU クラス)と比較して記録
- [ ] 未対応機能が出た場合、`IsComputeSupported()` 等の既存ゲートで落ちて描画が破綻しない
- [ ] 全 `.fx` × エントリの `.spv` がビルド時に生成される(`shader_entries.txt` に漏れなし)

### P4: Mac で入力・サウンド・デバッグ UI(Mac 実機)

実装:
- §3.2 の Cocoa 入力 + `GameControllerPadBackend`。
- §5 の `CoreAudioSoundBackend`/`CoreAudioSoundVoice`/`ExtAudioFileDecoder` を `SoftwareMixer` に接続。
- `imgui_impl_osx`。

評価:
- [ ] キーボード/マウス/パッドで AquaDash がプレイでき、長押し・トリガー判定が Windows と同じ
- [ ] BGM(mp3/wav)・SE・3D 音源が再生され、ピッチ/パン/バス音量が反映される。停止・一時停止・自然終了の回収が動く
- [ ] `SoundStream` の A/V 同期指標が Windows と同等の範囲
- [ ] ImGui のデバッグ UI が表示・操作でき、`SuppressKeyboard/Mouse` がゲーム入力と排他になる
- [ ] Windows 側の回帰なし(D3D12/Vulkan/UWP がビルド・動作)

### P5: 配布形態(任意)

- `.app` バンドル(Assets を `Resources` へ、`libMoltenVK.dylib`/`libvulkan.dylib` を `Frameworks` へ、rpath)。コード署名は範囲外。

評価:
- [ ] ソースツリー外にコピーした `.app` 単体で起動する

### P6: ネイティブ Metal(道B、別設計書)

- `MetalBackend設計.md` を起こしてから着手。§7 の方針を継承。

---

## 10. チェックポイント(設計全体)

- [ ] 抽象の直交性: `AQ_PLATFORM_*` と `ENGINE_GRAPHICS_*` が独立し、Mac × Vulkan / Win32 × Vulkan / Mac × Metal(将来)が成立する
- [ ] API/OS 固有型の漏れ: `HWND`/`NSView`/`CAMetalLayer`/`LPDIRECTINPUT*`/`IMF*` が `Platform/<OS>/`・`HID/<OS>/`・`Sound/<Backend>/`・`Graphics/<API>/` の外に現れない
- [ ] `.mm` が `Platform/Mac/`・`HID/Mac/`・`Sound/CoreAudio/`・`Sound/Decoder/ExtAudioFileDecoder.mm`・`MacMain.mm` に閉じている
- [ ] 各 Bridge(入力/サウンド/画像)に Null 実装があり、未対応プラットフォームでも落ちずに起動する
- [ ] 抽象IF(`IGraphicsDeviceImpl`/`IRenderContextImpl`/`IPlatform`/`ISoundBackend`/`IPadBackend`)への追加が 0 本(`IPlatform` は追加なし。`IKeyboardBackend`/`IMouseBackend` は新設)
- [ ] `.fx` が無改変
- [ ] 設計と実装の食い違いは本書へ反映し、`対象コミット` を更新した
