#include "aq.h"
#include "SSAOComputeCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			/** スロット (SSAO.fx / SSAOBlur.fx と一致させる) */
			static constexpr uint32_t SSAO_CB_SLOT = 0;

			/** ① AO の計算 (SSAO.fx) */
			static constexpr uint32_t AO_GBUFFER1_SLOT = 0;
			static constexpr uint32_t AO_GBUFFER2_SLOT = 1;
			static constexpr uint32_t AO_GBUFFER3_SLOT = 2;

			/** ② ぼかし (SSAOBlur.fx) */
			static constexpr uint32_t BLUR_RAW_AO_SLOT   = 0;
			static constexpr uint32_t BLUR_GBUFFER1_SLOT = 1;
			static constexpr uint32_t BLUR_GBUFFER2_SLOT = 2;

			/** 出力 (どちらも u0) */
			static constexpr uint32_t OUTPUT_UAV_SLOT = 0;

			/** SSAO.fx / SSAOBlur.fx の numthreads */
			static constexpr uint32_t THREAD_GROUP_SIZE = 8;
		}


		SSAOComputeCommand::SSAOComputeCommand(
			graphics::IShader& aoCS,
			graphics::IShader& blurCS,
			RenderTargetHandle gbuffer1Handle,
			RenderTargetHandle gbuffer2Handle,
			RenderTargetHandle gbuffer3Handle,
			RenderTargetHandle rawAOHandle,
			RenderTargetHandle blurredAOHandle,
			const SSAOCBData&  cbData)
			: aoCS_(&aoCS)
			, blurCS_(&blurCS)
			, gbuffer1Handle_(gbuffer1Handle)
			, gbuffer2Handle_(gbuffer2Handle)
			, gbuffer3Handle_(gbuffer3Handle)
			, rawAOHandle_(rawAOHandle)
			, blurredAOHandle_(blurredAOHandle)
			, cbData_(cbData)
		{
		}


		void SSAOComputeCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			// 資源を先にそろえる。1 つでも欠けたら何もしない (合成は前フレームまでの AO を読むことになるが、
			// RT が欠けるのはリサイズ失敗などの異常時だけ)
			auto& gd        = graphics::GraphicsDevice::Get();
			auto* gbuffer1  = gd.GetRenderTarget(gbuffer1Handle_);
			auto* gbuffer2  = gd.GetRenderTarget(gbuffer2Handle_);
			auto* gbuffer3  = gd.GetRenderTarget(gbuffer3Handle_);
			auto* rawAO     = gd.GetRenderTarget(rawAOHandle_);
			auto* blurredAO = gd.GetRenderTarget(blurredAOHandle_);
			if (!aoCS_ || !blurCS_ || !gbuffer1 || !gbuffer2 || !gbuffer3 || !rawAO || !blurredAO) { return; }

			const uint32_t width  = static_cast<uint32_t>(cbData_.viewRect.z);
			const uint32_t height = static_cast<uint32_t>(cbData_.viewRect.w);
			if (width == 0 || height == 0) { return; }

			// CB はフレームスロットごとのプールから取る (GPU 実行中の CB を上書きしない)。
			// AO とぼかしは同じ値を読むので 1 つを共用する。
			graphics::IConstantBuffer* ssaoCB = fc.perDrawCBPool ? fc.perDrawCBPool->Allocate() : nullptr;
			if (!ssaoCB) { return; }

			ctx.UpdateSubresource(*ssaoCB, cbData_);
			ctx.CSSetConstantBuffer(SSAO_CB_SLOT, *ssaoCB);

			// ビュー矩形の範囲だけを回す (シェーダ側で矩形の原点を足す)
			const uint32_t groupX = (width  + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE;
			const uint32_t groupY = (height + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE;

			// ① AO の計算: G-Buffer → 生の AO
			ctx.CSSetShader(*aoCS_);
			ctx.CSSetShaderResource(AO_GBUFFER1_SLOT, gbuffer1->GetRenderTargetSRV());
			ctx.CSSetShaderResource(AO_GBUFFER2_SLOT, gbuffer2->GetRenderTargetSRV());
			ctx.CSSetShaderResource(AO_GBUFFER3_SLOT, gbuffer3->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(OUTPUT_UAV_SLOT, rawAO->GetRenderTargetUAV());
			ctx.Dispatch(groupX, groupY, 1);

			// 生の AO を次で SRV として読むので、UAV を外しておく
			ctx.CSUnsetShaderResource(AO_GBUFFER1_SLOT);
			ctx.CSUnsetShaderResource(AO_GBUFFER2_SLOT);
			ctx.CSUnsetShaderResource(AO_GBUFFER3_SLOT);
			ctx.CSUnsetUnorderedAccessView(OUTPUT_UAV_SLOT);

			// ② ぼかし: 生の AO + G-Buffer → ぼかした AO
			ctx.CSSetShader(*blurCS_);
			ctx.CSSetShaderResource(BLUR_RAW_AO_SLOT,   rawAO->GetRenderTargetSRV());
			ctx.CSSetShaderResource(BLUR_GBUFFER1_SLOT, gbuffer1->GetRenderTargetSRV());
			ctx.CSSetShaderResource(BLUR_GBUFFER2_SLOT, gbuffer2->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(OUTPUT_UAV_SLOT, blurredAO->GetRenderTargetUAV());
			ctx.Dispatch(groupX, groupY, 1);

			// アンバインド (合成がぼかした AO を PS の SRV で読む)
			ctx.CSUnsetShaderResource(BLUR_RAW_AO_SLOT);
			ctx.CSUnsetShaderResource(BLUR_GBUFFER1_SLOT);
			ctx.CSUnsetShaderResource(BLUR_GBUFFER2_SLOT);
			ctx.CSUnsetUnorderedAccessView(OUTPUT_UAV_SLOT);
			ctx.CSUnsetShader();
		}
	}
}
