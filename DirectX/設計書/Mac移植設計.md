# Mac(Metal)移植 設計

> 対象コミット: 3e74793 / 最終更新: 2026-09-11

## 現在の到達点(main へマージした時点)

**macOS(Apple Silicon)で AquaDash がタイトルからステージクリアまで通しで動く。** 起動から
終了まで Vulkan validation エラー 0 / 終了コード 0。**P0〜P6 まで全フェーズ完了**。
道B(ネイティブ Metal)は [MetalBackend設計.md](MetalBackend設計.md) で P0〜P6 まで実装済みで、
Vulkan 構成と**同じ見た目**(画素比較で平均差 1.8/255)。`.app` 単体配布も両構成で動く。

| | 状態 |
|---|---|
| ビルド | Ninja / Xcode の両ジェネレータで通る。クリーンビルドでエラー 0 / 警告 35 |
| 描画 | ステージまで描画。路面・草・コイン・キャラ・地形・影・UI が出る |
| 入力 | キーボード / マウスは実機確認済み。**パッドは実機未確認**(コントローラが無いため) |
| サウンド | CoreAudio で出力。波形上は鳴っている(左右のピークが連続)。**耳での確認は未実施** |
| ImGui | 描画・入力とも動作(P4b 完了)。自前の `MacImGui` がキー/マウス/ホイール/文字入力/カーソル形状/クリップボードを賄う |

### 引き継ぎで最初に読むべきこと

1. **Windows 側の回帰確認(2026-09-10 実施)**: `DirectX.sln`(MSBuild v145)で D3D11 / D3D12 / Vulkan の
   Debug をビルドし、いずれも 0 エラー・警告 54 件(マージ前と同数。C4099×2 はマージ前から存在)。
   3 構成ともタイトル画面まで起動し、ウィンドウを閉じて終了コード 0 で終わることを確認した。
   - **D3D12 の終了時クラッシュ(例外 0x87D、タイミング依存)を発見・修正**。マージ前のコミット
     (6dd3e7f)でも同じ条件で再現したため**本移植の回帰ではなく既存不具合**。原因は D3D12 の
     `Present` がフェンスを Signal するだけで待たないのに、`Application::Finalize` が
     `D3D12ImGui::Shutdown` で在フライトの GPU 参照先を解放していたこと。Vulkan だけに入れていた
     `WaitDeviceIdle` を、抽象IF `IGraphicsDeviceImpl::WaitIdle()`(既定 no-op、D3D12=`WaitForGPU`、
     Vulkan=`vkDeviceWaitIdle`)に一般化して API を問わず待つようにした(§8-19)。
   - **この PC で検証できなかったもの**: UWP(`DebugXbox`)は VS 18 / VS 2022 のどちらにも
     Windows Store 向け C++ ツールセット(v142/v143/v145)が無く MSB8020 で止まる(コードの問題ではない)。
     Release は `ThirdParty/BulletPhysics/lib/Release` の prebuilt が無く `LNK1181`(gitignore 対象で
     ローカルに Debug しか無い環境依存)。コンパイルは通っている。
   - 未確認: 実操作(キー/パッド)での通しプレイ、`GameTimer` の FPS 制限(ゲーム側で `SetFPSLimit` 未使用)。
2. Mac で見つけた「Mac 固有ではない」不具合(いずれも Windows にも同じものがある):
   - `void*` への `delete` でデストラクタが走らずリソースが漏れていた(リソース 4 型)
   - 終了時に GPU の完了を待たずにリソースを破棄していた(Vulkan で発見。D3D12 でも同根の
     クラッシュがあり Windows 回帰確認で修正 → §8-19)。**同じ穴が実行時のステージ破棄にもあり
     `WaitForRenderIdle` が CPU 側しか待っていなかった** → §8-20
   - 関数ローカル static / グローバルなキャッシュが GPU デバイスより長生きしていた(3 例)
   - **Vulkan バックエンドにインスタンス描画が丸ごと欠けていた**(路面・草・コインが出ない)
   - パス途中で SRV に束縛される RT のレイアウト遷移が抜けていた
   - CMake が `ENGINE_GRAPHICS_Vulkan` を定義しており `ENGINE_GRAPHICS_VULKAN` と一致していなかった
3. 未解決のものは §8 のオープン課題に番号付きで並べてある(特に 15 / 17 / 18)。

---

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
| ~~`ThirdParty/imgui/imgui_impl_osx.{h,mm}`~~ | **同梱しない方針に変更(P4b)**。自前の `aqEngine/Platform/Mac/MacImGui.{h,mm}` を書く。理由と責務は P4b の節を見ること |
| `aqEngine/aq.h` | **ObjC++ TU(`__OBJC__`)では DirectXTex を include しない**(P2 で追加)。非 Windows の DirectXTex は `wsl/winadapter.h` 経由でスタブ `basetsd.h` を読み、そこが `BOOL` を uint32_t に typedef し `interface` を struct に #define するため、Cocoa の `typedef bool BOOL` と衝突して `@interface` が全滅する。`aq.h` は PCH として全 TU に強制インクルードされるので、ここで切る以外に手が無い。§10 の「`.mm` は Platform/Mac・HID/Mac・Sound/CoreAudio に閉じる」の帰結として、`.mm` は画像デコードに触らない |
| `aqEngine/Core/Application.cpp` | `ImGui_ImplWin32_*` を `#if defined(AQ_PLATFORM_WIN32)`、MAC は `aq::platform::MacImGui::Init/NewFrame/Shutdown`(**引数なし**)。描画は既存 `VulkanImGui`(自前)。<br>当初案の「`PlatformMac::GetNSView()` を `ImGui_ImplOSX_Init` へ渡す」は**不要になった**(P4b): `NSView` が要るのはイベント座標の変換だけで、それは `PlatformMac::DispatchInputEvent` が既に view を持ったまま呼ぶため。使われなくなった `GetNSView()` は削除する |
| `aqEngine/Rendering/ImGuiRenderCommand.cpp` | `imgui_impl_dx11.h` include を D3D11 ブロック内へ |

