#pragma once
#include <cstdint>
#include "IShaderResourceView.h"
#include "ISamplerState.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * API 非依存の深度専用テクスチャ。
		 * シャドウパスでは DSV として書き込み、メインパスでは SRV として PSt4 にバインドする。
		 * D3D11 具象型 (D3D11DepthMap) にのみ D3D11 固有の DSV アクセサが存在する。
		 */
		class IDepthMap
		{
		public:
			virtual ~IDepthMap() = default;

			/** PSt4 にバインドするための SRV */
			virtual IShaderResourceView* GetSRV()       const = 0;

			/** PSs1 にバインドする比較サンプラー (LESS_EQUAL) */
			virtual ISamplerState*       GetSampler()   const = 0;

			/** テクスチャの解像度 (正方形) */
			virtual uint32_t             GetResolution() const = 0;
		};
	}
}
