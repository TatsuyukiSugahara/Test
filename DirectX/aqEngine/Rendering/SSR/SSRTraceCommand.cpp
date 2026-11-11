#include "aq.h"
#include "SSRTraceCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		namespace
		{
			/** スロット (SSRTrace.fx と一致させる) */
			static constexpr uint32_t TRACE_CB_SLOT   = 0;
			static constexpr uint32_t SCENE_SLOT      = 0;
			static constexpr uint32_t GBUFFER1_SLOT   = 1;
			static constexpr uint32_t GBUFFER2_SLOT   = 2;
			static constexpr uint32_t GBUFFER3_SLOT   = 3;
			static constexpr uint32_t OUTPUT_UAV_SLOT = 0;

			/** SSRTrace.fx の numthreads */
			static constexpr uint32_t THREAD_GROUP_SIZE = 8;
		}


		SSRTraceCommand::SSRTraceCommand(
			graphics::IShader& traceCS,
			RenderTargetHandle sceneHandle,
			RenderTargetHandle gbuffer1Handle,
			RenderTargetHandle gbuffer2Handle,
			RenderTargetHandle gbuffer3Handle,
			RenderTargetHandle outputHandle,
			const SSRCBData&   cbData)
			: traceCS_(&traceCS)
			, sceneHandle_(sceneHandle)
			, gbuffer1Handle_(gbuffer1Handle)
			, gbuffer2Handle_(gbuffer2Handle)
			, gbuffer3Handle_(gbuffer3Handle)
			, outputHandle_(outputHandle)
			, cbData_(cbData)
		{
		}


		void SSRTraceCommand::Execute(graphics::RenderContext& ctx, FrameContext& fc) const
		{
			auto& gd       = graphics::GraphicsDevice::Get();
			auto* scene    = gd.GetRenderTarget(sceneHandle_);
			auto* gbuffer1 = gd.GetRenderTarget(gbuffer1Handle_);
			auto* gbuffer2 = gd.GetRenderTarget(gbuffer2Handle_);
			auto* gbuffer3 = gd.GetRenderTarget(gbuffer3Handle_);
			auto* output   = gd.GetRenderTarget(outputHandle_);
			if (!traceCS_ || !scene || !gbuffer1 || !gbuffer2 || !gbuffer3 || !output) { return; }

			const uint32_t width  = static_cast<uint32_t>(cbData_.viewRect.z);
			const uint32_t height = static_cast<uint32_t>(cbData_.viewRect.w);
			if (width == 0 || height == 0) { return; }

			// CB はフレームスロットごとのプールから取る (GPU 実行中の CB を上書きしない)
			graphics::IConstantBuffer* traceCB = fc.perDrawCBPool ? fc.perDrawCBPool->Allocate() : nullptr;
			if (!traceCB) { return; }

			// Scene を SRV で読むので、SRV/UAV ハザード回避のため RTV を外してから compute を回す (§0.6-2)。
			// 合成の前に SSRPass が Scene を RT としてバインドし直す。
			ctx.OMSetRenderTargets(0, nullptr);

			ctx.UpdateSubresource(*traceCB, cbData_);
			ctx.CSSetConstantBuffer(TRACE_CB_SLOT, *traceCB);

			ctx.CSSetShader(*traceCS_);
			ctx.CSSetShaderResource(SCENE_SLOT,    scene->GetRenderTargetSRV());
			ctx.CSSetShaderResource(GBUFFER1_SLOT, gbuffer1->GetRenderTargetSRV());
			ctx.CSSetShaderResource(GBUFFER2_SLOT, gbuffer2->GetRenderTargetSRV());
			ctx.CSSetShaderResource(GBUFFER3_SLOT, gbuffer3->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(OUTPUT_UAV_SLOT, output->GetRenderTargetUAV());

			// ビュー矩形の範囲だけを回す (シェーダ側で矩形の原点を足す。§0.6-4)
			ctx.Dispatch((width  + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE,
			             (height + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE, 1);

			ctx.CSUnsetShaderResource(SCENE_SLOT);
			ctx.CSUnsetShaderResource(GBUFFER1_SLOT);
			ctx.CSUnsetShaderResource(GBUFFER2_SLOT);
			ctx.CSUnsetShaderResource(GBUFFER3_SLOT);
			ctx.CSUnsetUnorderedAccessView(OUTPUT_UAV_SLOT);
			ctx.CSUnsetShader();
		}
	}
}
