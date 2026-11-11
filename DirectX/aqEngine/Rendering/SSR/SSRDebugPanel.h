#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace rendering
	{
		class SSRRenderer;

		/**
		 * SSR のパラメータを触るデバッグパネル (RenderingDebugPanel のタブ "SSR")。
		 * SSRRenderer の寿命は SSRPass が持つので、パネルはそれより先に破棄されること (AppHost が保証)。
		 */
		class SSRDebugPanel : public IDebugRenderable
		{
		private:
			SSRRenderer& ssr_;


		public:
			explicit SSRDebugPanel(SSRRenderer& ssr);

			void        DebugRenderMenu()     override {}
			void        DebugRender()         override {}
			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "SSR"; }
		};
	}
}
#endif
