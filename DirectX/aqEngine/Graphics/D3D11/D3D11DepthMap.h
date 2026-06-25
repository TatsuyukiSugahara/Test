#pragma once
#include "Graphics/IDepthMap.h"
#include "D3D11RenderResources.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * D3D11 深度専用テクスチャ (Texture2DArray, ArraySize=4)。
		 * - R32_TYPELESS テクスチャを DSV (D32_FLOAT) と SRV (R32_FLOAT Texture2DArray) の両方で使用する。
		 * - スライスごとに独立した DSV を持ち、ディレクショナルライトごとにシャドウを書き込める。
		 * - 比較サンプラー (COMPARISON_LESS_EQUAL) を内蔵し PSs1 にバインドする。
		 */
		class D3D11DepthMap final : public IDepthMap
		{
		public:
			static constexpr uint32_t kArraySize = 4;

			D3D11DepthMap();
			~D3D11DepthMap();

			bool Create(uint32_t resolution);
			void Release();

			// IDepthMap
			IShaderResourceView* GetSRV()        const override { return const_cast<ShaderResourceView*>(&srv_); }
			IShaderResourceView* GetSliceSRV(uint32_t slice) const override
			{
				return (slice < kArraySize)
					? const_cast<ShaderResourceView*>(&sliceSrvs_[slice])
					: const_cast<ShaderResourceView*>(&srv_);
			}
			ISamplerState*       GetSampler()    const override { return const_cast<SamplerState*>(&sampler_); }
			uint32_t             GetResolution() const override { return resolution_; }

			// D3D11 固有: スライス 0 の DSV (後方互換)
			ID3D11DepthStencilView* GetDSV() const { return dsvs_[0]; }

			// D3D11 固有: 指定スライスの DSV
			ID3D11DepthStencilView* GetDSV(uint32_t slice) const
			{
				return (slice < kArraySize) ? dsvs_[slice] : dsvs_[0];
			}

		private:
			ID3D11Texture2D*        texture_            = nullptr;
			ID3D11DepthStencilView* dsvs_[kArraySize]   = {};
			ShaderResourceView      srv_;
			ShaderResourceView      sliceSrvs_[kArraySize];
			SamplerState            sampler_;
			uint32_t                resolution_ = 0;
		};
	}
}
