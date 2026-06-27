#include "aq.h"
#include "HiZBuildCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		HiZBuildCommand::HiZBuildCommand(graphics::IShader*         reconstructShader,
		                                 graphics::IShader*         downsampleShader,
		                                 graphics::IConstantBuffer* hiZCB,
		                                 const HiZCBData&           cbData,
		                                 RenderTargetHandle         gbufferWorldPos,
		                                 const RenderTargetHandle (&levelHandles)[HiZRenderer::kMaxLevels],
		                                 const uint32_t (&levelW)[HiZRenderer::kMaxLevels],
		                                 const uint32_t (&levelH)[HiZRenderer::kMaxLevels],
		                                 uint32_t levelCount)
			: reconstructShader_(reconstructShader)
			, downsampleShader_(downsampleShader)
			, hiZCB_(hiZCB)
			, cbData_(cbData)
			, gbufferWorldPos_(gbufferWorldPos)
			, levelCount_(levelCount)
		{
			for (uint32_t i = 0; i < HiZRenderer::kMaxLevels; ++i)
			{
				levelHandles_[i] = levelHandles[i];
				levelW_[i]       = levelW[i];
				levelH_[i]       = levelH[i];
			}
		}


		void HiZBuildCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			if (!reconstructShader_ || !downsampleShader_ || levelCount_ == 0) return;

			auto& device   = graphics::GraphicsDevice::Get();
			auto* worldPos = device.GetRenderTarget(gbufferWorldPos_);
			if (!worldPos) return;

			graphics::IRenderTarget* levels[HiZRenderer::kMaxLevels] = {};
			for (uint32_t i = 0; i < levelCount_; ++i)
			{
				levels[i] = device.GetRenderTarget(levelHandles_[i]);
				if (!levels[i]) return;
			}

			// RTV を外してから SRV/UAV として使用する
			ctx.OMSetRenderTargets(0, nullptr);

			ctx.UpdateSubresource(*hiZCB_, cbData_);
			ctx.CSSetConstantBuffer(0, *hiZCB_);

			// レベル0: GBuffer2 worldPos から深度を再構成 (2x2 max)
			ctx.CSSetShader(*reconstructShader_);
			ctx.CSSetShaderResource(0, worldPos->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(0, levels[0]->GetRenderTargetUAV());
			ctx.Dispatch((levelW_[0] + 7) / 8, (levelH_[0] + 7) / 8, 1);
			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetUnorderedAccessView(0);

			// レベル1..N-1: max ダウンサンプル
			ctx.CSSetShader(*downsampleShader_);
			for (uint32_t i = 1; i < levelCount_; ++i)
			{
				ctx.CSSetShaderResource(0, levels[i - 1]->GetRenderTargetSRV());
				ctx.CSSetUnorderedAccessView(0, levels[i]->GetRenderTargetUAV());
				ctx.Dispatch((levelW_[i] + 7) / 8, (levelH_[i] + 7) / 8, 1);
				ctx.CSUnsetShaderResource(0);
				ctx.CSUnsetUnorderedAccessView(0);
			}

			ctx.CSUnsetShader();
		}
	}
}
