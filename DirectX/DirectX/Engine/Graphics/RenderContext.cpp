#include "../EnginePreCompile.h"
#include "RenderContext.h"


namespace engine
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
