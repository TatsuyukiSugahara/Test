# Mac(Metal)移植 調査メモ

対象: `aqEngine/` + `Game/`。Windows(Win32/UWP)専用の現状を、macOS(Apple Silicon)で Metal 描画させるために何が必要かを棚卸しする。
姉妹文書: [Xbox移植設計.md](Xbox移植設計.md)(`IPlatform` 抽象はここで導入済)、[VulkanBackend設計.md](VulkanBackend設計.md)。
本書は **調査結果と方針案のみ**。実装コードは含まない。調査日: 2026-09-08。
決定した設計・責務表・フェーズ計画は [Mac移植設計.md](Mac移植設計.md) が一次資料(本書 §7 のフェーズ案はそちらで確定版に置き換え)。

---

## 0. 結論(先に)

- **Metal に到達する道は 2 本**。どちらでも「Main などプラットフォーム層の Mac 対応」は共通で必要。
  - **道A: 既存 Vulkan バックエンド + MoltenVK**(Vulkan→Metal 変換層)。グラフィックス側の追加コードはほぼ「サーフェス生成の差し替え」だけ。Vulkan バックエンドは P4 まで実機動作済なので最短で絵が出る。
  - **道B: ネイティブ Metal バックエンド**(`MetalGraphicsDeviceImpl` / `MetalRenderContextImpl` を新規実装)。Vulkan 実装(22 ファイル・cpp 約 2,800 行)と同規模の新規実装 + HLSL→MSL のシェーダ変換経路が必要。
- **推奨: 道A で Mac 上に絵を出し、その後に道B を積む**。道A で「プラットフォーム層/入力/サウンド/ビルド」の Mac 化を先に片付けられるため、道B はグラフィックスだけに集中できる。道B は D3D12→Vulkan と同じ「抽象IF不変で実装を横に足す」手順がそのまま使える。
- **最大の作業はグラフィックスではなく足回り**: ビルドシステム(vcxproj のみ)、`aq.h`(PCH)の Windows 直依存、DirectInput 入力、XAudio2/Media Foundation サウンド、DirectXTex(WIC)。
- **良い知らせ**: バックエンド外 422 ファイルのうち Windows/MSVC 依存があるのは 58 ファイル(13.7%)。スレッド/同期は 100% `std::`、文字列も `char` ネイティブ、`__declspec` 等の MSVC 拡張はゼロ。**86% のファイルは無改変で clang を通る見込み**。

---

## 1. 現状の構造(移植に関係する部分)

