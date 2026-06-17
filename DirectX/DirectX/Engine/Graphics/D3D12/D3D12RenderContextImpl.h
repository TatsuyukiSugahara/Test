#pragma once
#include "Graphics/IRenderContextImpl.h"

struct ID3D12GraphicsCommandList;


namespace aq
{
	namespace graphics
	{
		/**
		 * DirectX 12 RenderContext Concrete Implementor (Bridge Pattern)
		 *
		 * 各メソッドがエンジンの抽象描画 API を D3D12 コマンドリスト記録呼び出しに変換する。
		 * 記録済みリストは D3D12GraphicsDeviceImpl::Present()（または EndFrame()）が
		 * CommandQueue に Submit する。
		 *
		 * DX11 → DX12 変換対応表
		 * -----------------------
		 * | DX11 概念                  | DX12 等価                                            |
		 * |---------------------------|-----------------------------------------------------|
		 * | IASetVertexBuffers         | IASetVertexBuffers（同名、D3D12 構造体）             |
		 * | IASetIndexBuffer           | IASetIndexBuffer（同名、D3D12 構造体）               |
		 * | VSSetShader + PSSetShader  | SetPipelineState（VS+PS+IL を合わせた PSO）          |
		 * | VSSetConstantBuffer (slot) | SetGraphicsRootConstantBufferView（ルートパラメータ）|
		 * | PSSetShaderResource        | SetGraphicsRootDescriptorTable                      |
		 * | PSSetSamplers              | SetGraphicsRootDescriptorTable（サンプラーヒープ）  |
		 * | OMSetRenderTargets         | OMSetRenderTargets（D3D12 RTV ハンドル）            |
		 * | ClearRenderTargetView      | ClearRenderTargetView（D3D12 RTV ハンドル）         |
		 * | UpdateConstantBuffer       | Map / 書き込み / Unmap（アップロードヒープスライス）|
		 * | DrawIndexed                | DrawIndexedInstanced                                |
		 *
		 * シェーダー / PSO キャッシュ
		 * ---------------------------
		 * DX11 では VSSetShader / PSSetShader が即座に有効になるが、DX12 では
		 * VS+PS+IL+ブレンド/ラスタライザー/デプス・ステンシル記述を組み合わせた PSO になる。
		 * このクラスは (VS blob, PS blob, IL) をキーとした PSO キャッシュを持ち、
		 * 初回描画時にコンパイル、以降はキャッシュを使う。
		 *
		 * ルートシグネチャ
		 * ----------------
		 * エンジンの現在の描画コールに対応する静的ルートシグネチャ:
		 *   [0] CBV  b0    — VSConstantBuffer（world/view/proj）
		 *   [1] SRV  t0    — ディフューズテクスチャ
		 *   [2] Sampler s0 — テクスチャサンプラー
		 * コンピュートシェーダーは別途ルートシグネチャが必要。
		 */
		class D3D12RenderContextImpl : public IRenderContextImpl
		{
		public:
			explicit D3D12RenderContextImpl(ID3D12GraphicsCommandList* cmdList);
			~D3D12RenderContextImpl() override = default;

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
			void PSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv) override;
			void PSUnsetShaderResource(uint32_t slot) override;
			void PSSetSampler(uint32_t startSlot, ISamplerState& samplerState) override;

			void CSSetShader(IShader& shader) override;
			void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void CSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv) override;
			void CSUnsetShaderResource(uint32_t slot) override;
			void CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& uav) override;
			void CSUnsetUnorderedAccessView(uint32_t slot) override;

			void Draw(uint32_t vertexCount, uint32_t startVertexLocation) override;
			void DrawIndexed(uint32_t indexCount) override;
			void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

			void UpdateConstantBuffer(IConstantBuffer& buf, const void* data) override;

		private:
			ID3D12GraphicsCommandList* cmdList_;

			// VSSetShader / PSSetShader と最初の Draw の間で保持する。
			// 描画時にオンデマンドで PSO を検索または生成するために使う。
			IShader* pendingVS_ = nullptr;
			IShader* pendingPS_ = nullptr;
		};
	}
}
