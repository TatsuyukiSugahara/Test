#pragma once
#include "D3D12Common.h"
#include "Graphics/IDepthMap.h"
#include "D3D12Resources.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 深度専用テクスチャ (Phase 3) ──
		// D3D11DepthMap と同等: R32_TYPELESS の Texture2DArray(ArraySize=4)。
		// - スライスごとに独立 DSV(D32_FLOAT) を持ち、ライトごとにシャドウを書き込む。
		// - 全スライス SRV(R32_FLOAT Texture2DArray) を PSt4 にバインドする。
		// - 比較サンプラー(LESS_EQUAL)は D3D12RootSignature の静的サンプラー s1 を使うため、
		//   GetSampler() は no-op wrapper を返す。
		class D3D12DepthMap final : public IDepthMap
		{
		public:
			static constexpr uint32_t kArraySize = 4;

			D3D12DepthMap()           = default;
			~D3D12DepthMap() override { Release(); }

			// device がディスクリプタスロットを確保して渡す。
			//   dsvHandles[kArraySize]  : スライス別 DSV スロット
			//   srvArrayStaging         : 全スライス配列 SRV (staging ヒープ)
			//   srvSliceStaging[kArraySize]: スライス別 SRV (staging ヒープ・デバッグ表示用)
			bool Create(ID3D12Device* device, uint32_t resolution,
			            const D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandles,
			            D3D12_CPU_DESCRIPTOR_HANDLE srvArrayStaging,
			            const D3D12_CPU_DESCRIPTOR_HANDLE* srvSliceStaging);
			void Release();

			// IDepthMap
			IShaderResourceView* GetSRV() const override { return const_cast<D3D12SRVHandleRef*>(&srv_); }
			IShaderResourceView* GetSliceSRV(uint32_t slice) const override
			{
				return (slice < kArraySize)
					? const_cast<D3D12SRVHandleRef*>(&sliceSrvs_[slice])
					: const_cast<D3D12SRVHandleRef*>(&srv_);
			}
			ISamplerState*       GetSampler()    const override { return const_cast<D3D12SamplerState*>(&sampler_); }
			uint32_t             GetResolution() const override { return resolution_; }

			// D3D12 固有
			ID3D12Resource*             GetResource() const { return texture_; }
			D3D12_CPU_DESCRIPTOR_HANDLE GetDsv(uint32_t slice) const
			{
				return (slice < kArraySize) ? dsvHandles_[slice] : dsvHandles_[0];
			}
			D3D12_RESOURCE_STATES GetState() const { return state_; }
			void                  SetState(D3D12_RESOURCE_STATES s) { state_ = s; }

		private:
			ID3D12Resource*             texture_ = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandles_[kArraySize] = {};
			D3D12SRVHandleRef           srv_;                      // 全スライス配列 SRV
			D3D12SRVHandleRef           sliceSrvs_[kArraySize];    // スライス別 SRV
			D3D12SamplerState           sampler_;                  // 静的サンプラー s1 流用 (no-op)
			D3D12_RESOURCE_STATES       state_      = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			uint32_t                    resolution_ = 0;
		};
	}
}