| 層 | 現状 | Mac での扱い |
|---|---|---|
| エントリ | `Game/Application/Main.cpp`(`WinMain`, 35 行)/ `UWPMain.cpp` | `MacMain.mm`(`main` + `NSApplication`)を追加。両エントリとも 30 行程度の同型ブートストラップなので 3 本目を足すだけ |
| プラットフォーム抽象 | `IPlatform`(`CreateMainWindow`/`PumpEvents`/`OnSuspend`/`GetContentRoot`)。Win32/UWP 実装。**Win32 は `!AQ_PLATFORM_UWP` でしか表現されていない**(45 箇所) | `PlatformMac.mm` を追加(NSWindow + CAMetalLayer 付き NSView)。`AQ_PLATFORM_WIN32`/`AQ_PLATFORM_MAC` を新設し負のガードを置換 |
| Engine | `IPlatform*` 経由。**例外: `Engine::GetHWND()`**(imgui win32 / DirectInput の `SetCooperativeLevel` が使用)。`CoInitializeEx` を Engine.cpp で呼ぶ | `GetNativeWindow()`(void*)へ一般化。COM 初期化は Windows ブロックへ |
| 描画抽象 | `IGraphicsDeviceImpl`(約 25 メソッド)/ `IRenderContextImpl`(約 50 メソッド)。Bridge。D3D11(12 ファイル)/D3D12(23)/Vulkan(22) | Metal 実装を 4 本目として追加可(道B)。抽象IF は追加 0 本を目標 |
| `ENGINE_GRAPHICS_*` の漏れ | バックエンド外 5 ファイル: `aq.h`, `Core/Application.cpp`, `Engine.cpp`, `Graphics/RenderContext.cpp`, `Rendering/ImGuiRenderCommand.cpp` | 道B ではこの 5 箇所に `ENGINE_GRAPHICS_METAL` 分岐を追加 |
| Vulkan 実装 | Vulkan 1.3 core(dynamic rendering / synchronization2 / scalarBlockLayout)、VMA、DXC 実行時コンパイル、SPIRV-Reflect、imgui 自前描画。**唯一の Win32 依存は `VK_KHR_win32_surface`**(`CreateSurface(hwnd)`)と `VulkanShader.cpp` の `wrl/client.h`+`MultiByteToWideChar` | MoltenVK 上でそのまま動く見込み(§3) |
| シェーダ | `Game/Assets/Shader/*.fx` 41 本 + 共有 CB ヘッダ 7 本(HLSL, VS/PS/CS のみ。GS/HS/DS なし) | Metal は GS 非対応だが未使用なので障害なし |
| 入力 | **キーボード/マウスは DirectInput 直書き**(`HID/Input.h` の public メンバに `LPDIRECTINPUT8`)。パッドは `IPadBackend` で抽象化済(Win32 = XInput + DualSense HID 直読みの合成 `Win32PadBackend` / UWP = WinRT)。DualSense 実装(5db2a5a)は `setupapi`/`hidsdi` と `#pragma comment(lib)` 2 本を追加(§6 の集計はこれを含まない) | キーボード/マウス用の抽象IFを新設 + Cocoa イベント実装。パッドは GameController.framework 実装を `IPadBackend` に追加(DualSense のアダプティブトリガーも `GCDualSenseAdaptiveTrigger` で対応可) |
| サウンド | `ISoundBackend`/`ISoundVoice` 抽象あり(Windows シンボル 0)。`SoundBackend.h` に `#elif defined(__ANDROID__)` のスロットが既にある。実装は XAudio2 のみ。デコーダは WAV(可搬) + Media Foundation。`Mixer3D` は X3DAudio 非依存。`VideoPlayer` は MF | CoreAudio/AVAudioEngine 実装を追加(約 4 ファイル・エンジン側変更なし)。MF デコーダ/動画は AVFoundation か機能落ち |
| 数学 | DirectXMath。`Math/Vector.h`/`Matrix.h` がラップするが `XMFLOAT4X4`/`XMMATRIX` が public 型に露出。`Math/` 外で 14 ファイルが直接使用 | **DirectXMath 自体は可搬**(ヘッダオンリ、clang/ARM64 NEON 対応。非 Windows では `sal.h` 互換ヘッダが要る)。置き換え不要、ビルドが通ることの確認のみ |
| テクスチャ | DirectXTex をソース同梱ビルド。`LoadFromDDSFile`/`LoadFromTGAFile`/**`LoadFromWICFile`**/`GenerateMipMaps`。実使用箇所は `Resource.cpp:1607-1625` と `Terrain/HeightmapChunk.cpp:50-133` の 2 点 | Linux 対応済の非 Windows 経路(DDS/HDR/TGA + BC ソフトコーデック)でビルド。**WIC(PNG/JPG)は使えない**ので `stb_image` を追加 |
| 物理 | Bullet(Windows 用 prebuilt `.lib`。ソース + CMakeLists 同梱) | CMake でソースからビルド |
| メモリ | `_aligned_malloc`/`_aligned_free`(4 ファイル・13 箇所、`<malloc.h>`) | `posix_memalign`/`std::aligned_alloc` へ |
| 時間 | `GameTimer`/`Profiler` が `QueryPerformanceCounter`/`LARGE_INTEGER`/`Sleep(1)`/`GetCurrentThreadId` | `std::chrono::steady_clock`/`std::this_thread` へ(2 ファイル) |
| その他 ThirdParty | imgui(win32/dx11/dx12 impl。**core 1.92 WIP と同梱 impl はバージョン不整合**のため D3D12/Vulkan は自前描画)、ufbx、spirv_reflect、vma | imgui は `imgui_impl_osx` を追加(core とバージョンを揃える)。他は可搬 |
| ビルド | `.vcxproj` 3 本(Engine/DirectX/GameUWP)+ `GameSources.props` + NuGet。C++20、PCH=`aq.h`、`/utf-8`。**CMake 等は無し** | CMake を新設(§5) |

