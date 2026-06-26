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
		// frames-in-flight 数。CPU が GPU より先行できるフレーム数。
		// バックバッファ枚数 (RENDER_TARGET_COUNT) と一致させる。
		// 毎フレーム CPU が書き込む動的リソース (定数バッファ / 動的VB・IB / SRVリング /
		// コマンドアロケータ) はこの数だけ多重化し、フェンスで世代管理する。
		constexpr uint32_t D3D12_FRAME_COUNT = 2;


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
