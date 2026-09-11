#include "aq.h"
// Metal の RenderContext Implementor。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalRenderContextImpl.h"
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
		{
			// P0 では device_ を保持するだけで、メソッドは呼ばない
			// (MetalGraphicsDeviceImpl は別担当で API が未確定のため)。
		}


		/**
		 * レンダーターゲット / クリア
		 */
		void MetalRenderContextImpl::OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget)
		{
			// TODO(P1): エンコーダを閉じ、MTLRenderPassDescriptor の colorAttachments[0] を組み直す(設計書 §3.1)。
		}


		void MetalRenderContextImpl::OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets)
		{
			// TODO(P4): 同上を MRT (SV_Target0..7) へ広げる。
		}


		void MetalRenderContextImpl::OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT)
		{
			// TODO(P4): color 1 本 + depthAttachment に相手 RT の深度テクスチャを差す。
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


		void MetalRenderContextImpl::ClearRenderTargetView(uint32_t index, float* clearColor)
		{
			// TODO(P1): エンコーダを閉じ、次に開くパスの loadAction = Clear + clearColor として予約する(設計書 §3.1)。
		}


		void MetalRenderContextImpl::ClearDepthBuffer()
		{
			// TODO(P3): 次に開くパスの depthAttachment.loadAction = Clear として予約する。
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
			// TODO(P4): color 0 本 + depthAttachment のみのパスを組む(設計書 §3.3)。
		}


		void MetalRenderContextImpl::ClearDepthMap(IDepthMap& depthMap)
		{
			// TODO(P4): 次に開く深度パスの loadAction = Clear として予約する。
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
