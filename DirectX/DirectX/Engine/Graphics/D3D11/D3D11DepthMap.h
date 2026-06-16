#pragma once
#include "Graphics/IDepthMap.h"
#include "D3D11RenderResources.h"


namespace engine
{
	namespace graphics
	{
		/**
		 * D3D11 深度専用テクスチャ。
		 * - R32_TYPELESS テクスチャを DSV (D32_FLOAT) と SRV (R32_FLOAT) の両方で使用する。
		 * - 比較サンプラー (COMPARISON_LESS_EQUAL) を内蔵し PSs1 にバインドする。
		 */
		class D3D11DepthMap final : public IDepthMap
		{
		public:
			D3D11DepthMap();
			~D3D11DepthMap();

			bool Create(uint32_t resolution);
			void Release();

			// IDepthMap
			IShaderResourceView* GetSRV()        const override { return const_cast<ShaderResourceView*>(&srv_); }
			ISamplerState*       GetSampler()    const override { return const_cast<SamplerState*>(&sampler_); }
			uint32_t             GetResolution() const override { return resolution_; }

			// D3D11 固有: D3D11RenderContextImpl から DSV を取得するために使用
			ID3D11DepthStencilView* GetDSV() const { return dsv_; }

		private:
			ID3D11Texture2D*        texture_    = nullptr;
			ID3D11DepthStencilView* dsv_        = nullptr;
			ShaderResourceView      srv_;
			SamplerState            sampler_;
			uint32_t                resolution_ = 0;
		};
	}
}