---

## 7. 道B(ネイティブ Metal)の方針 ― 詳細は [MetalBackend設計.md](MetalBackend設計.md)

> **2026-09-11 追記**: P6 に着手し、[MetalBackend設計.md](MetalBackend設計.md) を起こした。
> 事前の実機検証で、**下の当初方針のうち 3 点が成り立たないことが判明している**
> (同書 §0.2)。以降は同書が一次資料。
> - `metal-cpp` で `.cpp` → **Objective-C++(`.mm`)** に変更。出荷実績のあるエンジンが
>   いずれも ObjC++ であることと、既存の Mac コードと作法を揃えるため
> - `xcrun metal` で `.metallib` を事前生成 → **実行時に `newLibraryWithSource:`** に変更。
>   Metal Toolchain がこの環境に無いため(59 本 2 秒で実行時コンパイルできることは実測済み)
> - Vulkan の shift 規約(b:0/t:16/s:32/u:48)をそのまま流す → **Metal 専用シフト
>   (b:0/t:0/s:0/u:16)** に変更。`sampler(32)` が Metal の上限 16 を超えて 25/59 が壊れるため

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
5. ~~**imgui_impl_osx と `PumpEvents` の競合**~~ → **解消(P4b)**。`imgui_impl_osx` を同梱せず、`PlatformMac::DispatchInputEvent` が受けた `NSEvent` を `MacImGui::HandleEvent` へも渡す自前バックエンドにしたため、`NSView` へイベントモニタを張る主体がいなくなった。NSEvent の入り口は`PumpEvents` の 1 本のまま。詳細は P4b の節。
6. **KosmicKrisp**: macOS 26 + Apple Silicon 限定の完全準拠 Vulkan。MoltenVK で portability subset の制限に当たった場合の代替として評価。
7. **clang が出す警告の扱い**(P0 の clang-cl 検証で判明。ビルドは通るので P0 の完了条件からは外した):
   - `-Wdelete-abstract-non-virtual-dtor` 2 件 — `aq::IApplication`(`Engine.cpp:115`)と `app::actor::IState`(`StateMachine.cpp:117`)を、仮想デストラクタ無しの抽象基底ポインタ経由で `delete` している。**派生のデストラクタが走らない未定義動作**なので P1 で潰す
   - `-Wnontrivial-memcall` 6 件 — `MaterialCBData` / `Matrix4x4` への `memcpy`。実体はトリビアルに扱える見込みだが要確認
   - `-Wreorder-ctor` 1 件(`Graphics/Camera.cpp:10`)、`-Winconsistent-missing-override` 5 件、`-Wmicrosoft-exception-spec` 16 件<br>`-Wdelete-abstract-non-virtual-dtor` の 2 件は解消済み(コミット e5eecf2)
8. ~~**`CompressedDecoder.h` の Mac 分岐が未定義**~~ → **解決(P4a)**。P2 で置いた `NullDecoder` を
   `ExtAudioFileDecoder`(AudioToolbox の `ExtAudioFile`)へ差し替えた。
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
14. ~~**`-G Xcode` が未検証**~~ → **解決(P2)**。Xcode 26.6 を導入して確認した。configure・
   ビルド・実行・終了まで Ninja と同じ結果になる。`macos-xcode-debug` /
   `macos-xcode-release` のビルドプリセットを追加した。Xcode 導入直後は
   `sudo xcodebuild -license accept` / `-runFirstLaunch` が必要な点だけ手順に追記済み。
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

21. **`FindProjectRoot` が 3 か所に重複していて `GetContentRoot()` を見ていない**(P5 で判明):
   `VulkanShader.cpp` / `MetalShader.mm` / `MetalRenderContextImpl.mm` が
   「CWD から上方へ `Game/Assets` を探す」独自実装を持っており、
   `Engine::GetContentRoot()` を見ていない。さらに**サウンドとオーディオバンクは
   `"Assets/..."` を CWD 相対のまま `fopen`** する。そのため `.app` 単体起動は
   `MacMain.mm` が CWD を `Contents/Resources/Game` へ移すことで成立させている。
   **本来は `Resource.cpp` の `BuildResourcePathCandidates` と同じく
   `GetContentRoot()` 優先へ統一すべき**。統一すれば CWD への依存が消え、
   Windows / UWP とも作法が揃う。P5 では `aqEngine` を触らない方針で回避した。

17. **`Model.fx` / `SimpleBox.fx` の頂点オフセットがずれている**(P3 で判明。未修正):
   DXC は**未使用の頂点入力を SPIR-V から削る**。この 2 つは NORMAL(location 1)が消えて
   `0:SV_Position 2:TEXCOORD0` になるが、`VulkanShader::BuildInputLayout` は
   「存在する属性を location 順に詰める」ので TEXCOORD0 のオフセットが 12 になる。
   CPU の `VertexData` では uv は 24 なので**法線の位置から UV を読む**。
   影響はこの 2 経路(`NormalModel` = ライト無しの後方互換パスと、デバッグ用の当たり判定ボックス)だけで、
   主経路(`ModelLit` / `GBufferLit` / PBR / Terrain / Skeletal)は属性が連続しているため無事。
   正しく直すには「頂点レイアウトの真実をシェーダのリフレクションではなくエンジン側
   (`VertexData` / `SkinnedVertexData` / UI 頂点のどれか)から与える」必要があり、
   `IASetInputLayout` の引数を変えることになるため別途決める。
