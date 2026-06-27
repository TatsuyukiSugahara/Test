#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"

namespace aq
{
	namespace rendering
	{
		class DeferredRenderer;
		class IShadowRenderer;

		/**
		 * GBuffer 4 枚とシャドウデプスマップを ImGui Image で表示するデバッグパネル。
		 * RenderingDebugPanel のタブとして追加する。
		 */
		class DeferredDebugPanel : public IDebugRenderable
		{
		public:
			DeferredDebugPanel(DeferredRenderer& deferred, IShadowRenderer* shadow);

			void        DebugRenderMenu()     override {}
			void        DebugRender()         override {}
			void        RenderContent()       override;
			const char* GetDebugLabel() const override { return "GBuffer"; }

		private:
			DeferredRenderer& deferred_;
			IShadowRenderer*  shadow_;
			float             previewSize_ = 180.0f;
		};
	}
}
#endif
