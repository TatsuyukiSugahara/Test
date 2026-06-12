#pragma once

namespace engine
{
	namespace graphics  { class RenderContext; }
	namespace rendering { struct FrameContext;  }

	namespace rendering
	{
		class IRenderCommand
		{
		public:
			virtual ~IRenderCommand() = default;
			virtual void Execute(graphics::RenderContext& ctx, FrameContext& fc) const = 0;
		};
	}
}