18. **Mac は VSync 固定でフレーム時間を比較できない**(P3 で判明): スワップチェーンが
   `VK_PRESENT_MODE_FIFO_KHR` 決め打ちで、60Hz ディスプレイでは 60fps に張り付く。
   Windows(VSync オフ)との比較には present mode の選択(`IMMEDIATE` / `MAILBOX` の
   対応可否を見て選ぶ)か、GPU タイムスタンプによる計測が要る。どちらも Vulkan
   バックデンド共通の話なので Windows 側とまとめて決める。
19. **D3D12 の終了時クラッシュ(Windows 回帰確認で発見・修正済)**: `Application::Finalize` が
   `renderThread_.Finalize()`(CPU 側の完了待ちのみ)の直後に `D3D12ImGui::Shutdown()` で
   VB/IB/フォント/PSO を解放するが、D3D12 の `Present` は `commandQueue_->Signal` するだけで
   GPU 完了を待たない(frames-in-flight)ため、最後のフレームが参照中のリソースを解放して
   `KERNELBASE` で例外 0x87D(終了コード 2173)。15 秒実行で 3/3・25 秒で 1/2 と**タイミング依存**で、
   マージ前(6dd3e7f)でも再現する既存不具合。D3D11 は即時実行モデルのため発生しない。
   修正: `IGraphicsDeviceImpl::WaitIdle()`(既定 no-op)を追加し D3D12=`WaitForGPU()` /
   Vulkan=`WaitDeviceIdle()` で override、`GraphicsDevice::WaitIdle()` 経由で `Application::Finalize`
   が API を問わず呼ぶ(Vulkan 限定の `dynamic_cast` 分岐を撤去)。修正後 D3D12 ×5 / Vulkan ×2 /
   D3D11 ×1 の起動→終了がすべて終了コード 0。
20. **同じ穴が「実行時のリソース破棄」にもあった(19 の続き・修正済)**: 19 は終了処理の話だが、
   ステージ退出のようにゲーム中に GPU リソース所有エンティティを破棄する経路も同じ理由で壊れる。
   `Application::WaitForRenderIdle()` は doc コメントで「GPU アイドル化」と謳いながら実装は
   `renderThread_.WaitForCompletion()` だけで、**レンダースレッドがコマンドを積み終えて `Present` を
   呼んだ(= CPU 側)ことしか保証していなかった**。19 のとおり `Present` は GPU 完了を待たないので、
   ドレイン後も GPU は解放対象の VB/IB を読んでいる最中で、破棄すると device removed / ハング / AV。
   AquaDash の BACK TO TITLE で顕在化した(地形で止まったりプレイヤーで止まったりと**非決定的**なのは
   GPU 実行との競合だから)。修正: `WaitForRenderIdle()` をヘッダ inline から `Application.cpp` へ出し、
   ドレインの後に `GraphicsDevice::WaitIdle()`(19 で足した抽象IF)を呼ぶようにした。
   - **順序は入れ替えられない**。`D3D12GraphicsDeviceImpl::fenceValue_` は非 atomic な `uint64_t` で、
     レンダースレッドの `Present` も `++fenceValue_` する。先にドレインしてレンダースレッドを
     止めてから `WaitForGPU()` を呼ぶことで、同じカウンタと `commandQueue_` の同時アクセスを避けている。
   - 直列モードでは毎フレーム末尾の `FlushRender` が既にドレイン済みなので `WaitForCompletion` は
     即返り(実測 0.0ms)、防御は `WaitIdle`(実測 8.0ms)だけが担う。非同期(`AQ_RENDER_PIPELINED`)では
     `FlushRender` = `WaitForPipelinedFrame` が前フレームしか待たないためドレイン側も実際にブロックする。
     **両モードで BACK TO TITLE → タイトル → 再入場を実機確認済み**。
   - D3D11 は実行時破棄でも `WaitIdle` が no-op でよい(ランタイムがリソース参照を追跡して遅延解放する)。

21. **終了時に `MemoryTracker` が約 1.28MB / 10,467 件のリークを報告する**(P4b の評価中に判明。
   **本移植の回帰ではない**): Mac の Debug ビルドで AquaDash を起動してタイトルのまま
   ウィンドウを閉じると、終了時の `MemoryTracker` ダンプが 10,467 件・1,279,246 バイトを並べる。
   P4b の変更を `git stash` して同条件で測った値との差は **+12 件 / −240 バイト**で誤差の範囲、
   つまり**以前から出ていたもの**。ダンプの全件が「no source info」で、どこの確保かが分からない
   (`engineNewWith` を通っていない `new` / ThirdParty 側の確保と思われる)。
   終了コードは 0、Vulkan validation エラーも 0 なので実害は出ていないが、
   **これが正常なのか(意図的に解放しない静的データなのか)を誰も確認していない**。
   Windows でも同じ数字が出るのかを含めて別途見ること。
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
- [x] Mac(Apple Silicon)で `cmake -G Xcode` / `-G Ninja` の**両方**からビルド・リンクが通る(macOS 26.6.2 / M 系 / AppleClang 21.0.0)
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
      - **`-G Xcode` も確認済み**(Xcode 26.6)。`macos-xcode` プリセットで configure →
        `AquaDash.xcodeproj` 生成 → `** BUILD SUCCEEDED **` → 生成された `Game.app` の実行・
        終了まで、Ninja と同じ結果(validation エラー 0 / 終了コード 0)。
        ビルドプリセット `macos-xcode-debug` / `macos-xcode-release` が
        `CMakePresets.json` に無かったので追加した(configure プリセットだけあって
        `cmake --build --preset` が使えない状態だった)。
        なお **Xcode を入れた直後はライセンス未同意でコンパイラが見つからない**
        (`No CMAKE_C_COMPILER could be found`)。`sudo xcodebuild -license accept` と
        `sudo xcodebuild -runFirstLaunch` を先に通す必要がある
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

### P2.5: Mac の入力(Mac 実機) ― P3 より先に実施

