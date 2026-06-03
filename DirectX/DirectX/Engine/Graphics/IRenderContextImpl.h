#pragma once
#include "GraphicsTypes.h"
#include "IBuffer.h"
#include "IShader.h"
#include "IShaderResourceView.h"
#include "ISamplerState.h"
#include "IUnorderedAccessView.h"


namespace engine
{
	namespace graphics
	{
		class RenderTarget;

		/**
		 * RenderContext Implementor interface (Bridge Pattern)
		 *
		 * すべてのパラメータは API 非依存の抽象インターフェース型。
		 * D3D11 実装クラスがこれを継承し、内部で GetNativeHandle() を使って D3D11 型にキャストする。
		 */
		class IRenderContextImpl
		{
		public:
			virtual ~IRenderContextImpl() = default;

			virtual void OMSetRenderTargets(uint32_t numViews, RenderTarget* renderTarget) = 0;
			virtual void RSSetViewport(float topLeftX, float topLeftY, float width, float height) = 0;
			virtual void ClearRenderTargetView(uint32_t index, float* clearColor) = 0;

			virtual void IASetVertexBuffer(IVertexBuffer& vertexBuffer) = 0;
			virtual void IASetIndexBuffer(IIndexBuffer& indexBuffer) = 0;
			virtual void IASetPrimitiveTopology(PrimitiveTopology topology) = 0;
			virtual void IASetInputLayout(IShader& vsShader) = 0;

			virtual void VSSetShader(IShader& shader) = 0;
			virtual void VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) = 0;

			virtual void PSSetShader(IShader& shader) = 0;
			virtual void PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) = 0;
			virtual void PSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView) = 0;
			virtual void PSUnsetShaderResource(uint32_t slot) = 0;
			virtual void PSSetSampler(uint32_t startSlot, ISamplerState& samplerState) = 0;

			virtual void CSSetShader(IShader& shader) = 0;
			virtual void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) = 0;
			virtual void CSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView) = 0;
			virtual void CSUnsetShaderResource(uint32_t slot) = 0;
			virtual void CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& unorderedAccessView) = 0;
			virtual void CSUnsetUnorderedAccessView(uint32_t slot) = 0;

			virtual void Draw(uint32_t vertexCount, uint32_t startVertexLocation) = 0;
			virtual void DrawIndexed(uint32_t indexCount) = 0;
			virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

			/** テンプレートメソッドから呼ばれる型消去版 */
			virtual void UpdateSubresourceRaw(void* gpuBuffer, const void* srcData) = 0;
			virtual void CopyResourceRaw(void* dest, void* src) = 0;
			virtual void MapRaw(void* buffer, uint32_t subResource, MapType mapType, uint32_t flags, MappedSubresource& mapped) = 0;
			virtual void UnmapRaw(void* buffer, uint32_t subResource) = 0;
		};
	}
}
