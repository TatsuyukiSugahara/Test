#include "aq.h"
#include "FullscreenComputeCommand.h"
#include "Rendering/FrameContext.h"
#include "Graphics/RenderContext.h"
#include "Graphics/IRenderTarget.h"
#include <cstring>


namespace aq
{
	namespace rendering
	{
		FullscreenComputeCommand::FullscreenComputeCommand(
			graphics::IShader*         computeShader,
			const RenderTargetHandle*  inputs, const uint32_t inputCount,
			const RenderTargetHandle   output,
			graphics::IConstantBuffer* constantBuffer, const void* cbData, const uint32_t cbSizeBytes,
			const uint32_t width, const uint32_t height)
			: computeShader_(computeShader)
			, inputCount_(inputCount < MAX_INPUT_COUNT ? inputCount : MAX_INPUT_COUNT)
			, output_(output)
			, constantBuffer_(constantBuffer)
			, cbData_{}
			, width_(width)
			, height_(height)
		{
			for (uint32_t i = 0; i < inputCount_; ++i) {
				inputs_[i] = inputs[i];
			}

			if (constantBuffer_ && cbData)
			{
				const uint32_t copySize = cbSizeBytes < MAX_CB_SIZE_BYTES ? cbSizeBytes : MAX_CB_SIZE_BYTES;
				std::memcpy(cbData_.data(), cbData, copySize);
			}
		}


		void FullscreenComputeCommand::Execute(graphics::RenderContext& ctx, FrameContext&) const
		{
			auto& device = graphics::GraphicsDevice::Get();

			// 入力・出力 RT が 1 つでも取れなければ何もしない(TonemapPassCommand と同じ作法)。
			graphics::IRenderTarget* inputRTs[MAX_INPUT_COUNT] = {};
			for (uint32_t i = 0; i < inputCount_; ++i) {
				inputRTs[i] = device.GetRenderTarget(inputs_[i]);
				if (!inputRTs[i]) { return; }
			}
			auto* out = device.GetRenderTarget(output_);
			if (!computeShader_ || !out) { return; }

			// RTV を外してから SRV/UAV として使用する
			ctx.OMSetRenderTargets(0, nullptr);

			if (constantBuffer_)
			{
				ctx.UpdateSubresource(*constantBuffer_, cbData_);
				ctx.CSSetConstantBuffer(0, *constantBuffer_);
			}

			ctx.CSSetShader(*computeShader_);
			for (uint32_t i = 0; i < inputCount_; ++i) {
				ctx.CSSetShaderResource(i, inputRTs[i]->GetRenderTargetSRV());
			}
			ctx.CSSetUnorderedAccessView(0, out->GetRenderTargetUAV());

			ctx.Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);

			for (uint32_t i = 0; i < inputCount_; ++i) {
				ctx.CSUnsetShaderResource(i);
			}
			ctx.CSUnsetUnorderedAccessView(0);
			ctx.CSUnsetShader();
		}
	}
}
