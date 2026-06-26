#pragma once
// DirectX 12 バックエンド内部共通ヘッダ。D3D12 系の .cpp からのみ include する。
// d3d12.h / dxgi は ENGINE_GRAPHICS_D3D12 の有無に関わらずこの TU でのみ取り込み、
// バックエンド未選択時 (既定 D3D11) でもコンパイル・リンク可能にしておく。
#include <d3d12.h>
#include <dxgi1_6.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")


namespace aq
{
	namespace graphics
	{
		// COM ポインタを安全に解放する (D3D11 層の手動 Release パターンに合わせる)
		template<typename T>
		inline void SafeReleaseD3D12(T*& p)
		{
			if (p)
			{
				p->Release();
				p = nullptr;
			}
		}
	}
}
