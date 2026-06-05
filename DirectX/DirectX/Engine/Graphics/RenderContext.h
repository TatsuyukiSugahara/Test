#pragma once
#include <memory>
#include "IBuffer.h"
#include "IShader.h"
#include "IShaderResourceView.h"
#include "ISamplerState.h"
#include "IUnorderedAccessView.h"
#include "GraphicsTypes.h"
#include "IRenderContextImpl.h"
// D3D11 concrete resources (SamplerState/ShaderResourceView/UnorderedAccessView/RenderTarget):
//   Graphics/D3D11/D3D11RenderResources.h


namespace engine
{
	namespace graphics
	{
		class RenderTarget;

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


	}
}
