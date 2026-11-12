#include "aq.h"
#include "DoFFocusCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 被写界深度のオートフォーカス
		 */
		DoFFocusCommand::DoFFocusCommand(
			graphics::IShader*         shader,
			graphics::IConstantBuffer* constantBuffer,
			RenderTargetHandle         worldPosRT,
			RenderTargetHandle         focusRT,
			const DoFFocusCBData&      cbData)
			: shader_(shader)
			, constantBuffer_(constantBuffer)
			, worldPosRTHandle_(worldPosRT)
			, focusRTHandle_(focusRT)
			, cbData_(cbData)
		{
		}


		void DoFFocusCommand::Execute(graphics::RenderContext& ctx, FrameContext& /*fc*/) const
		{
			auto& gd = graphics::GraphicsDevice::Get();
			auto* worldPosRT = gd.GetRenderTarget(worldPosRTHandle_);
			auto* focusRT    = gd.GetRenderTarget(focusRTHandle_);
			if (!shader_ || !constantBuffer_ || !worldPosRT || !focusRT) { return; }

			// SRV/UAV ハザード回避のため RTV を外してから compute を回す。
			ctx.OMSetRenderTargets(0, nullptr);

			ctx.UpdateSubresource(*constantBuffer_, cbData_);
			ctx.CSSetConstantBuffer(0, *constantBuffer_);

			ctx.CSSetShader(*shader_);
			ctx.CSSetShaderResource(0, worldPosRT->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(0, focusRT->GetRenderTargetUAV());

			// 1 スレッドが中央 25 画素を読み、1 回だけ書く(設計書 §0.4 の 5)。
			ctx.Dispatch(1, 1, 1);

			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetUnorderedAccessView(0);
			ctx.CSUnsetShader();
		}
	}
}
