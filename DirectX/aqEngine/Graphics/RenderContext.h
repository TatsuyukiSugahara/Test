#pragma once
#include <memory>
#include "IBuffer.h"
#include "IShader.h"
#include "IShaderResourceView.h"
#include "ISamplerState.h"
#include "IUnorderedAccessView.h"
#include "IDepthMap.h"
#include "GraphicsTypes.h"
#include "IRenderContextImpl.h"
// D3D11 concrete resources (SamplerState/ShaderResourceView/UnorderedAccessView/RenderTarget):
//   Graphics/D3D11/D3D11RenderResources.h


namespace aq
{
	namespace graphics
	{
		class IRenderTarget;

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
			void OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget)
			{
				impl_->OMSetRenderTargets(numViews, renderTarget);
			}
			void OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets)
			{
				impl_->OMSetMRTRenderTargets(numViews, renderTargets);
			}
			void OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT)
			{
				impl_->OMSetRenderTargetWithDepth(colorRT, depthSourceRT);
			}
			void OMSetDepthMode(DepthMode mode)
			{
				impl_->OMSetDepthMode(mode);
			}
			void OMSetBlendMode(BlendMode mode)
			{
				impl_->OMSetBlendMode(mode);
			}
			void RSSetViewport(float topLeftX, float topLeftY, float width, float height)
			{
				impl_->RSSetViewport(topLeftX, topLeftY, width, height);
			}
			void RSSetScissorEnabled(bool enabled)
			{
				impl_->RSSetScissorEnabled(enabled);
			}
			void RSSetScissorRect(int x, int y, int w, int h)
			{
				impl_->RSSetScissorRect(x, y, w, h);
			}
			void ClearRenderTargetView(uint32_t index, float* clearColor)
			{
				impl_->ClearRenderTargetView(index, clearColor);
			}
			void ClearDepthBuffer()
			{
				impl_->ClearDepthBuffer();
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
			void CSUnsetShader()
			{
				impl_->CSUnsetShader();
			}
			void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
			{
				impl_->CSSetConstantBuffer(startSlot, constantBuffer);
			}
			void CSSetSampler(uint32_t startSlot, ISamplerState& samplerState)
			{
				impl_->CSSetSampler(startSlot, samplerState);
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

			template <typename SrcData>
			void UpdateSubresource(IConstantBuffer& buf, const SrcData& data)
			{
				impl_->UpdateConstantBuffer(buf, &data);
			}

			void OMSetDepthOnlyTarget(IDepthMap& depthMap)
			{
				impl_->OMSetDepthOnlyTarget(depthMap);
			}
			void ClearDepthMap(IDepthMap& depthMap)
			{
				impl_->ClearDepthMap(depthMap);
			}
			void PSUnsetShader()
			{
				impl_->PSUnsetShader();
			}

		private:
			std::unique_ptr<IRenderContextImpl> impl_;
		};


		/*******************************************/


	}
}