---

## 2. プラットフォーム層(「Main など」)で必要なこと

### 2.1 エントリ & ウィンドウ
- `MacMain.mm`: `main()` → `NSApplication` 生成 → `PlatformMac` を注入して `Engine::Create/Initialize/RunGame/Finalize`(`Main.cpp` と同じ流れ)。
- `PlatformMac`(Objective-C++, `.mm`): `CreateMainWindow` = `NSWindow` + `wantsLayer=YES` な `NSView` に `CAMetalLayer` を張り、`void*` で **CAMetalLayer**(または NSView)を返す。`PumpEvents` = `nextEventMatchingMask:untilDate:nil` ループ。`GetContentRoot` = 開発中はソースツリー(`FindProjectRoot` と同じ探索)/配布時は `.app/Contents/Resources`。
- `NativeWindowHandle` は既に `void*` なので、`IGraphicsDeviceImpl::Initialize(window,…)` の型変更は不要。HWND へキャストしている箇所(Vulkan `CreateSurface`, imgui win32, DirectInput)だけ差し替え。
- `PlatformBudget.h` に Mac 用プロファイルを追加(既存の `AQ_PLATFORM_UWP` 分岐と同じ形)。

### 2.2 `aq.h`(PCH)の分割 ★最優先・最大レバレッジ
`aq.h` は Engine の PCH で、バックエンド外 422 ファイル中 **145 ファイルが include**。無条件に `<windows.h>` / `<dinput.h>` / `<tchar.h>` / DirectXTex / DirectXMath / `#pragma comment(lib)`×8 / `#pragma warning`×13 を含む。実際に Windows シンボルを使うのは 58 ファイルなので、**分割だけで約 87 ファイルが「無改変で通る」側に移る**。
- `#if defined(_WIN32)` で Windows 専用ブロックを囲み、`AQ_PLATFORM_MAC` を追加。
- `Utility.h`(`aq.h` から include。`windows.h`/`tchar.h`/`ZeroMemory`/`OutputDebugStringA`/`_T(`/`L""`/`_wassert`)も同時に可搬化。
- `#pragma comment(lib)` は MSVC 専用 → CMake の `target_link_libraries` へ移す(Windows でも問題なし)。
- `ENGINE_GRAPHICS_*` に `ENGINE_GRAPHICS_METAL` を足す(道B 時)。
- clang は `-include` で強制ヘッダを指定できるので PCH の**仕組み**はそのまま使える。問題は**中身**だけ。

### 2.3 Windows API 直依存の置き換え(棚卸し詳細は §6)
- 文字列: `*_s` 系 CRT(`sprintf_s` 12 / `strncpy_s` 8 / `fopen_s` 9 / `strcpy_s` 1 / `vsprintf_s` 1)= **約 33 箇所・12 ファイル**。`snprintf`/`fopen` へ、または `Util/Portable.h` に薄いラッパ。`wchar_t`/`MultiByteToWideChar` は DirectXTex のワイドパス変換 2 箇所のみで、DirectXTex 経路の置換と同時に消える。
- `OutputDebugString`(5 ファイル 8 箇所)→ `fprintf(stderr)`。`MessageBox`/`__debugbreak`/SEH は**使用なし**。
- スレッド/同期: Win32 プリミティブ **0 件**(`std::mutex` 15 / `std::atomic` 8 / `std::thread` 4 ファイル)。対応不要。
- ファイル I/O: `CreateFile`/`FindFirstFile` 等 **0 件**。`std::filesystem`/`fopen_s` のみ。バックスラッシュ固定パスは `Application.cpp:87`(`"C:\\Windows"`)と `PlatformUWP.cpp:184` の 2 箇所だけ。
- COM: `CoInitializeEx`(Engine.cpp / XAudio2 / MFDecoder)、`ComPtr`(MFDecoder)。XAudio2/MF/WIC を置換すると COM 依存は全消滅。

