#include "aq.h"
#include "DoFPassCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 被写界深度
		 */
		DoFPassCommand::DoFPassCommand(
			graphics::IShader*         shader,
			graphics::IConstantBuffer* constantBuffer,
			RenderTargetHandle         inputRT,
			RenderTargetHandle         worldPosRT,
			RenderTargetHandle         outputRT,
			RenderTargetHandle         cocRT,
			RenderTargetHandle         focusRT,
			const DoFCBData&           cbData)
			: shader_(shader)
			, constantBuffer_(constantBuffer)
			, inputRTHandle_(inputRT)
			, worldPosRTHandle_(worldPosRT)
			, outputRTHandle_(outputRT)
			, cocRTHandle_(cocRT)
			, focusRTHandle_(focusRT)
			, cbData_(cbData)
		{
		}


		void DoFPassCommand::Execute(graphics::RenderContext& ctx, FrameContext& /*fc*/) const
		{
			auto& gd = graphics::GraphicsDevice::Get();
			auto* inputRT    = gd.GetRenderTarget(inputRTHandle_);
			auto* worldPosRT = gd.GetRenderTarget(worldPosRTHandle_);
			auto* outputRT   = gd.GetRenderTarget(outputRTHandle_);
			auto* cocRT      = gd.GetRenderTarget(cocRTHandle_);
			if (!shader_ || !constantBuffer_ || !inputRT || !worldPosRT || !outputRT || !cocRT) { return; }

			// オートフォーカスを使わないフレームも t2 を未バインドにしない(API によっては未バインドの SRV が
			// 検証エラーや不定値になる)ため、ダミーとして worldPos を入れる。useAutoFocus = 0 なのでシェーダは読まない。
			auto* focusRT = (cbData_.useAutoFocus != 0) ? gd.GetRenderTarget(focusRTHandle_) : nullptr;
			if (cbData_.useAutoFocus != 0 && !focusRT) { return; }
			auto* focusOrDummyRT = focusRT ? focusRT : worldPosRT;

			// SRV/UAV ハザード回避のため RTV を外してから compute を回す。
			ctx.OMSetRenderTargets(0, nullptr);

			ctx.UpdateSubresource(*constantBuffer_, cbData_);
			ctx.CSSetConstantBuffer(0, *constantBuffer_);

			ctx.CSSetShader(*shader_);
			ctx.CSSetShaderResource(0, inputRT->GetRenderTargetSRV());
			ctx.CSSetShaderResource(1, worldPosRT->GetRenderTargetSRV());
			ctx.CSSetShaderResource(2, focusOrDummyRT->GetRenderTargetSRV());
			ctx.CSSetUnorderedAccessView(0, outputRT->GetRenderTargetUAV());
			ctx.CSSetUnorderedAccessView(1, cocRT->GetRenderTargetUAV());

			const uint32_t width  = static_cast<uint32_t>(cbData_.screenWidth);
			const uint32_t height = static_cast<uint32_t>(cbData_.screenHeight);
			ctx.Dispatch((width + 7) / 8, (height + 7) / 8, 1);

			ctx.CSUnsetShaderResource(0);
			ctx.CSUnsetShaderResource(1);
			ctx.CSUnsetShaderResource(2);
			ctx.CSUnsetUnorderedAccessView(0);
			ctx.CSUnsetUnorderedAccessView(1);
			ctx.CSUnsetShader();
		}
	}
}
