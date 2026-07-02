#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
#include "D3D12Common.h"
#include "D3D12RenderTarget.h"


namespace aq
{
	namespace graphics
	{
		void D3D12RenderTarget::BindBackBuffer(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle)
		{
			resource_     = resource;
			ownsResource_ = false;  // スワップチェーンが所有
			rtvHandle_    = rtvHandle;
			state_        = D3D12_RESOURCE_STATE_PRESENT;
		}


		bool D3D12RenderTarget::CreateOffscreen(ID3D12Device* device,
		                                        uint32_t width, uint32_t height,
		                                        DXGI_FORMAT colorFormat, bool hasDepth,
		                                        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
		                                        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
		                                        D3D12_CPU_DESCRIPTOR_HANDLE srvStaging,
		                                        D3D12_CPU_DESCRIPTOR_HANDLE uavStaging)
		{
			Release();
			if (!device) return false;
			colorFormat_ = colorFormat;

			D3D12_HEAP_PROPERTIES defaultHeap = {};
			defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

			// ── カラーリソース (RTV + SRV + UAV) ──
			// UAV はブルーム等のコンピュートが書き込むため全オフスクリーン RT に許可する。
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width            = width;
			rd.Height           = height;
			rd.DepthOrArraySize = 1;
			rd.MipLevels        = 1;
			rd.Format           = colorFormat;
			rd.SampleDesc.Count = 1;
			rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			                    | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			// 最適化クリア値はエンジンの実クリア色と一致しないと警告になるため指定しない (nullptr)。
			HRESULT hr = device->CreateCommittedResource(
				&defaultHeap, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&resource_));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 オフスクリーンRT カラー資源作成失敗"); return false; }
			ownsResource_ = true;
			state_        = D3D12_RESOURCE_STATE_RENDER_TARGET;

			device->CreateRenderTargetView(resource_, nullptr, rtvHandle);
			rtvHandle_ = rtvHandle;

			// カラー SRV (staging ヒープへ)
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format                  = colorFormat;
			srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Texture2D.MipLevels     = 1;
			device->CreateShaderResourceView(resource_, &srvDesc, srvStaging);
			srv_.SetStagingCPUHandle(srvStaging);
			srv_.SetBarrierSource(resource_, &state_);  // SRV読み取り前に RENDER_TARGET→SRV 遷移

			// カラー UAV (staging ヒープへ)
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format        = colorFormat;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			device->CreateUnorderedAccessView(resource_, nullptr, &uavDesc, uavStaging);
			uav_.SetStagingCPUHandle(uavStaging);
			uav_.SetBarrierSource(resource_, &state_);

			// ── 深度リソース (DSV) ──
			if (hasDepth)
			{
				depthFormat_ = DXGI_FORMAT_D24_UNORM_S8_UINT;
				D3D12_RESOURCE_DESC dd = {};
				dd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				dd.Width            = width;
				dd.Height           = height;
				dd.DepthOrArraySize = 1;
				dd.MipLevels        = 1;
				dd.Format           = depthFormat_;
				dd.SampleDesc.Count = 1;
				dd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

				D3D12_CLEAR_VALUE clearDepth = {};
				clearDepth.Format               = depthFormat_;
				clearDepth.DepthStencil.Depth   = 1.0f;
				clearDepth.DepthStencil.Stencil = 0;

				hr = device->CreateCommittedResource(
					&defaultHeap, D3D12_HEAP_FLAG_NONE, &dd,
					D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearDepth, IID_PPV_ARGS(&depthResource_));
				if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 オフスクリーンRT 深度資源作成失敗"); return false; }
				depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
				dsvDesc.Format        = depthFormat_;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				device->CreateDepthStencilView(depthResource_, &dsvDesc, dsvHandle);
				dsvHandle_ = dsvHandle;
			}
			return true;
		}


		void D3D12RenderTarget::Release()
		{
			if (ownsResource_) SafeReleaseD3D12(resource_);
			else               resource_ = nullptr;
			SafeReleaseD3D12(depthResource_);
			ownsResource_ = false;
		}


		IShaderResourceView& D3D12RenderTarget::GetRenderTargetSRV()
		{
			return srv_;
		}


		IUnorderedAccessView& D3D12RenderTarget::GetRenderTargetUAV()
		{
			return uav_;
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D12