### 2.4 入力
- キーボード/マウス: `HID/Input.h` が `<dinput.h>` を file scope で include し、`KeyBoard`/`Mouse`/`InputManager` の public メンバに `LPDIRECTINPUT8`/`LPDIRECTINPUTDEVICE8`/`HRESULT` を持つ。**ここだけ Bridge が抜けている**。`IKeyboardBackend`/`IMouseBackend`(状態配列を埋める最小IF)を切り出し、Win32 = DirectInput、Mac = `NSEvent`(keyDown/keyUp/flagsChanged/mouseMoved)。`Input.cpp` の `::GetCursorPos`+`ScreenToClient` も同 IF へ。
- ゲームパッド: `GameControllerPadBackend`(GameController.framework, `GCController`)を `IPadBackend` 実装として追加し、`PadBackend.h` の選択に `__APPLE__` を足す。

### 2.5 サウンド
- `ISoundBackend`/`ISoundVoice` 実装を **AVAudioEngine**(または CoreAudio AudioUnit)で追加(`CoreAudioSoundBackend`/`CoreAudioSoundVoice` 約 4 ファイル)。`SoundBackend.h` に `#elif defined(__APPLE__)` を追加。3D は既に `Mixer3D` がエンジン側にあるので、バックエンドは「PCM ソースボイスの再生・ボリューム・ピッチ」相当で足りる想定(XAudio2 実装の機能一覧に合わせる。§8-4)。
- `MFDecoder`(MP3/AAC 等)→ `AudioToolbox`(`ExtAudioFile`)実装。まずは WAV のみで起動可(`WavDecoder`/`WavStreamDecoder` は可搬)。
- `VideoPlayer`(MF)→ AVFoundation(`AVPlayerItemVideoOutput`)か、初期は無効化。

### 2.6 ThirdParty
- **DirectXTex**: Linux 向けと同じ非 Windows 経路(DDS/HDR/TGA + BC ソフトウェア codec、C++17)でビルド。`sal.h` 等は `DirectX-Headers` の互換ヘッダで解決。**`LoadFromWICFile`(PNG/JPG)を `Resource.cpp` と `HeightmapChunk.cpp` が使っている**ため `stb_image` を追加してその 2 点を分岐。BC 圧縮テクスチャ自体は Apple Silicon Mac の Metal が BC1〜7 をサポートするのでそのまま使える。
- **Bullet**: 同梱ソースを `add_subdirectory` でビルド(`BT_USE_DOUBLE_PRECISION`/`BT_THREADSAFE=1` を揃える)。
- **imgui**: `imgui_impl_osx.mm` を追加。描画側は D3D12/Vulkan と同様に自前(`MetalImGui`)。`ImGui_ImplWin32_*` は 3 ファイル(`Core/Application.cpp`, `Rendering/ImGuiRenderCommand.cpp`, `Platform/PlatformWin32.cpp`)で無条件使用なので `#ifdef` 化。
- **DXC**: macOS 向け `libdxcompiler.dylib`(公式リポジトリにビルド手順、コミュニティ prebuilt あり)。実行時コンパイルを続けるならこれをバンドル。**推奨は Windows 側でオフラインコンパイルして `.spv` を同梱**し、Mac 実行時の DXC 依存を消す(Vulkan の `CreateShader` に `.spv` キャッシュ読み込み経路を追加。既存の実行時コンパイルは Windows で維持)。

---

## 3. 道A: Vulkan + MoltenVK

### 3.1 MoltenVK の対応状況(2026-09 時点)
- MoltenVK 1.3(2025-05)で Vulkan 1.3、1.4(2025-08)で Vulkan 1.4 に対応。dynamic rendering / synchronization2 / scalar block layout / copy_commands2(`vkCmdBlitImage2`)/ maintenance1(負ビューポート高さ)いずれもサポート。
- LunarG Vulkan SDK(macOS)に MoltenVK と、Metal 4 上の完全準拠実装 **KosmicKrisp**(Apple Silicon + macOS 26 必須)が同梱。当面は MoltenVK を既定、KosmicKrisp は Vulkan 1.3 完全準拠が要る場面の代替。
- 既知の制限: pipeline statistics クエリ非対応、`VkAllocationCallbacks` 無視、PVRTC の制約。**本エンジンの使用機能には抵触しない**。

