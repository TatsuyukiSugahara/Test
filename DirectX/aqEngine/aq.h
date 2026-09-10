#pragma once
#include "Platform/Common/PlatformDefs.h"

#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
#pragma warning (disable  : 4201)
#endif

// Graphics API selection.
// Define one ENGINE_GRAPHICS_* macro in project settings to override the default.
//#define ENGINE_GRAPHICS_D3D11
//#define ENGINE_GRAPHICS_D3D12
//#define ENGINE_GRAPHICS_VULKAN

#if !defined(ENGINE_GRAPHICS_D3D11) && !defined(ENGINE_GRAPHICS_D3D12) && !defined(ENGINE_GRAPHICS_VULKAN)
// 既定は D3D12。Xbox の UWP(道A)も、Dev Home の「ゲーム」種別なら実 GPU で D3D12 が
// FL12_0 まで通る(「アプリ」種別は D3D12 が WARP のみで非実用)。
// 「アプリ」種別/FL10 機向けの D3D11 + FL10 フォールバックは、ENGINE_GRAPHICS_D3D11 を
// 明示定義すれば選べる(IsComputeSupported 経由で Bloom/海/コンピュート等を落とす)。
#define ENGINE_GRAPHICS_D3D12
#endif

#if (defined(ENGINE_GRAPHICS_D3D11) + defined(ENGINE_GRAPHICS_D3D12) + defined(ENGINE_GRAPHICS_VULKAN)) > 1
#error "Define exactly one ENGINE_GRAPHICS_* backend"
#endif

// レンダリング同期モードの切り替え (AQ_RENDER_PIPELINED) はここではなく
// RenderConfig.h で行う（単一ソース）。include して全 TU から見えるようにする。
#include "RenderConfig.h"


// ここから windows.h 系(Win32 / UWP)専用ブロック。
// リンクするライブラリは Game/GraphicsApi.props と各 vcxproj の
// AdditionalDependencies で指定する(#pragma comment(lib) は使わない)。
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)

#define NOMINMAX
#include <windows.h>
#include <tchar.h>

#ifdef ENGINE_GRAPHICS_D3D11
#pragma warning(push)
#pragma warning(disable:4005)
#include <d3d11.h>
#pragma warning(pop)
#include <d3dcompiler.h>
#endif // ENGINE_GRAPHICS_D3D11

#ifdef ENGINE_GRAPHICS_D3D12
#pragma warning(push)
#pragma warning(disable:4005)
#include <d3d12.h>
#include <dxgi1_6.h>
#pragma warning(pop)
#include <d3dcompiler.h>
#endif // ENGINE_GRAPHICS_D3D12

#ifdef ENGINE_GRAPHICS_VULKAN
// Vulkan ヘッダ本体は Graphics/Vulkan/VulkanCommon.h 側で取り込む (VK_USE_PLATFORM_WIN32_KHR 定義込み)。
// vulkan-1.lib は Game/GraphicsApi.props の AqGraphicsApi=Vulkan 分岐でリンクする
// (ライブラリパスは Game vcxproj の $(VULKAN_SDK)\Lib)。
#endif // ENGINE_GRAPHICS_VULKAN


//DirectInput
#define	DIRECTINPUT_VERSION	0x0800
#include <dinput.h>


#endif // AQ_PLATFORM_WINDOWS_FAMILY


