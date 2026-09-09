#include "aq.h"
#include "TonemapPassCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		TonemapPassCommand::TonemapPassCommand(
			graphics::IShader*         compositeShader,
			graphics::IConstantBuffer* bloomCB,
			RenderTargetHandle         sceneRT,
			RenderTargetHandle         bloomRT,
			RenderTargetHandle         finalRT,
			float                      intensity,
			uint32_t                   width,
			uint32_t                   height,
			float                      exposure,
			uint32_t                   tonemapMode,
			float                      whitePoint,
			uint32_t                   applyGamma)
			: compositeShader_(compositeShader)
			, bloomCB_(bloomCB)
			, sceneRTHandle_(sceneRT)
			, bloomRTHandle_(bloomRT)
			, finalRTHandle_(finalRT)
			, intensity_(intensity)
			, width_(width)
			, height_(height)
			, exposure_(exposure)
			, tonemapMode_(tonemapMode)
			, whitePoint_(whitePoint)
			, applyGamma_(applyGamma)
		{
		}


		void TonemapPassCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			auto& device   = graphics::GraphicsDevice::Get();
			auto* sceneRT  = device.GetRenderTarget(sceneRTHandle_);
			auto* bloomRT  = device.GetRenderTarget(bloomRTHandle_);
			auto* finalRT  = device.GetRenderTarget(finalRTHandle_);
			if (!compositeShader_ || !bloomCB_ || !sceneRT || !bloomRT || !finalRT) return;

			// RTV を外してから SRV/UAV として使用する
			ctx.OMSetRenderTargets(0, nullptr);

			BloomCBData cb{};
			cb.intensity   = intensity_;
			cb.width       = width_;
			cb.height      = height_;
			cb.exposure    = exposure_;
			cb.tonemapMode = tonemapMode_;
			cb.whitePoint  = whitePoint_;
			cb.applyGamma  = applyGamma_;
			ctx.UpdateSubresource(*bloomCB_, cb);
			ctx.CSSetConstantBuffer(0, *bloomCB_);

			// シーン + ブルームを合成
			ctx.CSSetShader(*compositeShader_);
			ctx.CSSetShaderResource(0, sceneRT->GetRenderTargetSRV());
			ctx.CSSetShaderResource(1, bloomRT->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(0, finalRT->GetRenderTargetUAV());
			ctx.Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetShaderResource(1);
			ctx.CSUnsetUnorderedAccessView(0);

			ctx.CSUnsetShader();
		}
	}
}
