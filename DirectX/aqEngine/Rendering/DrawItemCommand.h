#pragma once
#include "IRenderCommand.h"
#include "RenderFrame.h"

namespace aq
{
	namespace rendering
	{
		/**
		 * Records one draw call.
		 * Execute() allocates a per-draw constant buffer from FrameContext::constantBufferPool,
		 * fills it with world/view/projection, and issues the draw.
		 */
		class DrawItemCommand final : public IRenderCommand
		{
		public:
			DrawItemCommand(const RenderItem& item, const CameraData& camera);
			void Execute(graphics::RenderContext& ctx, FrameContext& fc) const override;

		private:
			RenderItem item_;
			CameraData camera_;
		};
	}
}
