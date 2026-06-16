#pragma once
#include "Graphics/IRenderContextImpl.h"


namespace engine
{
	namespace graphics
	{
		/**
		 * DirectX 11 RenderContext Concrete Implementor (Bridge Pattern)
		 *
		 * ID3D11DeviceContext* などは .cpp にのみ登場する。
		 * 抽象インターフェース型を D3D11 具象型にキャストして API を呼ぶ。
		 */
		class D3D11RenderContextImpl : public IRenderContextImpl
		{
		public:
			explicit D3D11RenderContextImpl(ID3D11DeviceContext* context);
			~D3D11RenderContextImpl() override = default;

			void OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget) override;
			void RSSetViewport(float topLeftX, float topLeftY, float width, float height) override;
			void ClearRenderTargetView(uint32_t index, float* clearColor) override;

			void IASetVertexBuffer(IVertexBuffer& vertexBuffer) override;
			void IASetIndexBuffer(IIndexBuffer& indexBuffer) override;
			void IASetPrimitiveTopology(PrimitiveTopology topology) override;
			void IASetInputLayout(IShader& vsShader) override;

			void VSSetShader(IShader& shader) override;
			void VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;

			void PSSetShader(IShader& shader) override;
			void PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void PSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView) override;
			void PSUnsetShaderResource(uint32_t slot) override;
			void PSSetSampler(uint32_t startSlot, ISamplerState& samplerState) override;

			void CSSetShader(IShader& shader) override;
			void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void CSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView) override;
			void CSUnsetShaderResource(uint32_t slot) override;
			void CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& unorderedAccessView) override;
			void CSUnsetUnorderedAccessView(uint32_t slot) override;

			void Draw(uint32_t vertexCount, uint32_t startVertexLocation) override;
			void DrawIndexed(uint32_t indexCount) override;
			void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

			void UpdateConstantBuffer(IConstantBuffer& buf, const void* data) override;

			void OMSetDepthOnlyTarget(IDepthMap& depthMap) override;
			void ClearDepthMap(IDepthMap& depthMap) override;
			void PSUnsetShader() override;

			/** D3D11 専用: ラスタライザーステート */
			void RSSetState(ID3D11RasterizerState* state);

		private:
			static D3D11_PRIMITIVE_TOPOLOGY ToD3D11(PrimitiveTopology topology);
			static D3D11_MAP               ToD3D11(MapType mapType);

		private:
			static constexpr uint32_t MAX_MRT_NUM = 8;

			ID3D11DeviceContext*    context_;
			D3D11_VIEWPORT          viewport_;
			ID3D11RenderTargetView* renderTargetViews_[MAX_MRT_NUM];
			ID3D11DepthStencilView* depthStencilView_;
			uint32_t                renderTargetViewNum_;
		};
	}
}