**P4 から前倒しした。** P3 の評価は「タイトル〜**ステージ**まで Windows と同じ見た目」だが、
Mac の入力は P2 時点で Null(`KeyboardMouseBackend.h` / `PadBackend.h` とも Mac は Null 実装)で、
**タイトル画面から先へ進めないため P3 を評価できない**。入力だけを P4 から切り出して先に置く。
サウンド(`CoreAudioSoundBackend`)と `imgui_impl_osx` は P4 に残す。

実装:
- §3.2 の `CocoaInputSink` / `CocoaKeyboardBackend` / `CocoaMouseBackend`。
- §3.2 の `GameControllerPadBackend`。
- `PlatformMac::PumpEvents` からシンクへの `NSEvent` 転送(現在の `TODO(P4)` を潰す)。
- `KeyboardMouseBackend.h` / `PadBackend.h` の MAC 分岐を Null から実装へ差し替え。

決定事項(P2.5 で決めたもの):
- **シンクへの到達手段**: `CocoaInputSink` は `.mm` 内のファイルスコープシングルトン。
  `PlatformMac`(転送側)と `Cocoa*Backend`(取得側)はどちらも Mac 専用なので、
  `IPlatform` にも `IKeyboardBackend` にも露出させない(§3.2 の方針どおり)。
  GPU リソースを持たないため、シングルトンの寿命は問題にならない。
- **キーコード**: Carbon 仮想キーコード(`kVK_*`)の数値を自前の名前付き定数として持ち、
  `Carbon.framework` には依存しない。値は macOS の ANSI 配列で固定されている。
- **座標系**: §8-13 の決定で `contentsScale = 1` にしてあるため、
  **NSView のポイント座標 = 描画ピクセル座標が 1:1** になる。Y は Cocoa が左下原点なので
  `MouseState::cursorX/Y`(左上原点)へ反転するだけでよく、倍率補正は要らない。
- **`imgui_impl_osx` との競合**(§8-5): P2.5 では `imgui_impl_osx` を入れないため競合しない。
  `PumpEvents` は「シンクへ転送 → `[NSApp sendEvent:]`」の順とし、P4 で imgui を足すときに
  この順序のまま二重処理にならないかを再確認する。
- **振動は P2.5 では実装しない**: `GameControllerPadBackend::SetVibration` は no-op に留める。
  `GCDeviceHaptics` + `CHHapticEngine` はエンジンの生成/停止の寿命管理が要り、
  `CoreHaptics.framework` のリンク追加も伴う。P2.5 の目的(ステージまで進める)に不要なため
  **P4 へ回す**。アダプティブトリガー(`SetTriggerResistance`)は
  `GCDualSenseAdaptiveTrigger` で素直に書けるので P2.5 で入れる。

実装中に決めた追加事項:
- **押下のラッチ**: `CocoaInputSink` は「前回の取得以降に一度でも押されたキー/ボタン」も
  押下として返す。DirectInput はデバイスの**現在状態**をサンプリングするが Cocoa は離散
  イベントで届くため、1 フレームが長引くと(ロード中のヒッチ等)そのフレーム内で押して
  離すところまで進み、レベルだけ見ていると取りこぼす。次の取得では実レベルへ戻るので
  トリガー判定がちょうど 1 回成立する。
- **`.mm` にしないもの**: `CocoaInputSink` / `CocoaKeyboardBackend` / `CocoaMouseBackend` は
  Objective-C を一切使わないため `.cpp`(§3.2 は `.mm` としていたが実態に合わせる)。
  Cocoa の型を触るのは `PlatformMac.mm` と `GameControllerPadBackend.mm` だけ。

評価:
- [x] キーボードで AquaDash のタイトル → ステージまで操作できる
      - Space(仮想キーコード 49)でタイトルが進み、ステージ用アセット
        (`Terrain/grass.DDS` / `rock.DDS` / `snow.DDS` / `utc_all2.dds` / `utc_nomal.dds`)の
        読み込みまで到達することを確認
      - **前提として 2 つ潰した**:
        1. `.app` の `CFBundleIdentifier` が**空文字列**だった(CMake の既定 Info.plist)。
           識別子が空のバンドルは macOS から通常のアプリとして扱われず、
           ウィンドウがキーウィンドウにならないためキーボードイベントがキューに届かない。
           `Game/CMakeLists.txt` で `MACOSX_BUNDLE_GUI_IDENTIFIER` 等を設定した
        2. `NSApp run` を使わず自前ループを回しているため、`makeKeyAndOrderFront:` だけでは
           アプリがアクティブにならない。`[NSApp activate]`(macOS 14 未満は
           `activateIgnoringOtherApps:`)を追加した。`MacMain.mm` の `TODO(Mac実機)` が
           想定していたとおりの症状
- [x] トリガー(押した瞬間)判定が効く
      - Space の 1 回押下でちょうど 1 回だけ画面が進む(2 回押して 2 段進む)ことを確認
      - **長押しの挙動は未検証**。現在の AquaDash に「押しっぱなしで繰り返す」操作が
        見当たらず、観測できる差が作れなかった。P4 の通しプレイで見る
- [x] マウスのカーソル位置が正しい
      - ウィンドウ位置 (95, 58) / サイズ 1280x752(コンテンツ 720 + タイトルバー 32)の状態で、
        画面座標 (WX+100, WY+100) へワープ → クライアント (100.0, **68.0**) を取得。
        タイトルバー分を引いた期待値と一致し、(+200, +100) 移動も 1:1 で反映された
        (§8-13 で `contentsScale = 1` にしたため Retina でも倍率補正が要らないことの裏取り)
      - 左クリックが `buttons[0] = 0x80` として取れることも確認
      - **UI のヒットテストとの一致は未検証**。AquaDash の画面 JSON に `button` コンポーネントが
        無く(使っているのは `image` / `text` / `nineSlice` / `circleGauge`)、
        マウスで押せる UI が存在しないため。P4 で ImGui のパネルを足したときに見る
      - **相対移動量(dx/dy)も未検証**。合成イベント(`CGWarpMouseCursorPosition`)では
        delta が常に 0 になるため、実際に手で動かさないと確認できない
