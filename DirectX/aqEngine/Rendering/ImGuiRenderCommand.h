#pragma once
#ifdef AQ_IMGUI
#include "IRenderCommand.h"

struct ImDrawData;

namespace aq
{
	namespace rendering
	{
		class ImGuiRenderCommand : public IRenderCommand
		{
			ImDrawData* drawData_;

		public:
			explicit ImGuiRenderCommand(ImDrawData* data);
			void Execute(graphics::RenderContext&, FrameContext&) const override;
		};
	}
}
#endif // AQ_IMGUI
