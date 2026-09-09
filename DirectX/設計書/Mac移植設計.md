# Mac(Metal)移植 設計

> 対象コミット: 6dd3e7f / 最終更新: 2026-09-09

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
| `aqEngine/HID/Win32/DirectInputKeyboardBackend.{h,cpp}` / `DirectInputMouseBackend.{h,cpp}` **(新規・移設)** | 現 `Input.cpp` の DirectInput 部分。`DirectInput8Create`/`SetCooperativeLevel(HWND)`/`GetDeviceState` と、**DIK → `KeyBoardType` の変換表**。`::GetCursorPos`+`ScreenToClient` もここ |
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
| `aqEngine/Graphics/Vulkan/VulkanCommon.h` | `VK_USE_PLATFORM_WIN32_KHR` を `AQ_PLATFORM_WIN32` 限定に。MAC では `VK_USE_PLATFORM_METAL_EXT` |
| `VulkanGraphicsDeviceImpl.cpp` `CreateInstance` | インスタンス拡張: WIN32 = `VK_KHR_win32_surface`、MAC = `VK_EXT_metal_surface` + `VK_KHR_portability_enumeration`、flags に `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` |
| 同 `CreateSurface(void*)` | MAC では `vkCreateMetalSurfaceEXT`(`pLayer = static_cast<CAMetalLayer*>(handle)`)。引数名 `hwnd` → `nativeWindow` |
| 同 `CreateDevice` | MAC ではデバイス拡張に `VK_KHR_portability_subset` を追加。`VkPhysicalDevicePortabilitySubsetFeaturesKHR` を照会し、非対応項目(`triangleFans`, `imageViewFormatSwizzle`, `separateStencilMaskRef` 等)を `StartupLog` に出す |
| 同 スワップチェーン | `VK_PRESENT_MODE_FIFO_KHR` を第一候補に(MoltenVK は MAILBOX を返さない場合がある)。`minImageCount` は capabilities 準拠(既存どおり) |
| `VulkanShader.{h,cpp}` | **`.spv` 読み込み経路を追加**: `CreateShader(path, entry, type)` はまず `<shaderDir>/spv/<stem>.<entry>.<type>.spv` を探し、あれば `vkCreateShaderModule` + SPIRV-Reflect(既存)。無ければ従来の DXC 実行時コンパイル(`#if defined(AQ_PLATFORM_WIN32)` 内)。`<wrl/client.h>`/`MultiByteToWideChar` は DXC 分岐内に閉じる |
| `Tools/ShaderCompile/compile_spv.cmake` **(新規)** | `.fx` × エントリ一覧 → `dxc -spirv -fspv-entrypoint-name=main -E <entry> -T <vs\|ps\|cs>_6_0 -fvk-b-shift 0 all -fvk-t-shift 16 all -fvk-s-shift 32 all -fvk-u-shift 48 all -I <shaderDir>`(VulkanShader.cpp と**同じ引数**。引数は `dxc_args.txt` 1 ファイルに集約し両者が参照)。エントリ一覧は `Game/Assets/Shader/shader_entries.txt`(新規、`<file> <entry> <stage>` 行) |

抽象IF(`IGraphicsDeviceImpl`/`IRenderContextImpl`)・呼び出し側・`.fx` の変更は **0**。

---

## 5. サウンド

| ファイル | 責務 |
|---|---|
| `aqEngine/Sound/Mixer/SoftwareMixer.{h,cpp}` **(新規・可搬)** | 論理ボイスの集合を出力フォーマット(48kHz float, 2ch 想定)へミックスするプラットフォーム非依存ミキサ。ボイスごとに: 投入バッファキュー(コピー)/`RefSoundClip` 参照(ゼロコピー)/ループ領域/線形リサンプル(`SetFrequencyRatio`)/出力行列(`SetOutputMatrix`)/ボリューム/消費フレーム数。バス音量とマスタ音量。`Render(float* out, uint32_t frames)` を出力スレッドから呼ぶ。ロックは投入側と Render 側で SPSC リング + `std::atomic`(architecture.md §5) |
| `aqEngine/Sound/CoreAudio/CoreAudioSoundBackend.{h,mm}` **(新規)** | `ISoundBackend` 実装。`AudioUnit`(`kAudioUnitSubType_DefaultOutput`)を 1 つ開き、render callback で `SoftwareMixer::Render`。`GetOutputClock` は render callback の `AudioTimeStamp.mHostTime` + 累積フレームから算出。`CreateVoice` は `SoftwareMixer` に論理ボイスを追加して `CoreAudioSoundVoice` を返す |
| `aqEngine/Sound/CoreAudio/CoreAudioSoundVoice.{h,cpp}` **(新規)** | `ISoundVoice` 実装。全メソッドを `SoftwareMixer` の論理ボイス操作に委譲する薄いアダプタ |
| `aqEngine/Sound/Decoder/ExtAudioFileDecoder.{h,mm}` **(新規)** | `ISoundDecoder` 実装(AudioToolbox `ExtAudioFile`)。mp3/aac/m4a を PCM へ。`MFDecoder` と同じ静的 `DecodeFileFully` も提供 |
| `aqEngine/Sound/SoundBackend.h` | `#elif defined(AQ_PLATFORM_MAC)` → `SOUND_BACKEND_COREAUDIO` |
| `aqEngine/Sound/Decoder/CompressedDecoder.h` **(新規)** | 「wav 以外」用デコーダの選択ヘッダ。WIN32/UWP = `MFDecoder`、MAC = `ExtAudioFileDecoder`。`SoundClip.cpp:50`・`SoundEngine.cpp:33` の `MFDecoder` 直参照をこれ経由に |
| `aqEngine/Sound/Video/VideoPlayer.{h,cpp}` | 本体を `#if defined(AQ_PLATFORM_WIN32) \|\| defined(AQ_PLATFORM_UWP)` で囲み、MAC は `Open` が false を返す Null 動作 |

