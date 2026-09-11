#pragma once
// Metal の RenderContext Implementor(設計書/MetalBackend設計.md §3)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IRenderContextImpl.h"


namespace aq
{
	namespace graphics
	{
		class MetalGraphicsDeviceImpl;
		class MetalShader;


		/**
		 * Draw 時に flush する保留ステート (設計書 §3.2)
		 *
		 * Metal は 1 レンダーパス = 1 MTLRenderCommandEncoder で、エンコーダを開いた後に
		 * レンダーターゲットを差し替えられない。一方エンジンの API は D3D11 由来の
		 * イミディエイト風なので、いったんここへ溜めて Draw で確定させる(D3D12 / Vulkan と同型)。
		 *
		 * P0 では「代入するだけ」で、エンコーダへは何も流さない。
		 */
		struct PendingGraphicsState
		{
			// 配列長。実レジスタ使用は b <= 4 / t <= 11 / s <= 1 だが、
			// 設計書 §3.2 の記載に合わせて 16 本ずつ確保しておく。
			// (配列の要素数に使うため、例外的にクラス先頭へ置く)
			static constexpr uint32_t MAX_MRT            = 8;   // SV_Target0..7
			static constexpr uint32_t MAX_CONSTANT_COUNT = 16;  // b0..b15
			static constexpr uint32_t MAX_SRV_COUNT      = 16;  // t0..t15
			static constexpr uint32_t MAX_SAMPLER_COUNT  = 16;  // s0..s15
			static constexpr uint32_t MAX_VERTEX_STREAM  = 2;   // per-vertex / per-instance

			/** PSO キーになるもの (設計書 §4.1) */
			MetalShader*      vs             = nullptr;
			MetalShader*      ps             = nullptr;
			PrimitiveTopology topology       = PrimitiveTopology::TriangleList;
			BlendMode         blend          = BlendMode::Opaque;
			DepthMode         depth          = DepthMode::ReadWrite;
			MTLPixelFormat    colorFormat[MAX_MRT] = {};
			uint32_t          colorCount     = 0;
			MTLPixelFormat    depthFormat    = MTLPixelFormatInvalid;
			uint32_t          vertexStride   = 0;
			uint32_t          instanceStride = 0;

			/** エンコーダへ直接流すもの */
			MTLViewport    viewport       = {};
			MTLScissorRect scissor        = {};
			bool           scissorEnabled = false;

			IVertexBuffer*       vb[MAX_VERTEX_STREAM]      = {};
			IIndexBuffer*        ib                         = nullptr;
			IConstantBuffer*     vsCB[MAX_CONSTANT_COUNT]   = {};
			IConstantBuffer*     psCB[MAX_CONSTANT_COUNT]   = {};
			IShaderResourceView* psSRV[MAX_SRV_COUNT]       = {};
			ISamplerState*       psSampler[MAX_SAMPLER_COUNT] = {};

			/** 差分フラグ (P2 で PSO / バインドの再構築判定に使う) */
			bool pipelineDirty = true;
			bool bindingDirty  = true;
		};




		/**
		 * Metal RenderContext Implementor — P0(足場)
		 *
		 * P0 は **描画を一切しない**。IRenderContextImpl の純粋仮想 37 本をすべて
		 * override して空実装で埋め、AquaDash が起動して終了コード 0 で閉じられる
		 * ことだけを担保する(設計書 §12 の P0)。
		 * 実装は各メソッドの TODO に書いたフェーズで順に入れていく。
		 */
		class MetalRenderContextImpl : public IRenderContextImpl
		{
		private:
			/** 所属デバイス。P0 では保持するだけで呼び出さない */
			MetalGraphicsDeviceImpl* device_;

			/** 保留ステート (設計書 §3.2) */
			PendingGraphicsState pending_;


		public:
			explicit MetalRenderContextImpl(MetalGraphicsDeviceImpl* device);

			/** 所属デバイス (P1 以降でエンコーダ / drawable を取りに行くのに使う) */
			inline MetalGraphicsDeviceImpl* GetDevice() const { return device_; }


			/**
			 * レンダーターゲット / クリア
			 */
		public:
			void OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget) override;
			void OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets) override;
			void OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT) override;
			void OMSetDepthMode(DepthMode mode) override;
			void OMSetBlendMode(BlendMode mode) override;
			void RSSetViewport(float topLeftX, float topLeftY, float width, float height) override;
			void RSSetScissorEnabled(bool enabled) override;
			void RSSetScissorRect(int x, int y, int w, int h) override;
			void ClearRenderTargetView(uint32_t index, float* clearColor) override;
			void ClearDepthBuffer() override;


			/**
			 * 入力アセンブラ
			 */
		public:
			void IASetVertexBuffer(IVertexBuffer& vertexBuffer) override;
			void IASetIndexBuffer(IIndexBuffer& indexBuffer) override;
			void IASetPrimitiveTopology(PrimitiveTopology topology) override;
			void IASetInputLayout(IShader& vsShader) override;


			/**
			 * シェーダ / 定数バッファ / リソース
			 */
		public:
			void VSSetShader(IShader& shader) override;
			void VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;

			void PSSetShader(IShader& shader) override;
			void PSUnsetShader() override;
			void PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void PSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView) override;
			void PSUnsetShaderResource(uint32_t slot) override;
			void PSSetSampler(uint32_t startSlot, ISamplerState& samplerState) override;


			/**
			 * compute
			 */
		public:
			void CSSetShader(IShader& shader) override;
			void CSUnsetShader() override;
			void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void CSSetSampler(uint32_t startSlot, ISamplerState& samplerState) override;
			void CSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView) override;
			void CSUnsetShaderResource(uint32_t slot) override;
			void CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& unorderedAccessView) override;
			void CSUnsetUnorderedAccessView(uint32_t slot) override;


			/**
			 * 描画 / ディスパッチ
			 */
		public:
			void Draw(uint32_t vertexCount, uint32_t startVertexLocation) override;
			void DrawIndexed(uint32_t indexCount) override;
			void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation) override;
			void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

			void UpdateConstantBuffer(IConstantBuffer& buf, const void* data) override;


			/**
			 * シャドウ深度パス
			 */
		public:
			void OMSetDepthOnlyTarget(IDepthMap& depthMap) override;
			void ClearDepthMap(IDepthMap& depthMap) override;
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
