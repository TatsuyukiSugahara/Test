#pragma once

#pragma warning (disable  : 4201)

// #define DEBUG_MASTER		0	// ?f?o?b?O?r???h
// #define RELEASE_MASTER		1	// ?????[?X?r???h
// #define PREVIEW_MASTER		2	// ?}?X?^?[?r???h


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
#include <DirectXTex\DirectXTex.h>

#include <vector>
#include <array>
#include <list>
#include <string>
#include <algorithm>
#include <functional>

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
#include <assert.h>

#include <DirectXMath.h>

#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Utility.h"

#include "Util/CRC32.h"
#include "Util/ThreadPool.h"

#include "Utility.h"