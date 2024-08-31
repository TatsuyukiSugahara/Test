#pragma once

#pragma warning (disable  : 4201)

// #define DEBUG_MASTER		0	// デバッグビルド
// #define RELEASE_MASTER		1	// リリースビルド
// #define PREVIEW_MASTER		2	// マスタービルド


#include <windows.h>
#pragma warning(push)
#pragma warning(disable:4005)
#include <d3d11.h>
#pragma warning(pop)
#pragma comment(lib, "d3d11.lib")
#include <d3dcompiler.h>
#pragma comment(lib,"d3dcompiler.lib")


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

#include <memory>
#include <unordered_map>

#include <tchar.h>
#include <stdio.h>
#include <cstdint>
#include <assert.h>

#include <DirectXMath.h>

#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Utility.h"

#include "Utility.h"