- [ ] パッド(接続時)でスティック・ボタンが効く
      - **実機コントローラが無く未検証**。未接続時に `connected=false` で落ちないことは、
        今回の全実行(パッド無し)で確認済み。振動は P4
- [x] Windows 側の回帰なし(`AQ_PLATFORM_WIN32` 側のプリプロセス結果が変わらない)
      - 共有ファイルの差分は `KeyboardMouseBackend.h` / `PadBackend.h` の
        `#elif defined(AQ_PLATFORM_MAC)` ブロック内と、`Game/CMakeLists.txt` の
        `if(APPLE)` ブロックのみ。新規ファイルは全て `HID/Mac/` で、CMake が非 Mac では除外する

### P3: Mac で実シーン(Mac 実機)

> P2.5 でステージまで到達できるようになって見えた 2 件は、**どちらも解決済み**(下記の評価欄)。

実装:
- `.spv` をビルド時生成(§4)。アセットの `GetContentRoot` 経由読み込み。
- Windows 版と同じ AquaDash を起動。

評価:
- [ ] タイトル〜ステージまで Windows Vulkan 構成と同じ見た目(海・キャラ・影・Bloom・UI・デカール・草)
      - **目視確認待ち**。この環境では画面収録の権限が無く `screencapture` が使えないため、
        スクリーンショットでの比較ができない
      - 目視で「路面などが表示されない」と報告があり、**Vulkan バックエンドにインスタンス描画が
        丸ごと欠けていた**ことが判明。修正して路面リボン・エッジライン・センター破線・草・
        コインが正しく描かれることをスクリーンショットで確認した(VK エラー 0)
      - **なお画面収録の権限が付いたので、以降は `screencapture -R <窓の矩形>` で
        ウィンドウだけを撮って自分で見た目を確認できる**。窓の位置とサイズは
        `osascript` の System Events(`position of window 1` / `size of window 1`)で取れる
- [x] validation layer エラー 0(タイトル〜ステージを通して)
      - ステージに入って初めて出ていた **2 件を潰した**:
        1. **レンダーターゲットが `COLOR_ATTACHMENT` のままサンプルされていた**。
           `BarrierBeforePass` は dynamic rendering の scope に入る**前**にしか遷移を打てない
           (scope 内では打てないので、これ自体は正しい)。ところが UI はパスの**途中**で
           ミニマップのベイク結果(`OffscreenScenePass` の 512x512 RT)を SRV に束縛して描くため、
           バリアを打つ機会が無かった。**生産側(パス終了時)でカラー RT を
           `SHADER_READ_ONLY_OPTIMAL` へ戻す**ように `EndRenderingIfActive` を直した。
           次にその RT へ描くときは `BarrierBeforePass` が `COLOR_ATTACHMENT` へ戻すので往復は成立する
        2. **終了時の VMA アサート**: `InstancedStaticMesh` の名前レジストリ `g_named` が
           ファイルスコープのグローバルで、頂点/インデックスバッファを抱えたまま
           `vkDestroyDevice` より後まで生き残っていた。`ClearNamed()` を新設して
           `Application::Finalize` から呼ぶ(`e8710e4` で潰した `FontAssetCache` /
           `GpuClusterCuller` と同じ形)
      - **どちらも Mac 固有ではない**。Vulkan バックエンド共通の問題で、Windows Vulkan 構成にも同じものがある
- [ ] フレーム時間を Windows(同等 GPU クラス)と比較して記録
      - **比較不能のまま**。Mac のスワップチェーンは `VK_PRESENT_MODE_FIFO_KHR` 固定(VSync)で、
        タイトルもステージも **60.0 fps / 16.67 ms に張り付く**。Windows 側は VSync オフで
        900 fps 級(§9 P0 の記録)なので、同じ土俵に乗せるには present mode を選べるようにするか、
        GPU タイムスタンプで測る必要がある → §8-17
- [x] 未対応機能のゲートで描画が破綻しない
      - そもそもゲートに掛かる機能が無かった。portability subset が非対応と報告したのは
        `pointPolygons` / `tessellationIsolines` / `tessellationPointMode` の 3 件のみで、
        いずれも本エンジンは未使用。compute は対応しているため `IsComputeSupported()` は true で通る
- [x] 全 `.fx` × エントリの `.spv` がビルド時に生成される(`shader_entries.txt` に漏れなし)
      - `.fx` から拾ったエントリらしき関数 59 件がすべて `shader_entries.txt` にあり、
        `Game/Assets/Shader/spv/` に 59 本生成されている(過不足 0)

### P4a: Mac のサウンド(Mac 実機)

**入力は P2.5 で済ませたので、旧 P4 の残りをサウンドと ImGui に分けた**(互いに独立していて、
問題が出たときの切り分けが楽なため)。こちらはサウンドだけ。

実装:
- §5 の `CoreAudioSoundBackend`/`CoreAudioSoundVoice`/`ExtAudioFileDecoder` を `SoftwareMixer` に接続。
- `SoundBackend.h` の MAC 分岐を `SOUND_BACKEND_NULL` → `SOUND_BACKEND_COREAUDIO` へ。
- `CompressedDecoder.h` の MAC 分岐を `NullDecoder` → `ExtAudioFileDecoder` へ(§8-8 の解消)。

決定事項:
- **出力フォーマットは 48kHz / float32 / 2ch のインターリーブ**で AudioUnit を開く。
  `SoftwareMixer::Render(float* out, frames)` の出力をコールバックのバッファへ直接書けるため、
  レンダーコールバック内でのコピーも確保も発生しない(§5 が要求する実時間契約)。
- `SoundEngine` / `Mixer3D` / `SoundStream` / `AudioDirector` は無変更。