`SoundEngine`/`Mixer3D`/`SoundStream`/`AudioDirector` は無変更。

---

## 6. リソース・ThirdParty・imgui

| ファイル | 変更 |
|---|---|
| `ThirdParty/stb/stb_image.h` **(新規・同梱)** | PNG/JPG デコード |
| `aqEngine/Resource/ImageLoader.{h,cpp}` **(新規)** | `LoadImageFile(path) → ScratchImage`(DirectXTex 型は維持)。拡張子で DDS/TGA は DirectXTex、PNG/JPG は WIN32/UWP なら `LoadFromWICFile`、MAC なら `stb_image` → `Image` 構造体へ詰めて `ScratchImage::InitializeFromImage`。`Resource.cpp:1607-1625`・`Terrain/HeightmapChunk.cpp:50-133` の直呼びをこれに集約。引数は `std::string`(UTF-8)で受け、呼び出し元の `mbstowcs_s` による `wchar_t` パス変換(`Resource.cpp:1605`・`HeightmapChunk.cpp:47`)を本ローダ内へ吸収する |
| `ThirdParty/DirectXTex` | 非 Windows 経路(`DirectXTexDDS/TGA/HDR/Convert/Resize/Mipmaps/BC*`)を CMake で選択。`DirectXTexWIC.cpp`・`BCDirectCompute`・D3D11/12 系 cpp は Windows のみ。`sal.h` 互換は `ThirdParty/DirectX-Headers/include/wsl/` を同梱 |
| `ThirdParty/DirectXMath` **(新規・同梱)** | Mac には Windows SDK が無いため、`<DirectXMath.h>` の入手元を SDK から同梱ソースへ移す(Microsoft/DirectXMath)。コードは無改変。Windows は従来どおり SDK 版を使ってもよいが、版ずれを避けるため同梱側に一本化する |
| `ThirdParty/DirectX-Headers` **(新規・同梱)** | 非 Windows 用の `sal.h`(`include/wsl/`)と `directx/dxgiformat.h`。DirectXTex と、`DXGI_FORMAT` が D3D バックエンド外へ漏れている 4 ファイル(`Resource.cpp`・`Terrain/HeightmapChunk.cpp`・`HeightmapPainter.cpp`・`SplatmapPainter.cpp`)が要求する |
| `ThirdParty/BulletPhysics` | Mac は `add_subdirectory(src)`(`BT_USE_DOUBLE_PRECISION`/`BT_THREADSAFE=1`)。Windows は既存 prebuilt `.lib` 維持 |
| `ThirdParty/imgui/imgui_impl_osx.{h,mm}` **(新規・同梱)** | imgui 本体(1.92 WIP)と同じ版のものを取得 |
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
7. **HiDPI**: `CAMetalLayer.drawableSize` と `InitializeParameter` の描画解像度の関係(Retina で 2 倍になる)。P2 で決める。

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
- [ ] CMake → clang-cl 構成でエンジンと Game が**コンパイル・リンク**できる(実行は任意)
      - **保留**: VS の「C++ Clang tools for Windows」コンポーネントが未導入で `clang-cl` が存在しない。導入後に `--preset windows-clang-cl` で検証する
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
- §4 の `.spv` 読み込み経路と `compile_spv.cmake`/`shader_entries.txt`。Windows Vulkan 構成で `.spv` 有無の両方を検証。

評価:
- [ ] キーボード/マウス/パッドの全操作(AquaDash のタイトル〜ステージ)が P0 と同一に動く
- [ ] `Input.h` に `dinput.h`/`Xinput.h`/`HRESULT`/`LPDIRECTINPUT*` が現れない
- [ ] UWP 構成が Null 入力でビルド・起動する
- [ ] `Tools/MixerTest` で 2 ボイス(片方ピッチ 1.5、出力行列で左右反転)のミックス結果が期待波形になる
- [ ] Windows Vulkan 構成で `.spv` あり/なし双方で全シーンが描画され、見た目が一致する
- [ ] `ImageLoader` 経由で PNG/JPG/DDS/TGA のテクスチャが従来どおり表示される

### P2: Mac でウィンドウとクリア画面(Mac 実機)

実装:
- `MacMain.mm`、`PlatformMac.{h,mm}`、`PlatformBudget` の MAC プロファイル。
- CMake の Xcode/Ninja 生成。ThirdParty(Bullet/DirectXTex 非 Windows/ufbx/spirv_reflect/vma)の Mac ビルド。
- Vulkan §4 の Mac 分岐(Metal surface / portability)。Vulkan SDK for macOS の導入手順を `Tools/SetupVulkan/README.md` に追記。
- 入力 = Null、サウンド = `CoreAudioSoundBackend` の骨格(`Initialize` 成功・無音)、`ImageLoader` の `stb_image` 分岐。

評価:
- [ ] Mac(Apple Silicon)で `cmake -G Xcode` / `-G Ninja` からビルド・リンクが通る
- [ ] ウィンドウが開き、Vulkan(MoltenVK)でクリア色が出る。Vulkan validation layer でエラー 0
- [ ] 閉じるボタンで `PumpEvents` が false を返し、`Finalize` まで到達してプロセスが正常終了する
- [ ] `StartupLog` に portability subset の非対応項目が出力される
- [ ] Retina 環境での `drawableSize` と描画解像度の扱いを §8-7 に記録した

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
