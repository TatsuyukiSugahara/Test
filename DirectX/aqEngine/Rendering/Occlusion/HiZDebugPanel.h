#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace rendering
	{
		class HiZRenderer;

		/** Hi-Z ピラミッドの各レベルを可視化するデバッグ UI パネル。 */
		class HiZDebugPanel : public IDebugRenderable
		{
		public:
			explicit HiZDebugPanel(HiZRenderer& renderer);

			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "Hi-Z"; }

		private:
			HiZRenderer& renderer_;
			float        previewSize_ = 200.0f;
		};
	}
}
#endif
