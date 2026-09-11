#pragma once
// Metal の RenderContext Implementor(設計書/MetalBackend設計.md §3)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IRenderContextImpl.h"
#include <memory>


namespace aq
{
	namespace graphics
	{
		class MetalGraphicsDeviceImpl;
		class MetalRenderTarget;
		class MetalShader;
		class MetalPipelineCache;
		class MetalDepthStencilCache;


		/**
		 * Draw 時に flush する保留ステート (設計書 §3.2)
		 *
		 * Metal は 1 レンダーパス = 1 MTLRenderCommandEncoder で、エンコーダを開いた後に
		 * レンダーターゲットを差し替えられない。一方エンジンの API は D3D11 由来の
		 * イミディエイト風なので、いったんここへ溜めて Draw で確定させる(D3D12 / Vulkan と同型)。
		 *
		 * 実際にエンコーダへ流すのは MetalRenderContextImpl::FlushGraphicsState()。
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

			/**
			 * 差分フラグ。
			 *
			 * P2 の flush は**毎 Draw 束ね直している**(Metal のエンコーダはステートを
			 * 引き継がず、RT 切り替えで開き直すたびに全部やり直しになるため、
			 * 差分判定のほうが壊れやすい)。フラグは flush で倒すだけで、
			 * 束ね直しの間引きに使うのは計測してからにする。
			 */
			bool pipelineDirty = true;
			bool bindingDirty  = true;
		};




		/**
		 * Metal RenderContext Implementor — P2(描画パスとバインド)
		 *
		 * P1 のアタッチメント保持とクリア予約の上に、**Draw 時 flush** を載せた段階。
		 * Draw* が来た時点で「エンコーダを開く → PSO / 深度ステートを引く →
		 * ビューポート・シザー → 頂点 / 定数バッファ・テクスチャ・サンプラを束ねる →
		 * 描画コマンド」を行う(設計書 §3.2)。
		 * 深度のみパス(シャドウ)は P3 / P4、compute(Dispatch)は P5。
		 *
		 * **エンコーダの寿命** (設計書 §3.1): Metal は 1 レンダーパス = 1 エンコーダで、
		 * 開いた後にレンダーターゲットを差し替えられない。アタッチメントが変わる操作
		 * (OMSet* / Clear* / Dispatch)が来たらエンコーダを閉じ、次の描画で開き直す。
		 *
		 * **アタッチメントを差し替えるときは予約クリアを先に確定させる**(設計書への追加)。
		 * エンジンの Clear は D3D11 由来の即時実行の意味なので、描画が 1 本も無いまま
		 * RT を切り替えても「クリアだけは効いた」状態でなければならない。予約を持ち越すと
		 * 直前の RT 向けのクリア色が次の RT へ紛れ込む。
		 */
		class MetalRenderContextImpl : public IRenderContextImpl
		{
		private:
			/** 保留クリアのビットマスク幅。colorAttachments の本数と同じ */
			static constexpr uint32_t MAX_MRT = PendingGraphicsState::MAX_MRT;

			/** 深度のクリア値。D3D11 / Vulkan 側と揃える(遠クリップ = 1.0) */
			static constexpr float DEPTH_CLEAR_VALUE = 1.0f;

			/**
			 * フォールバック込みで毎 Draw 束ねる SRV / サンプラのスロット数。
			 *
			 * 全 59 本の実使用は t <= 11 / s <= 1(設計書 §5.1 の実測)。シェーダが宣言した
			 * 引数が nil だと Metal API Validation がエラーにするので、**未バインドのスロットも
			 * 白テクスチャ / 既定サンプラで埋める**(Vulkan 版が全スロットへ write するのと同じ)。
			 */
			static constexpr uint32_t BIND_SRV_COUNT     = 12;  // t0..t11
			static constexpr uint32_t BIND_SAMPLER_COUNT = 2;   // s0..s1


		private:
			/** 所属デバイス。エンコーダ / drawable / コマンドバッファはここから取る */
			MetalGraphicsDeviceImpl* device_;

			/** 保留ステート (設計書 §3.2) */
			PendingGraphicsState pending_;

			/** 現在のアタッチメント構成。エンコーダを開くときの MTLRenderPassDescriptor の素 */
			MetalRenderTarget* colorRTs_[MAX_MRT];
			uint32_t           colorRTCount_;

			/** 深度の供給元。自前深度を持つ RT か、OMSetRenderTargetWithDepth で指定された相手 */
			MetalRenderTarget* depthRT_;

			/**
			 * 開いているレンダーコマンドエンコーダ。無ければ nil。
			 * MRR だが commandBuffer から得るエンコーダは autorelease なので retain して保持する。
			 */
			id<MTLRenderCommandEncoder> encoder_;

			/**
			 * クリアの予約 (設計書 §3.1)。
			 *
			 * Metal には「エンコーダ内で RT をクリアする」コマンドが無いため、Clear* は即実行せず、
			 * 次にそのアタッチメントでエンコーダを開くときの loadAction = Clear + clearColor として
			 * ここへ憶える。実際に消費するのは BuildRenderPassDescriptor()。
			 */
			uint32_t clearColorMask_;
			float    clearColors_[MAX_MRT][4];
			bool     clearDepthPending_;

