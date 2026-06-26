#pragma once
#include "D3D12Common.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 レンダーターゲット (Phase 0: スワップチェーンのバックバッファのみ) ──
		// バックバッファリソースと RTV ディスクリプタハンドルを保持する。
		// SRV / UAV (オフスクリーン RT のサンプリング) は Phase 2 以降で実装する。
		class D3D12RenderTarget : public IRenderTarget
		{
		private:
			ID3D12Resource*             resource_ = nullptr;  // バックバッファ (所有しない: スワップチェーン管理)
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_ = {};
			D3D12_RESOURCE_STATES       state_     = D3D12_RESOURCE_STATE_PRESENT;

		public:
			D3D12RenderTarget()           = default;
			~D3D12RenderTarget() override = default;

			// バックバッファリソースと RTV ハンドルを紐付ける (スワップチェーンが所有)
			void BindBackBuffer(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle);

			ID3D12Resource*             GetResource() const  { return resource_; }
			D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const { return rtvHandle_; }
			D3D12_RESOURCE_STATES       GetState() const     { return state_; }
			void                        SetState(D3D12_RESOURCE_STATES state) { state_ = state; }

			// TODO(P2): オフスクリーン RT のサンプリング用に SRV/UAV を実装する。
			IShaderResourceView&  GetRenderTargetSRV() override;
			IUnorderedAccessView& GetRenderTargetUAV() override;
		};
	}
}
