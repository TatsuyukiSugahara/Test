#pragma once

#pragma warning (disable  : 4201)

// Graphics API selection.
// Define ENGINE_GRAPHICS_D3D11 here, or pass it via project preprocessor settings.
#define ENGINE_GRAPHICS_D3D11
// #define ENGINE_GRAPHICS_D3D12
// #define ENGINE_GRAPHICS_VULKAN

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


//DirectInput
#define	DIRECTINPUT_VERSION	0x0800
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#include <dinput.h>


#if _DEBUG
	#pragma comment(lib, "DirectXTex/x64/Debug/DirectXTex.lib")
#else
	#pragma comment(lib, "DirectXTex/x64/Release/DirectXTex.lib")
#endif
#pragma warning(push)
#pragma warning(disable:4065)
#include <DirectXTex\DirectXTex.h>
#pragma warning(pop)

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
