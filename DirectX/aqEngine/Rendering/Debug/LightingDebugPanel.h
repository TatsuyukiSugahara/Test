#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace rendering
	{
		class LightingDebugPanel : public IDebugRenderable
		{
		public:
			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "Lighting"; }
		};
	}
}
#endif