### 3.2 Vulkan バックエンド側の変更点
1. `VulkanCommon.h`: `VK_USE_PLATFORM_WIN32_KHR` → `#ifdef __APPLE__` で `VK_USE_PLATFORM_METAL_EXT`。
2. `CreateSurface`: `vkCreateWin32SurfaceKHR` → `vkCreateMetalSurfaceEXT`(`VkMetalSurfaceCreateInfoEXT.pLayer = CAMetalLayer*`)。インスタンス拡張を `VK_EXT_metal_surface` に差し替え。
3. インスタンス生成: `VK_KHR_portability_enumeration` を有効化し `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` を立てる。デバイス拡張に `VK_KHR_portability_subset` を追加(Loader 経由時に必須)。
4. `VulkanShader.cpp`: `<wrl/client.h>`/`ComPtr`/`MultiByteToWideChar` を排除(DXC 同梱の `WinAdapter.h` が `CComPtr` と `wchar_t` 互換を提供)。または §2.6 の `.spv` 同梱で DXC を Mac から外す。
5. 検証: 負ビューポート高さの Y-flip、`scalarBlockLayout` の SSBO レイアウト、`Texture2DArray` 深度スライス(layered depth)。portability subset の非対応項目(`triangleFans`, `imageViewFormatSwizzle` 等)を初期化時にチェックしてログに出す。

道A のグラフィックス変更は上記 5 点で、抽象IF・呼び出し側・`.fx` の変更は 0。

---

## 4. 道B: ネイティブ Metal バックエンド

### 4.1 実装物(Vulkan フォルダの写像)
| Vulkan | Metal | 備考 |
|---|---|---|
| `VulkanGraphicsDeviceImpl` | `MetalGraphicsDeviceImpl` | `MTLDevice`/`MTLCommandQueue`/`CAMetalLayer`(swapchain 相当)/frames-in-flight は `dispatch_semaphore` |
| `VulkanRenderContextImpl` | `MetalRenderContextImpl` | `MTLRenderCommandEncoder` は「RT 切替=エンコーダ終了/再開」で dynamic rendering と同型。保留ステート→Draw で flush 方式をそのまま踏襲 |
| `VulkanPipelineCache` | `MetalPipelineCache` | `MTLRenderPipelineState`(VS/PS/blend/RT formats)+ `MTLDepthStencilState`(DepthMode)を別キャッシュ |
| `VulkanPipelineLayout`/descriptor | argument table 直接バインド | Metal は `setVertexBuffer:offset:atIndex:` 等でスロット直指定。register→index 写像は Vulkan の shift 規約(b:0/t:16/s:32/u:48)をそのまま MSL 側の `[[buffer(n)]]`/`[[texture(n)]]` に流用可 |
| `VulkanBuffers` | `MetalBuffers` | `MTLBuffer`(shared/managed)+ フレームリング |
| `VulkanResources` | `MetalResources` | `MTLTexture`/`MTLSamplerState`/UAV は `MTLTexture` の書き込みアクセス |
| `VulkanRenderTarget`/`DepthMap` | 同名 | `MTLRenderPassDescriptor` 経由。layered depth は `renderTargetArrayLength`/slice 指定 |
| バリア追跡 | 不要(Metal が自動追跡、`MTLFence`/`memoryBarrier` は compute→draw の UAV 用のみ) | 実装量減 |
| `VulkanShader`(DXC) | `MetalShader` | §4.2 |
| `VulkanImGui` | `MetalImGui` | 自前描画を流用(既存の `imgui_impl_aq_dx12`/`_vulkan` と同じ流儀) |

ファイルは `.mm`(Objective-C++)か、`metal-cpp`(Apple 公式 C++ ラッパ)で `.cpp` のまま書くかを選ぶ。**metal-cpp を推奨**(既存スタイルと揃う、`.mm` は `PlatformMac` だけに閉じられる)。

