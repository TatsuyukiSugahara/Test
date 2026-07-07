#pragma once
#include "D3D12Common.h"
#include "Graphics/IRenderContextImpl.h"


namespace aq
{
	namespace graphics
	{
		class D3D12GraphicsDeviceImpl;
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
			explicit D3D12RenderContextImpl(D3D12GraphicsDeviceImpl* device);
			~D3D12RenderContextImpl() override = default;

			void OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget) override;
			void OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets) override;
			void OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT) override;
			void OMSetDepthMode(DepthMode mode) override;
			void OMSetBlendMode(BlendMode mode) override;
			void ClearDepthBuffer() override;
			void RSSetViewport(float topLeftX, float topLeftY, float width, float height) override;
			void RSSetScissorEnabled(bool enabled) override;
			void RSSetScissorRect(int x, int y, int w, int h) override;
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
			void CSUnsetShader() override {}
			void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void CSSetSampler(uint32_t startSlot, ISamplerState& /*samplerState*/) override {}
			void CSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv) override;
			void CSUnsetShaderResource(uint32_t slot) override;
			void CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& uav) override;
			void CSUnsetUnorderedAccessView(uint32_t slot) override;

			void OMSetDepthOnlyTarget(IDepthMap& depthMap) override;
			void ClearDepthMap(IDepthMap& depthMap) override;
			void OMSetDepthOnlyTargetSlice(IDepthMap& depthMap, uint32_t slice) override;
			void ClearDepthMapSlice(IDepthMap& depthMap, uint32_t slice) override;
			// 深度のみパス用に PS をクリアする (no-op だと古い PS が残り VS/PS リンケージ不一致になる)
			void PSUnsetShader() override { pendingPS_ = nullptr; }

			void Draw(uint32_t vertexCount, uint32_t startVertexLocation) override;
			void DrawIndexed(uint32_t indexCount) override;
			void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation) override;
			void IASetVertexBufferSlot(uint32_t slot, IVertexBuffer& vertexBuffer) override;
			void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
			                          uint32_t startIndexLocation, int32_t baseVertexLocation,
			                          uint32_t startInstanceLocation) override;
			void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;
			void IASetIndexBufferGpu(IGpuBuffer& indexBuffer) override;
			void DrawIndexedIndirect(IGpuBuffer& argsBuffer) override;
			void UavBarrier(IGpuBuffer& buffer) override;

			void UpdateConstantBuffer(IConstantBuffer& buf, const void* data) override;

		private:
			// Dispatch 後など、グラフィクスルートシグネチャが失われていれば張り直す。
			void EnsureGraphicsRootSig();
			// 描画時に PSO を解決してパイプラインを確定する
			void FlushPipeline();
			// SRV バインドに変更があれば ring からテーブルを確保しコピー・バインドする
			void FlushDescriptors();
			// 現在バインド中の RTV/DSV を OMSetRenderTargets でコマンドリストへ設定する
			void ApplyRenderTargets();

		private:
			D3D12GraphicsDeviceImpl* device_ = nullptr;

			static constexpr uint32_t MAX_RTV = 8;

			// 保留ステート (DX11 のイミディエイト設定を DX12 の PSO/記録に変換するため溜める)
			IShader*          pendingVS_ = nullptr;
			IShader*          pendingPS_ = nullptr;
			BlendMode         blend_     = BlendMode::Opaque;
			DepthMode         depth_     = DepthMode::ReadWrite;
			PrimitiveTopology topology_  = PrimitiveTopology::TriangleList;

			// 現在バインド中の RT/DSV 追跡 (PSO キーのフォーマット + Clear* の対象)
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[MAX_RTV] = {};
			uint32_t                    rtvCount_  = 0;
			DXGI_FORMAT                 rtFormats_[MAX_RTV] = {};
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_ = {};
			bool                        hasDSV_    = false;
			DXGI_FORMAT                 dsFormat_  = DXGI_FORMAT_UNKNOWN;

			// 保留 SRV テーブル (Phase 2)。各スロットのソース CPU ハンドル (staging ヒープ上)。
			// ptr==0 は未バインド = null SRV で埋める。Draw 時に dirty なら ring へコピー。
			static constexpr uint32_t SRV_SLOT_COUNT = 12;  // = D3D12RootSignature::SRV_TABLE_SIZE
			D3D12_CPU_DESCRIPTOR_HANDLE pendingSRV_[SRV_SLOT_COUNT] = {};
			bool                        srvDirty_     = false;
			uint64_t                    lastFrameGen_ = 0;  // SRV テーブルを張り直したフレーム世代
			bool                        graphicsRootDirty_ = false;  // Dispatch 後に graphics root sig 張り直しが必要

			// 保留コンピュート状態 (Phase 4: ブルーム)。Dispatch 時に確定する。
			static constexpr uint32_t CS_SRV_COUNT = 3;  // t0..t2 (t2 = クラスタカリング Hi-Z)
			static constexpr uint32_t CS_UAV_COUNT = 2;  // u0..u1
			IShader*                    pendingCS_   = nullptr;
			D3D12_GPU_VIRTUAL_ADDRESS   csCBAddr_    = 0;
			D3D12_CPU_DESCRIPTOR_HANDLE csSRV_[CS_SRV_COUNT] = {};
			D3D12_CPU_DESCRIPTOR_HANDLE csUAV_[CS_UAV_COUNT] = {};
		};
	}
}
