#include "aq.h"
#include "MotionBlurPassCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * カメラモーションブラー
		 */
		MotionBlurPassCommand::MotionBlurPassCommand(
			graphics::IShader*         shader,
			graphics::IConstantBuffer* constantBuffer,
			RenderTargetHandle         sceneRT,
			RenderTargetHandle         worldPosRT,
			RenderTargetHandle         outputRT,
			const MotionBlurCBData&    cbData)
			: shader_(shader)
			, constantBuffer_(constantBuffer)
			, sceneRTHandle_(sceneRT)
			, worldPosRTHandle_(worldPosRT)
			, outputRTHandle_(outputRT)
			, cbData_(cbData)
		{
		}


		void MotionBlurPassCommand::Execute(graphics::RenderContext& ctx, FrameContext& /*fc*/) const
		{
			auto& gd = graphics::GraphicsDevice::Get();
			auto* sceneRT    = gd.GetRenderTarget(sceneRTHandle_);
			auto* worldPosRT = gd.GetRenderTarget(worldPosRTHandle_);
			auto* outputRT   = gd.GetRenderTarget(outputRTHandle_);
			if (!shader_ || !constantBuffer_ || !sceneRT || !worldPosRT || !outputRT) { return; }

			// SRV/UAV ハザード回避のため RTV を外してから compute を回す。
			ctx.OMSetRenderTargets(0, nullptr);

			ctx.UpdateSubresource(*constantBuffer_, cbData_);
			ctx.CSSetConstantBuffer(0, *constantBuffer_);

			ctx.CSSetShader(*shader_);
			ctx.CSSetShaderResource(0, sceneRT->GetRenderTargetSRV());
			ctx.CSSetShaderResource(1, worldPosRT->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(0, outputRT->GetRenderTargetUAV());

			const uint32_t width  = static_cast<uint32_t>(cbData_.screenWidth);
			const uint32_t height = static_cast<uint32_t>(cbData_.screenHeight);
			ctx.Dispatch((width + 7) / 8, (height + 7) / 8, 1);

			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetShaderResource(1);
			ctx.CSUnsetUnorderedAccessView(0);
			ctx.CSUnsetShader();
		}
	}
}
