#include "aq.h"
#include "BloomPassCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		BloomPassCommand::BloomPassCommand(
			graphics::IShader*                                    extractShader,
			graphics::IShader*                                    dualDownShader,
			graphics::IShader*                                    dualUpShader,
			graphics::IShader*                                    dualUpAccumShader,
			graphics::IShader*                                    compositeShader,
			graphics::ISamplerState*                              sampler,
			graphics::IConstantBuffer*                            bloomCB,
			RenderTargetHandle                                    sceneRT,
			RenderTargetHandle                                    brightRT,
			const RenderTargetHandle (&pyramidRTs)[BloomRenderer::kMaxLevels],
			RenderTargetHandle                                    finalRT,
			float                                                 threshold,
			float                                                 intensity,
			uint32_t                                              blurPasses,
			uint32_t                                              width,
			uint32_t                                              height)
			: extractShader_(extractShader)
			, dualDownShader_(dualDownShader)
			, dualUpShader_(dualUpShader)
			, dualUpAccumShader_(dualUpAccumShader)
			, compositeShader_(compositeShader)
			, sampler_(sampler)
			, bloomCB_(bloomCB)
			, sceneRTHandle_(sceneRT)
			, brightRTHandle_(brightRT)
			, finalRTHandle_(finalRT)
			, threshold_(threshold)
			, intensity_(intensity)
			, blurPasses_(blurPasses)
			, width_(width)
			, height_(height)
		{
			for (uint32_t i = 0; i < BloomRenderer::kMaxLevels; ++i)
				pyramidRTHandles_[i] = pyramidRTs[i];
		}


		void BloomPassCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			auto& device   = graphics::GraphicsDevice::Get();
			auto* sceneRT  = device.GetRenderTarget(sceneRTHandle_);
			auto* brightRT = device.GetRenderTarget(brightRTHandle_);
			auto* finalRT  = device.GetRenderTarget(finalRTHandle_);
			if (!sceneRT || !brightRT || !finalRT) return;

			graphics::IRenderTarget* pyramidRT[BloomRenderer::kMaxLevels] = {};
			for (uint32_t i = 0; i < blurPasses_; ++i)
			{
				pyramidRT[i] = device.GetRenderTarget(pyramidRTHandles_[i]);
				if (!pyramidRT[i]) return;
			}

			// RTV を外してから SRV/UAV として使用する
			ctx.OMSetRenderTargets(0, nullptr);

			BloomCBData cb{};
			cb.threshold = threshold_;
			cb.intensity = intensity_;
			cb.width     = width_;
			cb.height    = height_;
			ctx.UpdateSubresource(*bloomCB_, cb);
			ctx.CSSetConstantBuffer(0, *bloomCB_);
			ctx.CSSetSampler(0, *sampler_);

			// 輝度抽出
			ctx.CSSetShader(*extractShader_);
			ctx.CSSetShaderResource(0, sceneRT->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(0, brightRT->GetRenderTargetUAV());
			ctx.Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetUnorderedAccessView(0);

			// ダウンサンプリング: brightRT → pyramid[0] → ... → pyramid[N-1]
			ctx.CSSetShader(*dualDownShader_);
			for (uint32_t i = 0; i < blurPasses_; ++i)
			{
				graphics::IRenderTarget* src = (i == 0) ? brightRT : pyramidRT[i - 1];
				uint32_t outW = (width_  >> (i + 1)) > 0u ? (width_  >> (i + 1)) : 1u;
				uint32_t outH = (height_ >> (i + 1)) > 0u ? (height_ >> (i + 1)) : 1u;

				ctx.CSSetShaderResource(0, src->GetRenderTargetSRV());
				ctx.CSSetUnorderedAccessView(0, pyramidRT[i]->GetRenderTargetUAV());
				ctx.Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
				ctx.CSUnsetShaderResource(0);
				ctx.CSUnsetUnorderedAccessView(0);
			}

			// アップサンプリング
			// pyramid[N-1..1]: 下位レベルの下サンプル結果に上位レベルのアップサンプルを累積加算する
			// pyramid[0] → brightRT: 全解像度に戻す（純粋上書き）
			for (int32_t i = static_cast<int32_t>(blurPasses_) - 1; i >= 0; --i)
			{
				uint32_t outW, outH;
				graphics::IRenderTarget* dst;

				if (i == 0)
				{
					// 最終段: brightRT に純粋アップサンプル（下サンプル結果は不要）
					dst  = brightRT;
					outW = width_;
					outH = height_;
					ctx.CSSetShader(*dualUpShader_);
				}
				else
				{
					// 中間段: pyramid[i-1] の下サンプル結果に UAV 読み書きで累積
					dst  = pyramidRT[i - 1];
					outW = (width_  >> i) > 0u ? (width_  >> i) : 1u;
					outH = (height_ >> i) > 0u ? (height_ >> i) : 1u;
					ctx.CSSetShader(*dualUpAccumShader_);
				}

				ctx.CSSetShaderResource(0, pyramidRT[i]->GetRenderTargetSRV());
				ctx.CSSetUnorderedAccessView(0, dst->GetRenderTargetUAV());
				ctx.Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
				ctx.CSUnsetShaderResource(0);
				ctx.CSUnsetUnorderedAccessView(0);
			}

			// シーン + ブルームを合成
			ctx.CSSetShader(*compositeShader_);
			ctx.CSSetShaderResource(0, sceneRT->GetRenderTargetSRV());
			ctx.CSSetShaderResource(1, brightRT->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(0, finalRT->GetRenderTargetUAV());
			ctx.Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetShaderResource(1);
			ctx.CSUnsetUnorderedAccessView(0);

			ctx.CSUnsetShader();

			// ImGui 等その後のパスが finalRT に描画できるよう RTV として復元する
			ctx.OMSetRenderTargets(1, finalRT);
		}
	}
}