評価:
- [x] AudioUnit が開いてレンダーコールバックが回り、`GetOutputClock` の `outputFrames` が進む
      - 起動 149.9ms で `[sound] CoreAudio 出力を開始 (48kHz / float32 / 2ch)`。
        `outputFrames` は 300 ゲームフレーム(= 5 秒)ごとにちょうど約 240,000 進み、
        **実時間と一致**する(48000 × 5)
- [x] **出力が無音でない**ことを波形で確認
      - レンダーコールバックの出力ピークを 1 秒ごとに測ると、左右で異なる値
        (L 0.65〜0.83 / R 0.66〜0.84)が連続して出る。クリップ(1.0 到達)は無し。
        デコーダ → `SoftwareMixer` → CoreAudio → デバイスが通っている証拠
- [x] `latencySeconds` が実測値になる(**12.9ms**)
      - `kAudioUnitProperty_Latency` は AudioUnit 自身の遅延しか返さず DefaultOutput では 0。
        A/V 同期(`SoundStream`)が欲しいのは「書いた PCM が鳴るまで」なので、HAL 側の
        デバイス遅延 + セーフティオフセット + バッファ長を足して求めるようにした
- [ ] アンダーランが発生しない
      - **起動直後に 2 回**発生し、その後は増えない(以降ずっと 2 のまま)。
        BGM ストリームの投入がデバイス開始に追いつくまでの立ち上がりと見られる。
        定常状態では 0 なので実害は無いが、Windows 側と比較していないため未確定
- [ ] BGM(wav/mp3)・SE・3D 音源が**実際に聞こえる**。ピッチ/パン/バス音量が反映される
      - **耳での確認待ち**(波形上は鳴っている)
- [ ] 停止・一時停止・自然終了の回収が動く
- [ ] `SoundStream` の A/V 同期指標が Windows と同等の範囲
- [x] Windows 側の回帰なし(`AQ_PLATFORM_WIN32` 側のプリプロセス結果が変わらない)
      - 共有ファイルの差分は `SoundBackend.h` / `CompressedDecoder.h` の Mac 分岐と
        `aqEngine/CMakeLists.txt` の `elseif(APPLE)` 内(`-framework CoreAudio` 追加)のみ。
        新規ファイルは `Sound/CoreAudio/` と `Sound/Decoder/ExtAudioFileDecoder.*` で、
        CMake が非 Mac では除外する

### P4b: Mac のデバッグ UI(Mac 実機)

**方式変更(P4b 着手時に決定)**: 当初は `imgui_impl_osx.{h,mm}` を同梱する予定だったが、
**自前の ImGui プラットフォームバックエンド `MacImGui` を書く**方式に変えた。

- P2.5 で `PumpEvents` → `DispatchInputEvent` → `CocoaInputSink` という NSEvent の一次受けが
  既にできている。`imgui_impl_osx` は `NSView` に `addLocalMonitorForEventsMatchingMask` で
  イベントモニタを張るため、同じ NSEvent を 2 系統から触ることになる(§8-5 の懸念そのもの)。
  **入り口を 1 本に保つほうが素直**で、順序を詰める必要も消える。
- `imgui_impl_osx` は IME(`NSTextInputClient` のサブビュー)まで面倒を見るが、Mac の
  フォントアトラスは ASCII + 矢印 + 幾何学模様のみ(§6)なので **IME は使えず要らない**。
  残る責務(キー写像・マウス・ホイール・カーソル形状・クリップボード)は自前で書ける量。
- 同梱すると imgui 1.92 WIP という中間版に対応した backend を持ち込むことになり、
  バージョン整合の確認コストが乗る。

責務:

| ファイル | 責務 |
|---|---|
| `aqEngine/Platform/Mac/MacImGui.{h,mm}` **(新規)** | ImGui の Mac プラットフォームバックエンド(`imgui_impl_win32` 相当)。`Init()` / `Shutdown()` / `NewFrame()` / `HandleEvent(NSEvent*, NSView*)`。ヘッダは C++ からも読めるよう、`HandleEvent` だけ `#ifdef __OBJC__` で囲む(`Application.cpp` は素の C++) |
| `aqEngine/Platform/Mac/PlatformMac.mm` | `DispatchInputEvent` の末尾で `MacImGui::HandleEvent(event, view)` を呼ぶ。`windowDidResignKey` / `windowDidBecomeKey`(新設)から `MacImGui::OnFocusChanged`。使われなくなった `GetNSView()` を削除 |
| `aqEngine/Platform/Mac/PlatformMac.h` | `GetNSView()` の宣言を削除 |
| `aqEngine/Core/Application.cpp` | Mac 分岐の `DisplaySize`/`DeltaTime` 直書きを `MacImGui::NewFrame()` へ、`winOk = true` を `MacImGui::Init()` へ、終了処理へ `MacImGui::Shutdown()` を追加。フォントの `TODO(P4)` を解消 |

決定事項:

- **ゲーム入力との排他は既存の仕組みのまま**。`Application::Update` の先頭で
  `io.WantCaptureKeyboard/Mouse` を `InputManager::Suppress*` へ渡す経路が既にあり、Win32 と同じ
  (Win32 も `ImGui_ImplWin32_WndProcHandler` と DirectInput の両方に入力が入り、抑制はこの一点で効く)。
  したがって `HandleEvent` と `CocoaInputSink` の**両方へ同じイベントを渡してよい**。
  「二重処理」とはイベントが 2 回 ImGui に入ることであって、ゲームと ImGui の両方が見ること
  ではない。
- **呼ぶ順序**は「シンクへ転送 → `MacImGui::HandleEvent` → `[NSApp sendEvent:]`」。
  前 2 つはどちらも状態を溜めるだけなので順序に意味は無く、既存行を動かさないため後ろに足す。