### 4.2 シェーダ経路(HLSL `.fx` → Metal)
| 経路 | 流れ | 長所 | 短所 |
|---|---|---|---|
| **(a) DXC → SPIR-V → SPIRV-Cross → MSL → `metal` CLI → `.metallib`** | 既存の Vulkan 用 DXC 引数を流用 | Vulkan 経路と 1 本化・デバッグ情報が残る・OSS のみ | SPIRV-Cross の MSL 生成で binding 写像(`--msl-*`)を整える必要 |
| (b) DXC → DXIL → Apple **Metal Shader Converter** → `.metallib` | Apple 公式、GS/テッセレーションも変換可 | HLSL の意味を保つ(SV_VertexID 等) | DXIL 経路は Windows で生成が楽、デバッグ情報が落ちる、ライブラリのバンドルが必要 |
| (c) 実行時に (a) を走らせる | ツールチェーン不要 | 開発中の即時反映 | SPIRV-Cross + Metal 実行時コンパイルを同梱、起動が重い |

推奨は **(a) をオフラインで**。`.fx` は無改変のまま、ビルド時に `.metallib` を生成して `Assets/Shader/Metal/` に置く。頂点入力レイアウトは SPIRV-Reflect の結果を SPIR-V 段で取れるので、Vulkan と同じコードで `MTLVertexDescriptor` を組める。

### 4.3 Metal 非対応機能の確認(`.fx` 41 本を走査)
- GS / HS / DS / `SV_ClipDistance` / Wave 命令 / `Texture2DMS`: **未使用**。
- 使用中で要注意: `RWByteAddressBuffer` + `Interlocked*`(クラスタカリング 2 本)→ MSL の `device atomic_uint*`(SPIRV-Cross 対応済)。`Texture2DArray` 深度(シャドウ)→ layered RT。`SampleCmp`(1 本)→ `sample_compare`。`RWTexture2D`(9 本, Hi-Z/Bloom 等)→ `texture2d<float, access::read_write>`。

---

## 5. ビルドシステム

- 現状 `.vcxproj`/`.sln`/NuGet のみ。**プロジェクト自身の CMake は存在しない**(Bullet 同梱の CMakeLists のみ)。Mac 向けには **CMake を新設**(Xcode と Ninja の両方を生成可)。既存 vcxproj は当面残す(Windows 開発の主経路を崩さない)か、CMake から VS 生成に統一する。
- 構成案: `CMakeLists.txt`(root)/ `aqEngine/CMakeLists.txt`(PCH=`aq.h`, C++20, ソース一覧は `Engine.vcxproj` の 188 ClCompile から機械変換)/ `Game/CMakeLists.txt`(`MACOSX_BUNDLE`, Assets を Resources へコピー。ソース一覧は `GameSources.props` から変換)/ `ThirdParty/*`(Bullet は `add_subdirectory`、DirectXTex は非 Windows 経路の cpp を選択)。
- 選択マクロ: `AQ_PLATFORM_WIN32`(新設)/`AQ_PLATFORM_UWP`/`AQ_PLATFORM_MAC`(新設)、`ENGINE_GRAPHICS_VULKAN`(道A)/`ENGINE_GRAPHICS_METAL`(道B)。
- リンク: 現在 `#pragma comment(lib)` 経由の `d3d12/dxgi/dinput8/xinput/xaudio2/mfplat/mfreadwrite/mfuuid/propsys/vulkan-1` を CMake 側の条件付きリンクへ。Mac 側は `Cocoa QuartzCore Metal GameController AVFoundation AudioToolbox` フレームワーク + `libMoltenVK.dylib`(または Vulkan Loader)。
- **Windows 上で先に clang-cl ビルドを通す**と、MSVC 拡張依存(`_s` 系, `#pragma comment`)を Mac に触る前に潰せる。

---

## 6. Windows/MSVC 依存の棚卸し(バックエンドフォルダ外)

母数: ソース 479 ファイル(x64 除く)。バックエンド(D3D11/D3D12/Vulkan)57 ファイルを除く **422 ファイル**が対象。うち何らかの Windows/MSVC 依存を含むのは **58 ファイル**。

