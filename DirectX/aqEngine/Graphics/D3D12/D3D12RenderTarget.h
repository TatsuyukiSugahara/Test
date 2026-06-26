#pragma once
#include "D3D12Common.h"
#include "Graphics/IRenderTarget.h"
#include "Graphics/IUnorderedAccessView.h"
#include "D3D12Resources.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 レンダーターゲット (Phase 0: バックバッファ / Phase 3: オフスクリーン色+深度) ──
		// 2 つのモードを持つ:
		//   (a) バックバッファ束ね (BindBackBuffer)        … スワップチェーンが所有・CopyToBackBuffer の宛先
		//   (b) オフスクリーン (CreateOffscreen)            … 自前のカラー資源+RTV(+SRV)(+深度+DSV)
		// メイン RT(深度付き)・GBuffer・ポストプロセス RT を統一的に扱う。
		class D3D12RenderTarget final : public IRenderTarget
		{
		public:
			D3D12RenderTarget()           = default;
			~D3D12RenderTarget() override { Release(); }

			// (a) バックバッファリソースと RTV ハンドルを紐付ける (スワップチェーンが所有)
			void BindBackBuffer(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle);

			// (b) オフスクリーンRTを生成する。device と各ディスクリプタハンドルは device 側が確保して渡す。
			//   rtvHandle: 確保済み RTV スロット / dsvHandle: 深度ありのとき DSV スロット
			//   srvStaging: カラーを SRV として読むための staging ヒープ上の SRV スロット
			bool CreateOffscreen(ID3D12Device* device,
			                     uint32_t width, uint32_t height,
			                     DXGI_FORMAT colorFormat, bool hasDepth,
			                     D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
			                     D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
			                     D3D12_CPU_DESCRIPTOR_HANDLE srvStaging,
			                     D3D12_CPU_DESCRIPTOR_HANDLE uavStaging);
			void Release();

			ID3D12Resource*             GetResource() const  { return resource_; }
			D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const { return rtvHandle_; }
			D3D12_RESOURCE_STATES       GetState() const     { return state_; }
			void                        SetState(D3D12_RESOURCE_STATES state) { state_ = state; }
			DXGI_FORMAT                 GetColorFormat() const { return colorFormat_; }

			bool                        HasDepth() const     { return depthResource_ != nullptr; }
			ID3D12Resource*             GetDepthResource() const { return depthResource_; }
			D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const { return dsvHandle_; }
			D3D12_RESOURCE_STATES       GetDepthState() const { return depthState_; }
			void                        SetDepthState(D3D12_RESOURCE_STATES s) { depthState_ = s; }
			DXGI_FORMAT                 GetDepthFormat() const { return depthFormat_; }

			// IRenderTarget
			IShaderResourceView&  GetRenderTargetSRV() override;
			IUnorderedAccessView& GetRenderTargetUAV() override;

		private:
			// カラー (バックバッファ時は非所有、オフスクリーン時は所有)
			ID3D12Resource*             resource_     = nullptr;
			bool                        ownsResource_ = false;
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_    = {};
			D3D12_RESOURCE_STATES       state_        = D3D12_RESOURCE_STATE_PRESENT;
			DXGI_FORMAT                 colorFormat_  = DXGI_FORMAT_R8G8B8A8_UNORM;
			D3D12SRVHandleRef           srv_;  // カラーSRV (staging ハンドル参照)
			D3D12UAVHandleRef           uav_;  // カラーUAV (オフスクリーンのみ・コンピュート書き込み用)

			// 深度 (オフスクリーンで hasDepth のときのみ)
			ID3D12Resource*             depthResource_ = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_     = {};
			D3D12_RESOURCE_STATES       depthState_    = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			DXGI_FORMAT                 depthFormat_   = DXGI_FORMAT_UNKNOWN;
		};
	}
}
