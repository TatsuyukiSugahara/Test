#include "aq.h"
// Metal の RenderContext Implementor。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalRenderContextImpl.h"
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"
#include "Graphics/Metal/MetalRenderTarget.h"
#include "Graphics/Metal/MetalBuffers.h"
#include "Graphics/Metal/MetalShader.h"

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalRenderContextImpl.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		MetalRenderContextImpl::MetalRenderContextImpl(MetalGraphicsDeviceImpl* device)
			: device_(device)
			, pending_()
			, colorRTs_()
			, colorRTCount_(0)
			, depthRT_(nullptr)
			, encoder_(nil)
			, clearColorMask_(0)
			, clearColors_()
			, clearDepthPending_(false)
		{
		}


		MetalRenderContextImpl::~MetalRenderContextImpl()
		{
			// エンコーダを開いたまま壊れるとコマンドバッファが不正になる。必ず閉じてから解放する。
			EndEncodingIfActive();
		}


		/**
		 * エンコーダ / 保留クリア
		 */
		void MetalRenderContextImpl::EndEncodingIfActive()
		{
			if (encoder_ == nil) {
				return;
			}

			[encoder_ endEncoding];
			[encoder_ release];
			encoder_ = nil;
		}


		MTLRenderPassDescriptor* MetalRenderContextImpl::BuildRenderPassDescriptor()
		{
			if (colorRTCount_ == 0) {
				// TODO(P4): OMSetDepthOnlyTarget のシャドウパスは color 0 本 + depth だけになる。
				return nil;
			}

			MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];  // autorelease
			for (uint32_t i = 0; i < colorRTCount_; ++i)
			{
				// プロキシ RT は drawable を取れていないと実体が nil になる。
				// 中途半端なパスを開くと Metal が検証エラーを出すので、まるごと諦める。
				id<MTLTexture> texture = (colorRTs_[i] != nullptr) ? colorRTs_[i]->GetTexture() : nil;
				if (texture == nil) {
					return nil;
				}

				MTLRenderPassColorAttachmentDescriptor* attachment = descriptor.colorAttachments[i];
				attachment.texture     = texture;
				attachment.storeAction = MTLStoreActionStore;
				if ((clearColorMask_ & (1u << i)) != 0) {
					attachment.loadAction = MTLLoadActionClear;
					attachment.clearColor = MTLClearColorMake(clearColors_[i][0], clearColors_[i][1],
					                                          clearColors_[i][2], clearColors_[i][3]);
				} else {
					// 予約が無ければ既存内容を残す。DontCare にすると前のパスの結果が消える。
					attachment.loadAction = MTLLoadActionLoad;
				}
			}

			// 深度は自前深度を持つ RT か、OMSetRenderTargetWithDepth で渡された相手から取る。
			// ポストプロセスの RT のように深度を持たない構成もある。
			if (depthRT_ != nullptr && depthRT_->HasDepth())
			{
				MTLRenderPassDepthAttachmentDescriptor* attachment = descriptor.depthAttachment;
				attachment.texture     = depthRT_->GetDepthTexture();
				attachment.storeAction = MTLStoreActionStore;
				attachment.loadAction  = clearDepthPending_ ? MTLLoadActionClear : MTLLoadActionLoad;
				attachment.clearDepth  = DEPTH_CLEAR_VALUE;
			}

			// このパスが実際にクリアを行うので、予約はここで消費する。
			clearColorMask_    = 0;
			clearDepthPending_ = false;
			return descriptor;
		}


		void MetalRenderContextImpl::FlushPendingClears()
		{
			// 描画があるときは Draw がエンコーダを開くついでに loadAction = Clear へ畳むので、
			// ここへ来た時点で予約は空になっている(= この空エンコーダは出ない)。
			// P1 は描画が 1 本も無く、予約が残ったままフレームが終わるため、ここで実際に塗る。
			if (clearColorMask_ == 0 && !clearDepthPending_) {
				return;
			}

			// 予約が残るのはエンコーダが開かれなかったときだけだが(Clear* は必ず閉じる)、念のため。
			EndEncodingIfActive();

			if (device_ == nullptr) {
				return;
			}

			// クリアもコマンドバッファが要る。まだフレームが始まっていなければここで始める(設計書 §2.3)。
			device_->BeginFrameIfNeeded();
			id<MTLCommandBuffer> commandBuffer =
				static_cast<id<MTLCommandBuffer>>(device_->GetCurrentCommandBufferHandle());
			if (commandBuffer == nil) {
				// drawable が取れずフレームを捨てた。予約は次のフレームへ持ち越す。
				return;
			}

			@autoreleasepool
			{
				MTLRenderPassDescriptor* descriptor = BuildRenderPassDescriptor();
				if (descriptor == nil) {
					return;
				}

				// 空のエンコーダ。実際に塗るのは loadAction = Clear なので描画コマンドは要らない。
				id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
				[encoder endEncoding];
			}
		}


		void MetalRenderContextImpl::SetAttachments(MetalRenderTarget* const* colorTargets,
		                                            const uint32_t            count,
		                                            MetalRenderTarget*        depthTarget)
		{
			const uint32_t colorCount = (count < MAX_MRT) ? count : MAX_MRT;

			// 構成が同一ならエンコーダを開いたまま維持する(設計書 §3.1)。
			bool sameConfig = (colorCount == colorRTCount_) && (depthTarget == depthRT_);
			for (uint32_t i = 0; sameConfig && i < colorCount; ++i) {
				sameConfig = (colorTargets[i] == colorRTs_[i]);
			}
			if (sameConfig) {
				return;
			}

			// 予約中のクリアは「今の構成」宛て。差し替える前にここで確定させる。
			// エンジンの Clear は D3D11 由来で即時実行の意味なので、描画が 1 本も無いまま
			// RT を切り替えても、クリアだけは効いていなければならない。
			// 描画があるときは Draw がエンコーダを開くついでに畳むので、ここで空パスは出ない。
			FlushPendingClears();
			EndEncodingIfActive();

			for (uint32_t i = 0; i < MAX_MRT; ++i) {
				colorRTs_[i] = (i < colorCount) ? colorTargets[i] : nullptr;
			}
			colorRTCount_ = colorCount;
			depthRT_      = depthTarget;

			// PSO キーにも反映する(設計書 §4.1)。P2 の Draw 時 flush がここを見る。
			for (uint32_t i = 0; i < MAX_MRT; ++i) {
				pending_.colorFormat[i] = (colorRTs_[i] != nullptr) ? colorRTs_[i]->GetPixelFormat()
				                                                   : MTLPixelFormatInvalid;
			}
			pending_.colorCount    = colorCount;
			pending_.depthFormat   = (depthRT_ != nullptr) ? depthRT_->GetDepthPixelFormat() : MTLPixelFormatInvalid;
			pending_.pipelineDirty = true;
		}


		/**
		 * レンダーターゲット / クリア
		 */
		void MetalRenderContextImpl::OMSetRenderTargets(const uint32_t numViews, IRenderTarget* renderTarget)
		{
			MetalRenderTarget* target = (numViews > 0) ? static_cast<MetalRenderTarget*>(renderTarget) : nullptr;

			// 自前深度を持つ RT ならそれを深度ソースにする(ポストプロセスの RT は深度なし)。
			MetalRenderTarget* depthTarget = (target != nullptr && target->HasDepth()) ? target : nullptr;
			SetAttachments(&target, (target != nullptr) ? 1u : 0u, depthTarget);
		}


		void MetalRenderContextImpl::OMSetMRTRenderTargets(const uint32_t numViews, IRenderTarget* const* renderTargets)
		{
			// TODO(P4): MRT 描画そのもの (SV_Target0..7) は P4。ここで構成だけ憶えておかないと、
			//           GBuffer 宛てのクリア予約が直前の RT へ紛れ込む。
			if (renderTargets == nullptr) {
				SetAttachments(nullptr, 0u, nullptr);
				return;
			}

			MetalRenderTarget* targets[MAX_MRT] = {};
			MetalRenderTarget* depthTarget      = nullptr;
			const uint32_t     count            = (numViews < MAX_MRT) ? numViews : MAX_MRT;
			for (uint32_t i = 0; i < count; ++i)
			{
				targets[i] = static_cast<MetalRenderTarget*>(renderTargets[i]);
				if (depthTarget == nullptr && targets[i] != nullptr && targets[i]->HasDepth()) {
					depthTarget = targets[i];
				}
			}
			SetAttachments(targets, count, depthTarget);
		}


		void MetalRenderContextImpl::OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT)
		{
			// TODO(P4): フォワードパスの本実装は P4。P1 は構成の記録だけ。
			MetalRenderTarget* target = static_cast<MetalRenderTarget*>(&colorRT);
			SetAttachments(&target, 1u, static_cast<MetalRenderTarget*>(&depthSourceRT));
		}


		void MetalRenderContextImpl::OMSetDepthMode(const DepthMode mode)
		{
			// MTLDepthStencilState は PSO と別オブジェクトなので、PSO キーには入れない(設計書 §4.3)。
			pending_.depth = mode;
		}


		void MetalRenderContextImpl::OMSetBlendMode(const BlendMode mode)
		{
			if (pending_.blend != mode) {
				pending_.blend         = mode;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::RSSetViewport(const float topLeftX, const float topLeftY,
		                                           const float width, const float height)
		{
			// Metal の NDC は D3D と同じ (Y 下向き / Z が [0,1]) なので Y-flip は要らない(設計書 §2.4)。
			pending_.viewport.originX = topLeftX;
			pending_.viewport.originY = topLeftY;
			pending_.viewport.width   = width;
			pending_.viewport.height  = height;
			pending_.viewport.znear   = 0.0;
			pending_.viewport.zfar    = 1.0;
		}


		void MetalRenderContextImpl::RSSetScissorEnabled(const bool enabled)
		{
			// Metal に「シザー無効」は無いため、無効時は RT 全面を指定することになる(設計書 §3.3)。
			pending_.scissorEnabled = enabled;
		}


		void MetalRenderContextImpl::RSSetScissorRect(const int x, const int y, const int w, const int h)
		{
			// MTLScissorRect は符号無しで、RT の外へはみ出すと Metal が落とすため負値を丸める。
			const int clampedX = (x > 0) ? x : 0;
			const int clampedY = (y > 0) ? y : 0;
			const int clampedW = (w > 0) ? w : 0;
			const int clampedH = (h > 0) ? h : 0;
			pending_.scissor.x      = static_cast<NSUInteger>(clampedX);
			pending_.scissor.y      = static_cast<NSUInteger>(clampedY);
			pending_.scissor.width  = static_cast<NSUInteger>(clampedW);
			pending_.scissor.height = static_cast<NSUInteger>(clampedH);
		}


		void MetalRenderContextImpl::ClearRenderTargetView(const uint32_t index, float* clearColor)
		{
			// Metal には「エンコーダ内で RT をクリアする」コマンドが無い。即実行はせず、
			// 次にこのアタッチメントでエンコーダを開くときの loadAction = Clear へ予約する(設計書 §3.1)。
			if (index >= colorRTCount_ || index >= MAX_MRT) {
				return;
			}

			// 開いているパスの loadAction はもう変えられないので、いったん閉じて開き直させる。
			EndEncodingIfActive();

			if (clearColor != nullptr) {
				for (uint32_t i = 0; i < 4; ++i) {
					clearColors_[index][i] = clearColor[i];
				}
			}
			clearColorMask_ |= (1u << index);
		}


		void MetalRenderContextImpl::ClearDepthBuffer()
		{
			// 深度を持たない構成(ポストプロセスの RT 等)では予約しない。
			// 予約したまま誰にも消費されないと、後続の別パスが巻き添えでクリアされる。
			if (depthRT_ == nullptr || !depthRT_->HasDepth()) {
				return;
			}

			// カラーと同じく、次に開くパスの depthAttachment.loadAction = Clear として予約する。
			EndEncodingIfActive();
			clearDepthPending_ = true;
		}


		/**
		 * 入力アセンブラ
		 */
		void MetalRenderContextImpl::IASetVertexBuffer(IVertexBuffer& vertexBuffer)
		{
			// TODO(P2): setVertexBuffer:offset:atIndex:metal::VERTEX_BUFFER_INDEX へ流す。
		}


		void MetalRenderContextImpl::IASetIndexBuffer(IIndexBuffer& indexBuffer)
		{
			// TODO(P2): 保留して drawIndexedPrimitives: の引数に渡す。
		}


		void MetalRenderContextImpl::IASetPrimitiveTopology(const PrimitiveTopology topology)
		{
			// PSO 側は class (point/line/triangle) 単位なので、変化時は PSO も引き直す(設計書 §3.3)。
			if (pending_.topology != topology) {
				pending_.topology      = topology;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::IASetInputLayout(IShader& vsShader)
		{
			// TODO(P2): VS のリフレクション結果から MTLVertexDescriptor を組む(設計書 §9.3)。
		}


		/**
		 * シェーダ / 定数バッファ / リソース
		 */
		void MetalRenderContextImpl::VSSetShader(IShader& shader)
		{
			MetalShader* vs = static_cast<MetalShader*>(&shader);
			if (pending_.vs != vs) {
				pending_.vs            = vs;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			// TODO(P2): setVertexBuffer:offset:atIndex:startSlot へ流す(設計書 §5.1)。
		}


		void MetalRenderContextImpl::PSSetShader(IShader& shader)
		{
			MetalShader* ps = static_cast<MetalShader*>(&shader);
			if (pending_.ps != ps) {
				pending_.ps            = ps;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::PSUnsetShader()
		{
			if (pending_.ps != nullptr) {
				pending_.ps            = nullptr;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			// TODO(P2): setFragmentBuffer:offset:atIndex:startSlot へ流す(設計書 §5.1)。
		}


		void MetalRenderContextImpl::PSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView)
		{
			// TODO(P3): setFragmentTexture:atIndex:startSlot へ流す。
		}


		void MetalRenderContextImpl::PSUnsetShaderResource(uint32_t slot)
		{
			// TODO(P3): 該当スロットの保留を落とす。
		}


		void MetalRenderContextImpl::PSSetSampler(uint32_t startSlot, ISamplerState& samplerState)
		{
			// TODO(P3): setFragmentSamplerState:atIndex:startSlot へ流す。
		}


		/**
		 * compute
		 */
		void MetalRenderContextImpl::CSSetShader(IShader& shader)
		{
			// TODO(P5): compute PSO を保留する。
		}


		void MetalRenderContextImpl::CSUnsetShader()
		{
			// TODO(P5): compute の保留を落とす。
		}


		void MetalRenderContextImpl::CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			// TODO(P5): MTLComputeCommandEncoder の setBuffer:offset:atIndex: へ流す。
		}


		void MetalRenderContextImpl::CSSetSampler(uint32_t startSlot, ISamplerState& samplerState)
		{
			// TODO(P5): MTLComputeCommandEncoder の setSamplerState:atIndex: へ流す。
		}


		void MetalRenderContextImpl::CSSetShaderResource(uint32_t startSlot, IShaderResourceView& shaderResourceView)
		{
			// TODO(P5): MTLComputeCommandEncoder の setTexture:atIndex: へ流す。
		}


		void MetalRenderContextImpl::CSUnsetShaderResource(uint32_t slot)
		{
			// TODO(P5): 該当スロットの保留を落とす。
		}


		void MetalRenderContextImpl::CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& unorderedAccessView)
		{
			// TODO(P5): テクスチャ UAV は setTexture、バッファ UAV は setBuffer(index は metal::UAV_INDEX_SHIFT + slot)。
		}


		void MetalRenderContextImpl::CSUnsetUnorderedAccessView(uint32_t slot)
		{
			// TODO(P5): 該当スロットの保留を落とす。
		}


		/**
		 * 描画 / ディスパッチ
		 */
		void MetalRenderContextImpl::Draw(uint32_t vertexCount, uint32_t startVertexLocation)
		{
			// TODO(P2): 保留ステートを flush して drawPrimitives:vertexStart:vertexCount:。
		}


		void MetalRenderContextImpl::DrawIndexed(uint32_t indexCount)
		{
			// TODO(P2): 保留ステートを flush して drawIndexedPrimitives:。
		}


		void MetalRenderContextImpl::DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation)
		{
			// TODO(P2): 同上 (indexBufferOffset に startIndexLocation * 要素サイズを足す)。
		}


		void MetalRenderContextImpl::Dispatch(uint32_t x, uint32_t y, uint32_t z)
		{
			// TODO(P5): エンコーダを compute へ切り替えて dispatchThreadgroups:threadsPerThreadgroup:。
		}


		void MetalRenderContextImpl::UpdateConstantBuffer(IConstantBuffer& buf, const void* data)
		{
			// ここだけは P0 から実際に動かす。エンジンが毎フレーム呼ぶため、
			// 空にしておくと後のフェーズで原因の分かりにくい不具合になる。
			if (!data) {
				return;
			}
			static_cast<MetalConstantBuffer&>(buf).Update(data);
		}


		/**
		 * シャドウ深度パス
		 */
		void MetalRenderContextImpl::OMSetDepthOnlyTarget(IDepthMap& depthMap)
		{
			// TODO(P4): color 0 本 + depthAttachment に MetalDepthMap のスライスを差す(設計書 §3.3)。
			//           P1 は構成を空にするだけ。こうしないと、シャドウパスに入る直前の RT 宛ての
			//           クリア予約がパスを跨いで生き残る。
			SetAttachments(nullptr, 0u, nullptr);
		}


		void MetalRenderContextImpl::ClearDepthMap(IDepthMap& depthMap)
		{
			// TODO(P4): 次に開く深度パスの loadAction = Clear として予約する。
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