- **座標系**: §8-13 の決定で `contentsScale = 1` に固定してあるため、NSView のポイント座標が
  そのまま `io.DisplaySize`(= `Engine::GetScreenWidth/Height`)の座標系になる。
  `DisplayFramebufferScale` は既定の (1,1) のまま。Y は Cocoa が左下原点なので反転する
  (`CocoaInputSink` への `PushMousePosition` と同じ変換)。
- **キー対応表は `CocoaInputSink` の `KEY_MAP` と共有しない**。あちらは `KeyBoardType`
  (ゲームが見る 16 キー)への写像で、ImGui は英数・記号・F1〜F12・テンキー・編集キーまで要る。
  写像先の enum が違うので、`MacImGui.mm` に `kVK_* → ImGuiKey` の表を別に持つ。
  `kVK_*` の数値を自前定数として写す方針は `CocoaInputSink` と揃える(`Carbon.framework` に依存しない)。
- **文字入力**は `[event characters]` を UTF-8 にして `io.AddInputCharactersUTF8`。制御文字は落とす。
  IME(`markedText`)は扱わない。
- **Command 押下中は keyUp が配送されない** macOS の仕様に、`CocoaInputSink::OnModifierFlagsChanged`
  と同じ対処(Command が離れた時点で押下を一掃)を入れる。
- **修飾キー**は `flagsChanged` の `modifierFlags` から `ImGuiMod_Ctrl/Shift/Alt/Super` を毎回入れ直す。
- **ホイール**は `scrollingDeltaX/Y`。`hasPreciseScrollingDeltas`(トラックパッド)は
  ピクセル量で来るので 1/10 に、行単位のときはそのまま `AddMouseWheelEvent` へ渡す。
- **カーソル形状**(`ImGui::GetMouseCursor()` → `NSCursor`)と**クリップボード**
  (`NSPasteboard`)も入れる。どちらも数十行で、デバッグ UI の使い勝手に直結する。
  クリップボードの口は 1.91.1 で `ImGuiIO` から **`ImGuiPlatformIO::Platform_*ClipboardTextFn`**
  へ移っている(同梱の 1.92.0 WIP では `io` 側は旧 API 互換として残っているだけ)。
- **フォーカスの出入りは両方通知する**(実装中に判明して追加): `MacImGui::OnFocusChanged(bool)` を
  `NSWindowDelegate` の `windowDidResignKey` / `windowDidBecomeKey` から呼ぶ。
  ImGui の `io.AppFocusLost` は**立ちっぱなしのフラグ**で、真の間は毎フレーム
  `ClearInputKeys` / `ClearInputMouse` が走る(`imgui.cpp` の `UpdateInputEvents` 末尾)。
  **`AddFocusEvent(false)` だけを送ると、以後 ImGui の入力が永久に捨てられる**。
  既存の `windowDidResignKey` は `CocoaInputSink::OnFocusLost()` しか呼んでいなかったので、
  `windowDidBecomeKey` ごと足す。
- **フォント**: `Application.cpp` の `TODO(P4)` を解消し、Mac でも
  `/System/Library/Fonts/SFNS.ttf` →(無ければ)`Helvetica.ttc` を 15px で読む。
  グリフ範囲は Windows 側と同じ表を共用する。読めなければ従来どおり `AddFontDefault()`。

実装:
- 上表の 4 ファイル。`imgui_impl_osx` は同梱しない(§8-5 は本方式で解消)。
- 新規ファイルは `aqEngine/CMakeLists.txt` の glob(`*.mm` / `Platform/Mac/`)が自動で拾う。
  **Windows の `.vcxproj` には登録しない**(Mac 専用ファイルは `PlatformMac` / `CocoaInputSink` 等と
  同じく CMake 側だけで扱う既存方針)。

評価(2026-09-11 実機。`macos-ninja` Debug / 合成入力 CGEvent + スクリーンショットで確認):
- [x] ImGui のデバッグ UI が表示・操作でき、`SuppressKeyboard/Mouse` がゲーム入力と排他になる
      - メニューバー(Tools → Audio / Prefab Editor / Level Editor)が開く。Scene Hierarchy の
        エンティティをクリックで選択でき Inspector に反映される
      - **排他の確認**: Inspector の Name 欄を編集中に Space を押すと文字入力側に入り、
        タイトル画面は進まない。欄からフォーカスを外すと同じ Space でゲームが開始する
- [x] 入力が二重に処理されない(1 回のクリックで ImGui とゲームの両方が反応しない)
      - 上記 Space の挙動がそのまま証拠。`imgui_impl_osx` を入れていないので NSEvent の
        入り口は `PumpEvents` 1 本のまま
- [x] F1 / 中クリックでデバッグ UI がトグルできる(両方とも動作)
- [x] ImGui のテキスト入力欄に英数字が打てる(Name 欄が `Session` → `Sessionxyz` になる)
- [x] カーソル形状がウィジェットに応じて変わる(テキスト欄で I ビーム / ゲーム上で矢印)
- [x] 別アプリへ切り替えて戻っても ImGui の入力が効き続ける(`AppFocusLost` が残らない)
      - Finder へ切り替えて復帰後も F1 トグルが効く
- [x] ホイールでパネルがスクロールする
- [x] キーボード/マウスで AquaDash が**通してプレイ**できる
      - タイトル → ステージ → STAGE CLEAR(COIN 157 / TIME 01:30)まで到達。終了コード 0、
        Vulkan validation **エラー 0**(警告は §8-15 の既知のもののみ)
      - **パッドは実機が無いため未確認のまま**(P2.5 から継続。本フェーズでも解消しない)
      - 音は鳴っている前提(P4a)。**耳での確認は引き続き未実施**
