#include "aq.h"
#include "SSRCompositeCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderContextImpl.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IRenderTarget.h"
#include "Rendering/IBL/IBLEnvironment.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			/** スロット (PBRLighting.fx の PSMainIBL と同じ配置。SSRComposite.fx と一致させる) */
			static constexpr uint32_t LIGHTING_CB_SLOT  = 1;
			static constexpr uint32_t COMPOSITE_CB_SLOT = 2;
			static constexpr uint32_t SSR_TRACE_SLOT    = 0;
			static constexpr uint32_t IBL_SPECULAR_SLOT = 6;
			static constexpr uint32_t IBL_BRDF_LUT_SLOT = 7;
			static constexpr uint32_t IBL_SAMPLER_SLOT  = 0;
			static constexpr uint32_t GBUFFER0_SLOT     = 8;
			static constexpr uint32_t GBUFFER1_SLOT     = 9;
			static constexpr uint32_t GBUFFER2_SLOT     = 10;
			static constexpr uint32_t GBUFFER3_SLOT     = 11;
		}


		SSRCompositeCommand::SSRCompositeCommand(
			graphics::IShader&                    compositeVS,
			graphics::IShader&                    compositePS,
			RenderTargetHandle                    gbuffer0Handle,
			RenderTargetHandle                    gbuffer1Handle,
			RenderTargetHandle                    gbuffer2Handle,
			RenderTargetHandle                    gbuffer3Handle,
			RenderTargetHandle                    ssrTraceHandle,
			std::shared_ptr<const IBLEnvironment> ibl,
			const float                           iblIntensity,
			const uint32_t                        testMode,
			const bool                            debugView)
			: compositeVS_(&compositeVS)
			, compositePS_(&compositePS)
			, gbuffer0Handle_(gbuffer0Handle)
			, gbuffer1Handle_(gbuffer1Handle)
			, gbuffer2Handle_(gbuffer2Handle)
			, gbuffer3Handle_(gbuffer3Handle)
			, ssrTraceHandle_(ssrTraceHandle)
			, ibl_(std::move(ibl))
			, iblIntensity_(iblIntensity)
			, testMode_(testMode)
			, debugView_(debugView)
		{
		}


		void SSRCompositeCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// 資源を先にそろえる。1 つでも欠けたら描かない (差分を足さない = IBL だけの絵のまま)。
			// 状態を変える前に判定し、途中で抜けても BlendMode / DepthMode を汚さない。
			auto& gd       = graphics::GraphicsDevice::Get();
			auto* gbuffer0 = gd.GetRenderTarget(gbuffer0Handle_);
			auto* gbuffer1 = gd.GetRenderTarget(gbuffer1Handle_);
			auto* gbuffer2 = gd.GetRenderTarget(gbuffer2Handle_);
			auto* gbuffer3 = gd.GetRenderTarget(gbuffer3Handle_);
			auto* ssrTrace = gd.GetRenderTarget(ssrTraceHandle_);
			if (!gbuffer0 || !gbuffer1 || !gbuffer2 || !gbuffer3 || !ssrTrace) { return; }

			if (!ibl_) { return; }
			graphics::IShaderResourceView* iblSpecular = ibl_->GetSpecularSRV();
			graphics::IShaderResourceView* iblBrdfLut  = ibl_->GetBrdfLutSRV();
			graphics::ISamplerState*       iblSampler  = ibl_->GetSampler();
			if (!iblSpecular || !iblBrdfLut || !iblSampler) { return; }

			// cameraPosition / globalSpecularScale はライティングと同じ LightingCB から読む
			if (!fc.lightingCB) { return; }

			// CB はフレームスロットごとのプールから取る (GPU 実行中の CB を上書きしない。IBL設計 §1.1)
			graphics::IConstantBuffer* compositeCB = fc.perDrawCBPool->Allocate();
			if (!compositeCB) { return; }

			// 差分の加算 (src.rgb × src.a + dst。PS の α は 1 固定)。深度は使わない。
			// デバッグ表示は確信度のグレーで G-Buffer の画素を上書きする。
			ctx.OMSetBlendMode(debugView_ ? graphics::BlendMode::Opaque : graphics::BlendMode::Additive);
			ctx.OMSetDepthMode(graphics::DepthMode::Disabled);

			// b1: per-frame lighting
			ctx.PSSetConstantBuffer(LIGHTING_CB_SLOT, *fc.lightingCB);

			// b2: 強度・ミップ数・testMode・デバッグ表示
			SSRCompositeCBData cbData;
			cbData.iblIntensity      = iblIntensity_;
			cbData.iblSpecularMaxMip = ibl_->GetSpecularMaxMip();
			cbData.testMode          = testMode_;
			cbData.debugView         = debugView_ ? 1u : 0u;
			ctx.UpdateSubresource(*compositeCB, cbData);
			ctx.PSSetConstantBuffer(COMPOSITE_CB_SLOT, *compositeCB);

			// t0: トレースの出力 RT
			ctx.PSSetShaderResource(SSR_TRACE_SLOT, ssrTrace->GetRenderTargetSRV());

			// t6 / t7 / s0: 鏡面キューブと BRDF LUT
			ctx.PSSetShaderResource(IBL_SPECULAR_SLOT, *iblSpecular);
			ctx.PSSetShaderResource(IBL_BRDF_LUT_SLOT, *iblBrdfLut);
			ctx.PsSetSampler(IBL_SAMPLER_SLOT, *iblSampler);

			// t8-t11: GBuffer
			ctx.PSSetShaderResource(GBUFFER0_SLOT, gbuffer0->GetRenderTargetSRV());
			ctx.PSSetShaderResource(GBUFFER1_SLOT, gbuffer1->GetRenderTargetSRV());
			ctx.PSSetShaderResource(GBUFFER2_SLOT, gbuffer2->GetRenderTargetSRV());
			ctx.PSSetShaderResource(GBUFFER3_SLOT, gbuffer3->GetRenderTargetSRV());

			// フルスクリーントライアングル (SV_VertexID ベース: 頂点バッファ不要)
			ctx.IASetPrimitiveTopology(graphics::PrimitiveTopology::TriangleList);
			ctx.VSSetShader(*compositeVS_);
			ctx.PSSetShader(*compositePS_);
			ctx.Draw(3, 0);

			// アンバインド (次パスで RT → SRV 競合を防ぐ。後続の空パスが t5 以降を別のキューブで使う)
			ctx.PSUnsetShaderResource(GBUFFER0_SLOT);
			ctx.PSUnsetShaderResource(GBUFFER1_SLOT);
			ctx.PSUnsetShaderResource(GBUFFER2_SLOT);
			ctx.PSUnsetShaderResource(GBUFFER3_SLOT);
			ctx.PSUnsetShaderResource(IBL_SPECULAR_SLOT);
			ctx.PSUnsetShaderResource(IBL_BRDF_LUT_SLOT);
			ctx.PSUnsetShaderResource(SSR_TRACE_SLOT);

			// パス契約 2: 入った状態 (Opaque / ReadWrite) に戻して終わる
			ctx.OMSetBlendMode(graphics::BlendMode::Opaque);
			ctx.OMSetDepthMode(graphics::DepthMode::ReadWrite);
		}
	}
}
