#pragma once
#include "Rendering/IRenderCommand.h"
#include "Rendering/RenderTargetHandle.h"
#include "Math/Matrix.h"
#include "HiZRenderer.h"

namespace aq
{
	namespace graphics { class IShader; class IConstantBuffer; }

	namespace rendering
	{
		/**
		 * Hi-Z ピラミッドを構築する compute コマンド。
		 * 1) Reconstruct: GBuffer2 worldPos → レベル0 (深度の max)
		 * 2) Downsample : レベル i → レベル i+1 (max-reduction)
		 */
		class HiZBuildCommand final : public IRenderCommand
		{
		public:
			HiZBuildCommand(graphics::IShader*         reconstructShader,
			                graphics::IShader*         downsampleShader,
			                graphics::IConstantBuffer* hiZCB,
			                const HiZCBData&           cbData,
			                RenderTargetHandle         gbufferWorldPos,
			                const RenderTargetHandle (&levelHandles)[HiZRenderer::kMaxLevels],
			                const uint32_t (&levelW)[HiZRenderer::kMaxLevels],
			                const uint32_t (&levelH)[HiZRenderer::kMaxLevels],
			                uint32_t levelCount);

			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			graphics::IShader*         reconstructShader_;
			graphics::IShader*         downsampleShader_;
			graphics::IConstantBuffer* hiZCB_;
			HiZCBData                  cbData_;
			RenderTargetHandle         gbufferWorldPos_;
			RenderTargetHandle         levelHandles_[HiZRenderer::kMaxLevels];
			uint32_t                   levelW_[HiZRenderer::kMaxLevels];
			uint32_t                   levelH_[HiZRenderer::kMaxLevels];
			uint32_t                   levelCount_;
		};
	}
}