| カテゴリ | 規模 | 主な箇所 | 手当 |
|---|---|---|---|
| Windows/D3D/WinRT ヘッダ include | 10 ファイル | `aq.h`, `Utility.h`, `Engine.h`(`windows.h`), `Util/GameTimer.h`, `Platform/*`, `HID/WinRTGamepadBackend.cpp`, `Sound/Decoder/MFDecoder.cpp`, `Game/Application/UWPMain.cpp` | `aq.h`/`Utility.h` の分割で大半が解決。残りはプラットフォーム別ファイルとして `#if` で排他 |
| DirectXMath | 18 ファイル(Math/ 4 + 外 14) | `Math/Vector.h`(77 hits), `Resource.cpp`, `Terrain/*Painter.cpp`, `HeightmapChunk.cpp`, `ParticleComponentSystem.cpp`, `Meshlet.cpp`, `HiZRenderer.cpp` | **置換不要**(DirectXMath は可搬)。`sal.h` 互換ヘッダのみ |
| DirectXTex | 6 ファイル(実使用は 2 点) | `Resource.cpp:1607-1625`, `Terrain/HeightmapChunk.cpp:50-133`(`LoadFromWICFile` 含む) | 非 Windows 経路でビルド + `stb_image` |
| サウンド | 6 ファイル | `Sound/XAudio2/*`(4), `Sound/Decoder/MFDecoder.{h,cpp}` | `ISoundBackend` 実装を追加(エンジン側変更なし) |
| 入力 | 9 ファイル | `HID/Input.{h,cpp}`(DirectInput 直書き・`GetHWND` 使用), `HID/XInputPadBackend.*`, `HID/WinRTGamepadBackend.*`, `HID/PadBackend.h`, `Game/Application/GameInput.cpp` | キーボード/マウス IF 新設 + Cocoa/GameController 実装 |
| スレッド/同期 | **0 ファイル**(Win32 プリミティブなし) | ― | 対応不要 |
| 時間 | 2 ファイル | `Util/GameTimer.{h,cpp}`(QPC, `Sleep(1)`), `Util/Profiler.{h,cpp}`(QPC, `GetCurrentThreadId`) | `std::chrono`/`std::this_thread` |
| ファイル I/O | `fopen_s` 6 ファイル 9 箇所 | `Application.cpp`, `LevelManager.cpp`, `Resource.cpp`, `WavDecoder.cpp`, `WavStreamDecoder.cpp`, `SoundClip.cpp`, `SimpleJson.cpp`, `aq.cpp` | `fopen` へ。バックスラッシュ固定パスは 2 箇所のみ |
| 文字列/CRT `_s` | 約 33 箇所・12 ファイル | `sprintf_s` 12 / `fopen_s` 9 / `strncpy_s` 8 / `wchar_t` 5 / `OutputDebugString` 8 / `ZeroMemory`・`_T(`・`L""` 各 1(`Utility.h`) | `snprintf` 等へ機械置換。`TCHAR`/`LPCWSTR`/`_countof`/`localtime_s` は **0** |
| MSVC 拡張 | ほぼ 0 | `#pragma comment(lib)` 14(aq.h 8 / MFDecoder 4 / XAudio2 1 / XInput 1), `#pragma warning` 13(全て aq.h), `#pragma pack` 4(1 ファイル), SAL 4(`Main.cpp` の WinMain のみ), `WINAPI`/`CALLBACK` 5 | `__declspec`/`__forceinline`/SEH/`__uuidof`/intrinsics は **0** |
| メモリ | 4 ファイル 13 箇所 | `Memory/HeapAllocator.h`, `Memory/StackAllocator.h` 他(`_aligned_malloc`/`_aligned_free`) | `posix_memalign`/`std::aligned_alloc` |
| COM | 4 ファイル 14 箇所 | `Engine.cpp`(`CoInitializeEx`), `MFDecoder.cpp`(`ComPtr`), `XAudio2SoundBackend.cpp`, `PlatformUWP.cpp` | 上記の置換で全消滅 |
| imgui platform | 3 ファイル | `Core/Application.cpp`, `Rendering/ImGuiRenderCommand.cpp`, `Platform/PlatformWin32.cpp`(`ImGui_ImplWin32_*`) | `imgui_impl_osx` へ `#ifdef` 分岐 |
| `AQ_PLATFORM_UWP` ガード | 45 箇所(18 ファイル) | Win32 を `!AQ_PLATFORM_UWP` で表現 | `AQ_PLATFORM_WIN32` 新設・置換 |

---

## 7. フェーズ計画(案)

