#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace rendering
	{
		class BloomRenderer;

		/** Bloom パラメータのデバッグ UI パネル。 */
		class BloomDebugPanel : public IDebugRenderable
		{
		public:
			explicit BloomDebugPanel(BloomRenderer& renderer);

			void DebugRenderMenu() override;
			void DebugRender()     override;

		private:
			BloomRenderer& renderer_;
			bool           show_ = false;
		};
	}
}
#endif