- [ ] Windows 側の回帰なし(D3D12/Vulkan/UWP がビルド・動作)
      - **この Mac では検証できない。次に Windows を触るときの宿題**
      - 共有ファイルの差分は `Core/Application.cpp` のみで、Mac 経路は
        `#elif defined(AQ_PLATFORM_MAC)` に閉じている。**UWP が拾っていた `#else` は
        そのまま残した**(`DisplaySize`/`DeltaTime` の手当てが UWP には引き続き要る)。
        フォント読み込みはグリフ範囲表とロード手順を Win/Mac で共用する形に整理したので、
        **Windows のフォント探索の挙動は変わらないはずだが要確認**
      - 新規ファイルは `Platform/Mac/` 配下のみで CMake が非 Mac では除外する
      - (2026-09-10: D3D11/D3D12/Vulkan はビルド・起動・終了まで確認済(冒頭「引き継ぎ」1)。
        UWP は Windows Store ツールセットが無い PC のため未確認)

### P5: 配布形態

`.app` バンドルに Assets と(Vulkan 構成なら)ランタイムを同梱する。コード署名は範囲外。

実装は `Tools/PackageApp/package_app.cmake` + `Game/CMakeLists.txt` の
**`aqBundleApp` ターゲット**。`Assets` が 92MB あるので **ALL には入れない**
(毎ビルドでコピーするとイテレーションが遅くなる)。`aqCompileMsl` / `aqCompileSpv` へ
推移的に依存するので、シェーダ生成物は必ず生成後に同梱される。手順は
`Tools/SetupCMake/README.md` §5.2.2。

評価(2026-09-11):
- [x] **ソースツリー外にコピーした `.app` 単体で起動する**。`/tmp` へコピーし、
      `VULKAN_SDK` / `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` / `DYLD_LIBRARY_PATH` を
      **すべて外した状態**で Metal 構成・Vulkan 構成の両方がタイトル画面まで起動し、
      終了コード 0。アセット読み込みの失敗 0 件
- [x] **Finder からの起動(CWD = `/`)でも動く**
- [x] Vulkan 構成の `otool -L` は `@rpath/libvulkan.1.dylib` のみ、`LC_RPATH` は
      `@executable_path/../Frameworks` のみ(SDK の rpath は `install_name_tool` で剥がす。
      **剥がさないと「同梱 dylib ではなく開発機の SDK を読んでいるだけ」を見抜けない**)

実装で分かったこと(**設計の記述が 3 点足りなかった**):

1. **同梱先は `Contents/Resources` 直下ではなく `Contents/Resources/Game/Assets`**。
   `Resource.cpp` の `BuildResourcePathCandidates` が `"Assets/..."` を
   `<root>/Game/Assets/...` へ組み立てるため(UWP の appx も同じ理由で
   `install/Game/Assets/...` に置いている)。「Assets を `Resources` へ」だと 1 段足りない。
2. **Assets を同梱するだけでは §8-16 は解消しない。CWD の移動が要る。**
   `GetContentRoot()` を見ていない経路が 2 種類ある:
   - シェーダのパス解決(`VulkanShader.cpp` / `MetalShader.mm` /
     `MetalRenderContextImpl.mm` の `FindProjectRoot`。CWD から上方へ `Game/Assets` を探す)
   - **サウンドとオーディオバンク**(`"Assets/..."` を CWD 相対のまま `fopen` する)
   そこで `MacMain.mm` が **CWD を `Contents/Resources/Game` へ移す**。
   **`Resources` へ移すと前者しか満たせず、BGM とオーディオバンクが読めなくなる**(実機で踏んだ)。
   `Resources/Game` なら両方成り立ち、開発時の `cd Game && ...` と同じ形になる。
   → **本来の直し方は `aqEngine` 側で 3 つの `FindProjectRoot` を `GetContentRoot()` 優先へ
   統一すること**(`Resource.cpp` と同じ形)。P5 では `aqEngine` を触らず CWD で回避した。§8-21 へ。
3. **Vulkan は dylib 2 本 + rpath だけでは足りない。** ICD 定義 JSON の同梱と
   `library_path` のバンドル内相対への書き換え、`VK_DRIVER_FILES` / `VK_ICD_FILENAMES` の
   実行時設定(`.app` 起動時かつ未設定のときだけ)が要る。SDK の
   `libvulkan.dylib` / `libvulkan.1.dylib` は**シンボリックリンク**なので実体をコピーすること
   (`file(COPY)` だとリンクのまま複製されて配布先で切れる。`file(COPY_FILE)` を使う)。

### P6: ネイティブ Metal(道B、別設計書)

- `MetalBackend設計.md` を起こしてから着手。§7 の方針を継承。

---

## 10. チェックポイント(設計全体)

- [ ] 抽象の直交性: `AQ_PLATFORM_*` と `ENGINE_GRAPHICS_*` が独立し、Mac × Vulkan / Win32 × Vulkan / Mac × Metal(将来)が成立する
- [ ] API/OS 固有型の漏れ: `HWND`/`NSView`/`CAMetalLayer`/`LPDIRECTINPUT*`/`IMF*` が `Platform/<OS>/`・`HID/<OS>/`・`Sound/<Backend>/`・`Graphics/<API>/` の外に現れない
- [ ] `.mm` が `Platform/Mac/`・`HID/Mac/`・`Sound/CoreAudio/`・`Sound/Decoder/ExtAudioFileDecoder.mm`・`MacMain.mm` に閉じている(P4b で `Platform/Mac/MacImGui.mm` が加わった。**P6 で `Graphics/Metal/` を追加する** → [MetalBackend設計.md](MetalBackend設計.md) §10)
- [ ] 各 Bridge(入力/サウンド/画像)に Null 実装があり、未対応プラットフォームでも落ちずに起動する
- [ ] 抽象IF(`IGraphicsDeviceImpl`/`IRenderContextImpl`/`IPlatform`/`ISoundBackend`/`IPadBackend`)への追加が 0 本(`IPlatform` は追加なし。`IKeyboardBackend`/`IMouseBackend` は新設)
- [ ] `.fx` が無改変
- [ ] 設計と実装の食い違いは本書へ反映し、`対象コミット` を更新した