			/**
			 * PSO と深度ステートのキャッシュ(設計書 §4)。
			 * デバイスではなく**本クラスが所有する**。Draw 時 flush からしか引かないため。
			 */
			std::unique_ptr<MetalPipelineCache>     pipelineCache_;
			std::unique_ptr<MetalDepthStencilCache> depthStencilCache_;

			/**
			 * 未バインドスロット用のフォールバック。
			 *
			 * Metal API Validation は「シェーダが宣言している引数が nil」をエラーにする。
			 * UI シェーダはテクスチャとサンプラを必ず使うので、バインドされていない
			 * スロットへは 1x1 の白テクスチャと既定サンプラを束ねる。
			 */
			id<MTLTexture>      fallbackTexture_;
			id<MTLSamplerState> fallbackSampler_;

			/** フォールバックを使ったスロットを 1 回だけログするためのビットマスク */
			uint32_t fallbackTextureLoggedMask_;
			uint32_t fallbackSamplerLoggedMask_;


		public:
			explicit MetalRenderContextImpl(MetalGraphicsDeviceImpl* device);
			~MetalRenderContextImpl() override;

			/** 所属デバイス */
			inline MetalGraphicsDeviceImpl* GetDevice() const { return device_; }


			/**
			 * エンコーダ / 保留クリア(MetalGraphicsDeviceImpl から呼ぶ)
			 */
		public:
			/**
			 * 予約されたままのクリアを実際に画面へ反映する。
			 *
			 * 描画があるときは、Draw がエンコーダを開くついでに loadAction = Clear へ畳まれるので
			 * **この空エンコーダは出ない**(予約が既に消費済みで、ここは即 return する)。
			 * 描画が 1 本も無いまま CopyToBackBuffer / Present に来たときだけ、
			 * 空のエンコーダ(loadAction = Clear → すぐ endEncoding)を 1 本だけ発行する。
			 */
			void FlushPendingClears();

			/** 開いているエンコーダがあれば閉じる。アタッチメントが変わる操作と Present の前に呼ぶ */
			void EndEncodingIfActive();


		private:
			/**
			 * 現在のアタッチメント構成から MTLRenderPassDescriptor を組む。
			 * **保留クリアの予約はここで消費する**(このパスが実際にクリアを行うため)。
			 * Draw 時 flush も同じものを使う。
			 * @return 組めなければ nil(アタッチメントが 1 枚も無い等)
			 */
			MTLRenderPassDescriptor* BuildRenderPassDescriptor();

			/**
			 * エンコーダが開いていなければ開く。
			 * 予約クリアの消費は BuildRenderPassDescriptor() が一手に行うので、ここでは触らない。
			 */
			void OpenEncoderIfNeeded();

			/**
			 * ビューポート / シザー / 深度ステートをエンコーダへ流す。
			 *
			 * **Metal のエンコーダはステートを引き継がない**ため、開き直すたびに要る。
			 * 呼び分けを間違えると「RT を切り替えた後だけ描画が消える」形で出るので、
			 * 判定を省いて Draw ごとに流している(いずれも安価な setter)。
			 */
			void ApplyEncoderStates();

			/**
			 * Draw 直前に保留ステートを確定させる(設計書 §3.2)。
			 * @return 描画できる状態なら true。false なら Draw を捨てる
			 */
			bool FlushGraphicsState();

			/**
			 * DrawIndexed 系の共通処理。
			 * @param indexCount            描画するインデックス数
			 * @param startIndexLocation    開始インデックス(バイトではなく要素数)
			 * @param instanceCount         インスタンス数(通常描画は 1)
			 * @param baseVertexLocation    頂点のベースオフセット
			 * @param startInstanceLocation インスタンスのベースオフセット
			 */
			void DrawIndexedInternal(const uint32_t indexCount,
			                         const uint32_t startIndexLocation,
			                         const uint32_t instanceCount,
			                         const int32_t  baseVertexLocation,
			                         const uint32_t startInstanceLocation);

			/** 1x1 の白テクスチャと既定サンプラを作る(コンストラクタから 1 回だけ) */
			void CreateFallbackResources(id<MTLDevice> device);

			/**
			 * アタッチメント構成を差し替える。OMSet* 系の共通処理。
			 * 構成が同一なら何もしない(エンコーダを開いたまま維持する。設計書 §3.1)。
			 * @param colorTargets カラーの配列 (count が 0 なら参照しない)
			 * @param count        カラーの本数
			 * @param depthTarget  深度の供給元。無ければ nullptr
			 */
			void SetAttachments(MetalRenderTarget* const* colorTargets, const uint32_t count, MetalRenderTarget* depthTarget);


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
			void IASetVertexBufferSlot(uint32_t slot, IVertexBuffer& vertexBuffer) override;
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
			void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
			                          uint32_t startIndexLocation, int32_t baseVertexLocation,
			                          uint32_t startInstanceLocation) override;
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
