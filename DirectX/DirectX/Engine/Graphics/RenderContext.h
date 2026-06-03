#pragma once
#include <memory>
#include "IBuffer.h"
#include "IShader.h"
#include "IShaderResourceView.h"
#include "ISamplerState.h"
#include "IUnorderedAccessView.h"
#include "GraphicsTypes.h"
#include "IRenderContextImpl.h"


namespace engine
{
	namespace graphics
	{
		/*******************************************/


		class SamplerState : public ISamplerState
		{
		private:
			ID3D11SamplerState* samplerState_;

		public:
			SamplerState();
			~SamplerState();

			bool  Create(const SamplerDesc& desc) override;
			void  Release() override;
			void* GetNativeHandle() const override { return static_cast<void*>(samplerState_); }

			/** D3D11 固有アクセサ */
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

			bool  Create(StructuredBuffer& structuredBuffer);
			bool  Create(ID3D11Texture2D* texture);
			void  Release() override;
			void* GetNativeHandle() const override { return static_cast<void*>(shaderResourceView_); }

			/** D3D11 固有アクセサ */
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

			bool  Create(StructuredBuffer& structuredBuffer);
			bool  Create(ID3D11Texture2D* texture);
			void  Release() override;
			void* GetNativeHandle() const override { return static_cast<void*>(unorderedAccessView_); }

			/** D3D11 固有アクセサ */
			inline ID3D11UnorderedAccessView*& GetBody() { return unorderedAccessView_; }
		};




		/*******************************************/


		class RenderTarget
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

			inline ID3D11Texture2D*        GetRenderTarget()     const { return renderTarget_; }
			inline ID3D11RenderTargetView* GetrenderTargetView() const { return renderTargetView_; }
			inline IShaderResourceView&    GetRenderTargetSRV()        { return renderTargetSRV_; }
			inline IUnorderedAccessView&   GetRenderTargetUAV()        { return renderTargetUAV_; }
			inline ID3D11DepthStencilView* GetDepthStencilView() const { return depthStencilView_; }
		};




		/*******************************************/


		/**
		 * RenderContext Abstraction (Bridge Pattern)
		 *
		 * 描画コマンドを発行するクラス。メソッドパラメータはすべて抽象インターフェース型。
		 */
		class RenderContext
		{
		public:
			RenderContext() = default;
			~RenderContext() = default;

			RenderContext(const RenderContext&) = delete;
			RenderContext& operator=(const RenderContext&) = delete;

			void SetImpl(std::unique_ptr<IRenderContextImpl> impl) { impl_ = std::move(impl); }

			template<typename TImpl>
			TImpl* GetImplAs() { return static_cast<TImpl*>(impl_.get()); }


		public:
			void OMSetRenderTargets(uint32_t numViews, RenderTarget* renderTarget)
			{
				impl_->OMSetRenderTargets(numViews, renderTarget);
			}
			void RSSetViewport(float topLeftX, float topLeftY, float width, float height)
			{
				impl_->RSSetViewport(topLeftX, topLeftY, width, height);
			}
			void ClearRenderTargetView(uint32_t index, float* clearColor)
			{
				impl_->ClearRenderTargetView(index, clearColor);
			}

			void IASetVertexBuffer(IVertexBuffer& vertexBuffer)
			{
				impl_->IASetVertexBuffer(vertexBuffer);
			}
			void IASetIndexBuffer(IIndexBuffer& indexBuffer)
			{
				impl_->IASetIndexBuffer(indexBuffer);
			}
			void IASetPrimitiveTopology(PrimitiveTopology topology)
			{
				impl_->IASetPrimitiveTopology(topology);
			}
			void IASetInputLayout(IShader& vsShader)
			{
				impl_->IASetInputLayout(vsShader);
			}

			void VSSetShader(IShader& shader)
			{
				impl_->VSSetShader(shader);
			}
			void VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
			{
				impl_->VSSetConstantBuffer(startSlot, constantBuffer);
			}

			void PSSetShader(IShader& shader)
			{
				impl_->PSSetShader(shader);
			}
			void PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
			{
				impl_->PSSetConstantBuffer(startSlot, constantBuffer);
			}
			void PSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView)
			{
				impl_->PSSetShaderResource(startSlot, shaderResourceView);
			}
			void PSUnsetShaderResource(uint32_t slot)
			{
				impl_->PSUnsetShaderResource(slot);
			}
			void PsSetSampler(uint32_t startSlot, ISamplerState& samplerState)
			{
				impl_->PSSetSampler(startSlot, samplerState);
			}

			void CSSetShader(IShader& shader)
			{
				impl_->CSSetShader(shader);
			}
			void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
			{
				impl_->CSSetConstantBuffer(startSlot, constantBuffer);
			}
			void CSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView)
			{
				impl_->CSSetShaderResource(startSlot, shaderResourceView);
			}
			void CSUnsetShaderResource(uint32_t slot)
			{
				impl_->CSUnsetShaderResource(slot);
			}
			void CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& unorderedAccessView)
			{
				impl_->CSSetUnorderedAccessView(startSlot, unorderedAccessView);
			}
			void CSUnsetUnorderedAccessView(uint32_t slot)
			{
				impl_->CSUnsetUnorderedAccessView(slot);
			}

			void Draw(uint32_t vertexCount, uint32_t startVertexLocation)
			{
				impl_->Draw(vertexCount, startVertexLocation);
			}
			void DrawIndexed(uint32_t indexCount)
			{
				impl_->DrawIndexed(indexCount);
			}
			void Dispatch(uint32_t x, uint32_t y, uint32_t z)
			{
				impl_->Dispatch(x, y, z);
			}

			/** GetNativeHandle() で void* を取り出して型消去版の impl へ委譲 */
			template <typename TBuffer, typename SrcBuffer>
			void UpdateSubresource(TBuffer& gpuBuffer, SrcBuffer buffer)
			{
				if (gpuBuffer.GetNativeHandle()) {
					impl_->UpdateSubresourceRaw(gpuBuffer.GetNativeHandle(), &buffer);
				}
			}

			template <typename TResource>
			void CopyResource(TResource& destResource, TResource& srcResource)
			{
				if (destResource.GetNativeHandle() && srcResource.GetNativeHandle()) {
					impl_->CopyResourceRaw(destResource.GetNativeHandle(), srcResource.GetNativeHandle());
				}
			}

			template <typename TBuffer>
			void Map(TBuffer& buffer, uint32_t subResource, MapType mapType, uint32_t mapFlags, MappedSubresource& mappedResource)
			{
				if (buffer.GetNativeHandle()) {
					impl_->MapRaw(buffer.GetNativeHandle(), subResource, mapType, mapFlags, mappedResource);
				}
			}

			template <typename TBuffer>
			void Unmap(TBuffer& buffer, uint32_t subResource)
			{
				if (buffer.GetNativeHandle()) {
					impl_->UnmapRaw(buffer.GetNativeHandle(), subResource);
				}
			}


		private:
			std::unique_ptr<IRenderContextImpl> impl_;
		};




		/*******************************************/


		class Texture
		{
		public:
			static IShaderResourceView* Create2D(const DirectX::TexMetadata& metaData, const DirectX::Image* images);
		};
	}
}
