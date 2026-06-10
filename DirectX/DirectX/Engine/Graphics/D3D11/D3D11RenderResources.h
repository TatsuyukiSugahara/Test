#pragma once
#include "../ISamplerState.h"
#include "../IShaderResourceView.h"
#include "../IUnorderedAccessView.h"
#include "../IRenderTarget.h"
#include "../GraphicsTypes.h"

namespace engine
{
	namespace graphics
	{
		class StructuredBuffer;

		/*******************************************/

		class SamplerState : public ISamplerState
		{
		private:
			ID3D11SamplerState* samplerState_;

		public:
			SamplerState();
			~SamplerState();

			bool Create(const SamplerDesc& desc) override;
			void Release() override;

			ID3D11SamplerState*& GetBody() { return samplerState_; }
		};

		/*******************************************/

		class ShaderResourceView : public IShaderResourceView
		{
		private:
			ID3D11ShaderResourceView* shaderResourceView_;

		public:
			ShaderResourceView();
			~ShaderResourceView();

			bool Create(StructuredBuffer& structuredBuffer);
			bool Create(ID3D11Texture2D* texture);
			void Release() override;

			inline ID3D11ShaderResourceView*& GetBody() { return shaderResourceView_; }
		};

		/*******************************************/

		class UnorderedAccessView : public IUnorderedAccessView
		{
		private:
			ID3D11UnorderedAccessView* unorderedAccessView_;

		public:
			UnorderedAccessView();
			~UnorderedAccessView();

			bool Create(StructuredBuffer& structuredBuffer);
			bool Create(ID3D11Texture2D* texture);
			void Release() override;

			inline ID3D11UnorderedAccessView*& GetBody() { return unorderedAccessView_; }
		};

		/*******************************************/

		class RenderTarget : public IRenderTarget
		{
		private:
			ID3D11Texture2D*        renderTarget_;
			ID3D11RenderTargetView* renderTargetView_;
			ID3D11Texture2D*        depthStencil_;
			ID3D11DepthStencilView* depthStencilView_;
			ShaderResourceView      renderTargetSRV_;
			UnorderedAccessView     renderTargetUAV_;

		public:
			RenderTarget();
			~RenderTarget();

			bool Create(int32_t width, int32_t height, int32_t mipLevel,
				PixelFormat colorFormat, PixelFormat depthStencilFormat,
				SampleDesc multiSampleDesc,
				ID3D11Texture2D* renderTarget = nullptr,
				ID3D11Texture2D* depthStencil = nullptr);
			void Release();

			// IRenderTarget
			IShaderResourceView&  GetRenderTargetSRV() override { return renderTargetSRV_; }
			IUnorderedAccessView& GetRenderTargetUAV() override { return renderTargetUAV_; }

			// D3D11 固有アクセサ (D3D11 実装コードのみ使用)
			inline ID3D11Texture2D*        GetRenderTarget()     const { return renderTarget_; }
			inline ID3D11RenderTargetView* GetrenderTargetView() const { return renderTargetView_; }
			inline ID3D11DepthStencilView* GetDepthStencilView() const { return depthStencilView_; }
		};
	}
}
