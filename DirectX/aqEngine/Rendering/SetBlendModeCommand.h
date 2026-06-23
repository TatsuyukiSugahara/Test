#pragma once
#include "IRenderCommand.h"
#include "Graphics/IRenderContextImpl.h"
#include "Graphics/RenderContext.h"

namespace aq
{
	namespace rendering
	{
		class SetBlendModeCommand : public IRenderCommand
		{
		public:
			explicit SetBlendModeCommand(graphics::BlendMode mode) : mode_(mode) {}

			void Execute(graphics::RenderContext& ctx, FrameContext&) const override
			{
				ctx.OMSetBlendMode(mode_);
			}

		private:
			graphics::BlendMode mode_;
		};


		class SetScissorRectCommand : public IRenderCommand
		{
		public:
			explicit SetScissorRectCommand(bool enabled, int x = 0, int y = 0, int w = 0, int h = 0)
				: enabled_(enabled), x_(x), y_(y), w_(w), h_(h)
			{}

			void Execute(graphics::RenderContext& ctx, FrameContext&) const override
			{
				ctx.RSSetScissorEnabled(enabled_);
				if (enabled_)
					ctx.RSSetScissorRect(x_, y_, w_, h_);
			}

		private:
			bool enabled_;
			int  x_, y_, w_, h_;
		};
	}
}
