#pragma once

#pragma warning (disable  : 4201)

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


#define NOMINMAX
#include <windows.h>

#ifdef ENGINE_GRAPHICS_D3D11
#pragma warning(push)
#pragma warning(disable:4005)
#include <d3d11.h>
#pragma warning(pop)
#pragma comment(lib, "d3d11.lib")
#include <d3dcompiler.h>
#pragma comment(lib,"d3dcompiler.lib")
#endif // ENGINE_GRAPHICS_D3D11

#ifdef ENGINE_GRAPHICS_D3D12
#pragma warning(push)
#pragma warning(disable:4005)
#include <d3d12.h>
#include <dxgi1_6.h>
#pragma warning(pop)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#include <d3dcompiler.h>
#pragma comment(lib,"d3dcompiler.lib")
#endif // ENGINE_GRAPHICS_D3D12

#ifdef ENGINE_GRAPHICS_VULKAN
// Vulkan ヘッダ本体は Graphics/Vulkan/VulkanCommon.h 側で取り込む (VK_USE_PLATFORM_WIN32_KHR 定義込み)。
// ここでは最終リンクへ vulkan-1.lib を要求する (ライブラリパスは Game vcxproj の $(VULKAN_SDK)\Lib)。
#pragma comment(lib, "vulkan-1.lib")
#endif // ENGINE_GRAPHICS_VULKAN


//DirectInput
#define	DIRECTINPUT_VERSION	0x0800
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#include <dinput.h>


// DirectXTex:
//  - デスクトップ: ThirdParty の prebuilt lib(静的CRT /MT)+ ヘッダを使う。
//  - UWP(Xbox): /MD 必須で prebuilt(/MT)と衝突するため、NuGet パッケージ
//    "directxtex_uwp" が提供する /MD ビルド(ヘッダ + lib)を使う。lib は NuGet の
//    .targets が自動リンクするので pragma comment(lib) は不要。ヘッダは <DirectXTex.h>。
#if defined(AQ_PLATFORM_UWP)
#pragma warning(push)
#pragma warning(disable:4065)
#include <DirectXTex.h>            // NuGet: directxtex_uwp (/MD, WINAPI_FAMILY_APP)
#pragma warning(pop)
#else
#if _DEBUG
	#pragma comment(lib, "DirectXTex/x64/Debug/DirectXTex.lib")
#else
	#pragma comment(lib, "DirectXTex/x64/Release/DirectXTex.lib")
#endif
#pragma warning(push)
#pragma warning(disable:4065)
#include <DirectXTex\DirectXTex.h>
#pragma warning(pop)
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

#include <tchar.h>
#include <stdio.h>
#include <cstdint>
#include <cstring>
#include <assert.h>

#include <DirectXMath.h>

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
#include "Rendering/PostProcess/BloomRenderer.h"
#include "Rendering/Deferred/DeferredRenderer.h"
#include "Resource/Resource.h"
#include "UI/UIContext.h"
#include "UI/Screen/UIScreenManager.h"