// DirectXTex: 画像ローダ。Windows / Mac の両方で使う(Mac は非 Windows 経路 =
// DDS/TGA/HDR + BC ソフトコーデック。PNG/JPG は Resource/ImageLoader が stb_image へ回す)。
//  - デスクトップ: ThirdParty/DirectXTex のソースを aqEngine に同梱ビルドする
//    (Engine.vcxproj の Debug/Release 構成でコンパイル)。各構成の CRT に自動一致するため
//    prebuilt lib は不要・pragma comment(lib) も不要(シンボルは aqEngine.lib に含まれる)。
//  - UWP(Xbox): /MD 必須のため NuGet パッケージ "directxtex_uwp" を使う。
//    lib は NuGet の .targets が自動リンク。ヘッダは <DirectXTex.h>。
//  - Mac: 同梱ソース。sal.h / dxgiformat.h 等は ThirdParty/DirectX-Headers が供給する。
// 区切りは '/' にすること('\' は clang で解決できない)。
#if defined(AQ_PLATFORM_UWP)
#pragma warning(push)
#pragma warning(disable:4065)
#include <DirectXTex.h>            // NuGet: directxtex_uwp (/MD, WINAPI_FAMILY_APP)
#pragma warning(pop)
#elif defined(AQ_PLATFORM_WIN32)
#pragma warning(push)
#pragma warning(disable:4065)
#include <DirectXTex/DirectXTex.h> // ソースは ThirdParty/DirectXTex を Engine に同梱ビルド
#pragma warning(pop)
#elif defined(__OBJC__)
// Objective-C++ TU(.mm)には持ち込まない。
//
// 非 Windows の DirectXTex は <wsl/winadapter.h> 経由で DirectX-Headers の
// スタブ basetsd.h を読む。これが `BOOL` を uint32_t に typedef し `interface` を
// struct に #define するため、Cocoa の `typedef bool BOOL` と衝突し、
// `@interface` が `struct` に置換されて Foundation のヘッダが全滅する。
// aq.h は PCH として全 TU に強制インクルードされるので、ここで切るしかない。
//
// .mm 側は Platform/Mac・HID/Mac・Sound/CoreAudio に閉じており(設計書 §10)、
// 画像デコードには触らないため機能欠落は無い。.mm から DirectXTex が要るように
// なったら、それは責務の置き場所を間違えているサインとして扱う。
#else
#include <DirectXTex/DirectXTex.h>
#endif

#include <vector>
#include <array>
#include <list>
#include <string>
#include <algorithm>
#include <functional>
#include <utility>

#include <memory>
#include <unordered_map>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <chrono>

#include <stdio.h>
#include <cstdint>
#include <cstring>
#include <assert.h>

// DirectXMath: macOS には Windows SDK が無いため、入手元を ThirdParty/DirectXMath の
// 同梱ヘッダに一本化する(設計書 §0「数学」/ §6)。SDK 版との版ずれを避けるため
// Windows も同梱側を使う。ThirdParty/DirectXMath/Inc もインクルードパスに入っており、
// DirectXTex 等が書く無修飾の <DirectXMath.h> / <DirectXPackedVector.h> も同梱側に解決される。
#include <DirectXMath/Inc/DirectXMath.h>

#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Utility.h"

#include "Util/CRC32.h"
#include "Util/ThreadPool.h"

#include "Utility.h"

// 起動診断用の簡易ログ。UWP(Xbox 実機)では通常デバッガを接続できないため、
// パッケージの LocalState/startup.log に追記する。Win32 では no-op。
// (Device Portal の File explorer から読み出して初期化の到達点を特定する)
namespace aq { void StartupLog(const char* msg); }

// 起動時間の計測マーク。プロセス起動からの経過 ms と前マークからの差分を
// カレントディレクトリの startup_timing.log(毎回上書き)と OutputDebugString に出す。
// Debug/Release 両構成で有効(実装 aq.cpp)。Win32 の StartupLog もここへ流す。
namespace aq
{
	void StartupMark(const char* label);
	void StartupMarkf(const char* fmt, ...);
}

#include "Engine.h"
#include "ECS/ECS.h"
#include "Component/TransformComponentSystem.h"
#include "Component/HierarchicalTransformComponent.h"
#include "Component/BodyComponentSystem.h"
#include "Graphics/Camera.h"
#include "Graphics/LightManager.h"
#include "Graphics/GraphicsDevice.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/FrameCommands.h"
#include "Rendering/Shadow/HardShadowRenderer.h"
#include "Rendering/PostProcess/PostProcessChain.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Rendering/Offscreen/OffscreenScenePass.h"
#include "Resource/Resource.h"
#include "UI/UIContext.h"
#include "UI/Screen/UIScreenManager.h"
