#include "aq.h"
#include "D3D12RenderTarget.h"


namespace aq
{
	namespace graphics
	{
		void D3D12RenderTarget::BindBackBuffer(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle)
		{
			resource_  = resource;
			rtvHandle_ = rtvHandle;
			state_     = D3D12_RESOURCE_STATE_PRESENT;
		}


		IShaderResourceView& D3D12RenderTarget::GetRenderTargetSRV()
		{
			// Phase 0 ではバックバッファを SRV としてサンプリングしない。
			EngineAssertMsg(false, "D3D12 RenderTarget SRV は Phase 2 で実装予定");
			return *reinterpret_cast<IShaderResourceView*>(this);  // 到達しない (上で assert)
		}


		IUnorderedAccessView& D3D12RenderTarget::GetRenderTargetUAV()
		{
			EngineAssertMsg(false, "D3D12 RenderTarget UAV は Phase 2 で実装予定");
			return *reinterpret_cast<IUnorderedAccessView*>(this);  // 到達しない (上で assert)
		}
	}
}