| Phase | 内容 | 到達点 |
|---|---|---|
| **P0** | CMake 新設(Windows で MSVC/clang-cl 両方が通る)。`aq.h`/`Utility.h` の Windows ブロック分離。`#pragma comment(lib)` 撤去。`AQ_PLATFORM_WIN32` 新設。`GetHWND`→`GetNativeWindow` | Windows で既存構成と同じ動作。clang-cl で MSVC 依存が可視化 |
| **P1** | 非グラフィック層の可搬化: `_s` 系/`OutputDebugString`/QPC/`_aligned_malloc` の置換。DirectInput を `IKeyboardBackend`/`IMouseBackend` に分離。Sound/Input/Video に Null 実装。DirectXTex 非 Windows 経路 + `stb_image` | Windows で回帰なし。Mac で **エンジンが初期化を通る**(描画なし) |
| **P2** | `PlatformMac.mm` + `MacMain.mm`。Bullet/ufbx/imgui を Mac でビルド | Mac でウィンドウ + メインループ |
| **P3(道A)** | Vulkan バックエンドを MoltenVK で起動(§3.2 の 5 点)。`.spv` 同梱経路 | **Mac に実シーンが出る**(海+キャラ+UI) |
| **P4** | 入力(Cocoa/GameController)、サウンド(AVAudioEngine + AudioToolbox)、`imgui_impl_osx` | プレイ可能 |
| **P5(道B, 任意)** | `Graphics/Metal/` 新規(metal-cpp)+ SPIRV-Cross オフライン `.metallib` 経路 + `ENGINE_GRAPHICS_METAL` 分岐(5 ファイル) | ネイティブ Metal でフルパイプライン。MoltenVK と切替比較 |

道A で止めても「Metal で動く Mac 版」は成立する(MoltenVK は Metal 上で動く)。道B は性能・Metal 固有機能・依存削減が必要になった時点で着手すればよい。

---

## 8. オープン課題
1. **DXC を Mac 実行時に持ち込むか**(`libdxcompiler.dylib` バンドル)vs **`.spv`/`.metallib` をオフライン生成**するか。Mac 上で `.fx` を編集して即反映したいなら前者。
2. **PNG/JPG の WIC 依存**: `LoadFromWICFile` の 2 箇所を `stb_image` に差し替える際、sRGB/ミップ生成(`GenerateMipMaps`)の挙動を DirectXTex と揃える。
3. **`Engine::GetHWND()` の呼び出し元**(imgui win32 / DirectInput `SetCooperativeLevel`)を `void*` 化。
4. **サウンドの機能範囲**: XAudio2 実装が使う機能(サブミックス、フィルタ、ピッチ、ストリーミング)のうち AVAudioEngine で等価にできない部分の有無。
5. **imgui のバージョン**: core 1.92 WIP に合う `imgui_impl_osx` を取る(同梱 win32/dx impl と同じ不整合を踏まない)。
6. **KosmicKrisp を使うか**: macOS 26 + Apple Silicon 限定だが Vulkan 1.3 完全準拠。ターゲット OS の下限で判断。
7. **clang-cl を Windows の CI/ローカルに常設するか**: Mac を触らずに可搬性回帰を検出できる。

---

## 参考
- MoltenVK 1.3 リリース(Vulkan 1.3): https://www.phoronix.com/news/MoltenVK-1.3-Released
- MoltenVK 1.4 リリース(Vulkan 1.4): https://www.phoronix.com/news/MoltenVK-1.4
- LunarG「The State of Vulkan on Apple – Jan. 2026」(KosmicKrisp): https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/
- MoltenVK Runtime User Guide(導入・既知の制限): https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- DirectXShaderCompiler(macOS ビルド/`-metal`): https://github.com/microsoft/DirectXShaderCompiler
- DXC macOS prebuilt(コミュニティ): https://github.com/MethanePowered/DirectXShaderCompilerBinary
- Metal Shader Converter(WWDC23「Bring your game to Mac, Part 2」): https://developer.apple.com/videos/play/wwdc2023/10124/
- DirectXTex の非 Windows 対応: https://walbourn.github.io/directxtex-directxmesh-and-uvatlas-now-support-linux/
