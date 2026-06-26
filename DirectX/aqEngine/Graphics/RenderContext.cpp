#include "aq.h"
// 注: このファイルは D3D11 具象リソース (SamplerState/SRV/UAV/RenderTarget) の実装。
// D3D12 選択時は D3D12 側の実装を使うためビルド対象外にする。
#ifdef ENGINE_GRAPHICS_D3D11
#include "D3D11/D3D11RenderResources.h"


namespace aq
{
	namespace graphics
	{
		SamplerState::SamplerState()
			: samplerState_(nullptr)
		{
		}

		SamplerState::~SamplerState()
		{
			Release();
		}

		void SamplerState::Release()
		{
			if (samplerState_) {
				samplerState_->Release();
				samplerState_ = nullptr;
			}
		}


		/*******************************************/


		ShaderResourceView::ShaderResourceView()
			: shaderResourceView_(nullptr)
		{
		}

		ShaderResourceView::~ShaderResourceView()
		{
			Release();
		}

		void ShaderResourceView::Release()
		{
			if (shaderResourceView_) {
				shaderResourceView_->Release();
				shaderResourceView_ = nullptr;
			}
		}


		/*******************************************/


		UnorderedAccessView::UnorderedAccessView()
			: unorderedAccessView_(nullptr)
		{
		}

		UnorderedAccessView::~UnorderedAccessView()
		{
			Release();
		}

		void UnorderedAccessView::Release()
		{
			if (unorderedAccessView_) {
				unorderedAccessView_->Release();
				unorderedAccessView_ = nullptr;
			}
		}


		/*******************************************/


		RenderTarget::RenderTarget()
			: renderTarget_(nullptr)
			, renderTargetView_(nullptr)
			, depthStencil_(nullptr)
			, depthStencilView_(nullptr)
		{
		}

		RenderTarget::~RenderTarget()
		{
			Release();
		}

		void RenderTarget::Release()
		{
			renderTargetSRV_.Release();
			renderTargetUAV_.Release();
			if (renderTarget_)     { renderTarget_->Release();     renderTarget_     = nullptr; }
			if (renderTargetView_) { renderTargetView_->Release(); renderTargetView_ = nullptr; }
			if (depthStencil_)     { depthStencil_->Release();     depthStencil_     = nullptr; }
			if (depthStencilView_) { depthStencilView_->Release(); depthStencilView_ = nullptr; }
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D11
