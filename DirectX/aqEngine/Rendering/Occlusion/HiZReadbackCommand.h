#pragma once
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"

namespace aq
{
	namespace rendering
	{
		class HiZRenderer;

		/**
		 * Hi-Z の粗いレベルを GPU→CPU リードバックし、HiZRenderer へ届けるコマンド。
		 * GraphicsDevice::ReadbackOffscreenR32 が frames-in-flight を考慮した遅延読み出しを行う。
		 */
		class HiZReadbackCommand final : public IRenderCommand
		{
		public:
			HiZReadbackCommand(HiZRenderer* renderer, RenderTargetHandle level, uint32_t width, uint32_t height)
				: renderer_(renderer), level_(level), width_(width), height_(height) {}

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			HiZRenderer*       renderer_;
			RenderTargetHandle level_;
			uint32_t           width_;
			uint32_t           height_;
		};
	}
}
